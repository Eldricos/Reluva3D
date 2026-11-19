// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The GPU backend (docs/common/graphics/glide.md, The GPU backend). Each primitive
// is set up on the CPU exactly as the software rasterizer sets it up
// (raster_setup.h): the snapped vertices, culling, each row's span of
// pixels by the fill rule, the plane of each value. Primitives are
// collected into a batch with the state they're drawn with and binned
// into 16x16 tiles; a batch goes to the GPU when something outside needs
// the buffers, texture memory changes under it, or it's full. The kernel
// (shaders/rasterize.comp) runs one workgroup a tile with any primitives,
// one thread a pixel, taking the tile's primitives in order through the
// pixel pipeline as the software one does.
//
// The frame buffer (three colour buffers and the auxiliary one, 16 bits a
// pixel as on the board) and every TMU's memory live on the GPU. The
// pipeline's own buffers are brought up to date on sync() (a lock, a read,
// a presented frame), one buffer at a time where only one is read; what
// the host then writes into them is found by comparing them with what was
// read back, row by row, and sent to the GPU before the next primitive.

#include "graphics/glide/gpu_renderer.h"

#include "graphics/glide/raster_setup.h"
#include "graphics/glide/shaders/glide_kernels.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

namespace reluva::glide {

namespace {

using namespace raster;

// A state record's words, as rasterize.comp reads them.
enum StateWord : int {
    kSColorFunc = 0,
    kSColorFactor = 1,
    kSColorInvert = 2,
    kSColorLocal = 3,
    kSColorOther = 4,
    kSAlphaFunc = 5,
    kSAlphaFactor = 6,
    kSAlphaInvert = 7,
    kSAlphaLocal = 8,
    kSAlphaOther = 9,
    kSAlphaLighting = 10,
    kSConstant = 11,
    kSChromakey = 12,
    kSChromaColor = 13,
    kSAlphaTest = 14,
    kSAlphaRef = 15,
    kSDepthMode = 16,
    kSDepthFunc = 17,
    kSDepthMask = 18,
    kSDepthBias = 19,
    kSColorMask = 20,
    kSAlphaMask = 21,
    kSDither = 22,
    kSFogMode = 23,
    kSFogColor = 24,
    kSBlendRgbSrc = 25,
    kSBlendRgbDst = 26,
    kSBlendAlphaSrc = 27,
    kSBlendAlphaDst = 28,
    kSTexture = 29,
    kSTmusUsed = 30,
    kSAlphaBuffer = 31,
    kSBlend = 32,
    kSColorSlot = 33,
    kSChromaRange = 34,
    kSChromaRangeMax = 35,
    kSFogTable = 36,
    kSChromaRangeMin = 52,
    kSTmu = 64,
    kSTmuWords = 48,
};
enum TmuWord : int {
    kTValid = 0,
    kTPerPixelLod = 1,
    kTLodMin = 2,
    kTLodMax = 3,
    kTFormat = 4,
    kTBytes = 5,
    kTMemBase = 6,
    kTMemMask = 7,
    kTPalette = 8,
    kTNcc = 9,
    kTClampS = 10,
    kTClampT = 11,
    kTMinFilter = 12,
    kTMagFilter = 13,
    kTMipmapMode = 14,
    kTLodBias = 15,
    kTRgbFunc = 16,
    kTRgbFactor = 17,
    kTRgbInvert = 18,
    kTAlphaFunc = 19,
    kTAlphaFactor = 20,
    kTAlphaInvert = 21,
    kTDetailBias = 22,
    kTDetailScale = 23,
    kTDetailMax = 24,
    kTSmallLod = 25,
    kTLargeLod = 26,
    kTBase = 27,
    kTSize = 36,
    kTNeedsDetail = 45,
};
constexpr uint32_t kStateWords = kSTmu + kSTmuWords * kMaxTmus;
constexpr uint32_t kPrimWords = 52;
// A TMU's tables in the batch: the palette, then NCC tables 0 and 1 (y[16],
// i[4][3], q[4][3] as words).
constexpr uint32_t kTableWords = 256 + 40 * 2;
enum PrimFlag : uint32_t { kFlip = 1, kDerivatives = 2, kClear = 4, kClearColor = 8, kClearAux = 16 };
constexpr int kTile = 16;

// The uniform block (std140: 12 words).
struct Params {
    uint32_t width;
    uint32_t height;
    uint32_t tilesX;
    uint32_t activeCount;
    uint32_t statesBase;
    uint32_t tablesBase;
    uint32_t primsBase;
    uint32_t spansBase;
    uint32_t tileOffsets;
    uint32_t tileList;
    uint32_t activeTiles;
    uint32_t pad0;
};

uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool usesOther(const CombineSettings &c) {
    return (c.function >= kFuncScaleOther && c.function <= kFuncScaleOtherMinusLocalAddLocalAlpha) ||
           c.factor == kFactorOtherAlpha || c.factor == kFactorOneMinusOtherAlpha;
}

bool blendUsesDstAlpha(const State &s) {
    auto dst = [](int f) { return f == kBlendDstAlpha || f == kBlendOneMinusDstAlpha; };
    return dst(s.blendRgbSrc) || dst(s.blendRgbDst) || dst(s.blendAlphaSrc) || dst(s.blendAlphaDst) ||
           s.blendRgbSrc == kBlendAlphaSaturate || s.blendAlphaSrc == kBlendAlphaSaturate;
}

bool usesTables(int format) {
    return format == kTexYiq422 || format == kTexP8 || format == 6 || format == kTexAyiq8422 || format == kTexAp88;
}

class GpuRenderer : public Renderer {
public:
    explicit GpuRenderer(std::unique_ptr<gfx::Compute> gpu) : _gpu(std::move(gpu)) {}
    ~GpuRenderer() override {
        for (gfx::ComputeBuffer b : {_frame, _tex, _batchBuffer})
            if (b)
                _gpu->destroyBuffer(b);
    }

    Error init() {
        gfx::ComputeKernelCode code;
        code.spirv = shaders::kRasterizeSpirv;
        code.spirvSize = sizeof(shaders::kRasterizeSpirv);
        code.msl = shaders::kRasterizeMsl;
        code.readOnlyBuffers = 2;
        code.readWriteBuffers = 1;
        code.uniformBuffers = 1;
        code.threadsX = kTile;
        code.threadsY = kTile;
        return _gpu->createKernel(code, _kernel);
    }

    void triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) override;
    void line(Pipeline &p, const Vertex &a, const Vertex &b) override;
    void point(Pipeline &p, const Vertex &a) override;
    void fragments(Pipeline &p, const Fragments &f) override;
    void clear(Pipeline &p, uint32_t argb, int alpha, int depth) override;
    void sync(Pipeline &p) override;
    void syncBuffer(Pipeline &p, int buffer) override;
    void textureMemoryChanged(Pipeline &p, int tmu, uint32_t start, uint32_t bytes) override;
    void tablesChanged(Pipeline &, int tmu) override {
        if (tmu >= 0 && tmu < kMaxTmus)
            _tableAt[size_t(tmu)] = -1;
    }

private:
    bool ready(Pipeline &p);
    void uploadSlotRows(Pipeline &p, int slot, int firstRow, int rows);
    void download(Pipeline &p, int slot);
    uint32_t stateFor(Pipeline &p);
    // A primitive over rows rowFirst.. of buffer rows (spans already in
    // spans_ from spanAt), its values' plane.
    size_t addPrim(Pipeline &p, uint32_t flags, int rowFirst, int rows, uint32_t spanAt, int x0, int x1, float fx0,
                   float fy0, const Values &base, const float *dx, const float *dy, bool writesAux);
    void addPixel(Pipeline &p, int x, int y, const Values &v);
    void flush();
    void flushIfFull();

    std::unique_ptr<gfx::Compute> _gpu;
    gfx::ComputeKernel _kernel = 0;
    gfx::ComputeBuffer _frame = 0;
    gfx::ComputeBuffer _tex = 0;
    gfx::ComputeBuffer _batchBuffer = 0;
    uint32_t _batchCapacity = 0; // bytes
    const Pipeline *_pipeline = nullptr;
    uint64_t _opened = 0;
    int _width = 0;
    int _height = 0;
    int _tmus = 0;
    uint32_t _tmuBytes = 0;
    std::array<bool, Pipeline::kSlots> _gpuNewer{};  // drawn on the GPU since read back
    std::array<bool, Pipeline::kSlots> _hostKnown{}; // the host's copy was read back (snapshot_ is it)
    std::array<std::vector<uint16_t>, Pipeline::kSlots> _snapshot;
    bool _checkHost = false;

    // The batch being collected.
    std::vector<uint32_t> _states;
    std::vector<uint32_t> _tables;
    std::vector<uint32_t> _prims;
    std::vector<uint32_t> _spans;
    struct Box {
        // inclusive
        int x0;
        int y0;
        int x1;
        int y1;
    };
    std::vector<Box> _boxes;
    std::vector<uint32_t> _record;
    std::vector<uint32_t> _lastRecord;
    uint32_t _lastState = 0;
    bool _haveState = false;
    std::array<int, kMaxTmus> _tableAt{-1, -1, -1};
    std::array<bool, Pipeline::kSlots> _touched{};
    std::vector<uint32_t> _scratch;
};

bool GpuRenderer::ready(Pipeline &p) {
    if (!p.isOpen())
        return false;
    if (&p != _pipeline || p.opened() != _opened || p.width() != _width || p.height() != _height) {
        flush();
        _pipeline = &p;
        _opened = p.opened();
        _width = p.width();
        _height = p.height();
        const uint32_t pixels = uint32_t(_width * _height);
        if (_frame)
            _gpu->destroyBuffer(_frame);
        _frame = 0;
        if (_gpu->createBuffer(pixels * 4u * uint32_t(Pipeline::kSlots), _frame))
            return false;
        for (int s = 0; s < Pipeline::kSlots; ++s)
            uploadSlotRows(p, s, 0, _height);
        {
            if (_tex)
                _gpu->destroyBuffer(_tex);
            _tex = 0;
            _tmus = p.board().tmus;
            _tmuBytes = p.tmuMemorySize();
            if (_gpu->createBuffer(_tmuBytes * uint32_t(_tmus), _tex))
                return false;
            for (int i = 0; i < _tmus; ++i)
                _gpu->upload(_tex, uint32_t(i) * _tmuBytes, p.tmuMemory(i), _tmuBytes);
        }
        _tableAt.fill(-1);
        _checkHost = false;
    }
    if (_checkHost) {
        // What the host wrote since the buffers were read back: rows that
        // differ from what was read, sent over.
        _checkHost = false;
        for (int s = 0; s < Pipeline::kSlots; ++s) {
            const uint16_t *host = p.slotPixels(s);
            if (!host || !_hostKnown[size_t(s)] || _gpuNewer[size_t(s)])
                continue;
            const uint16_t *known = _snapshot[size_t(s)].data();
            int first = -1;
            for (int y = 0; y <= _height; ++y) {
                const bool differs =
                    y < _height && std::memcmp(host + size_t(y) * size_t(_width), known + size_t(y) * size_t(_width),
                                               size_t(_width) * 2) != 0;
                if (differs && first < 0)
                    first = y;
                else if (!differs && first >= 0) {
                    uploadSlotRows(p, s, first, y - first);
                    first = -1;
                }
            }
        }
    }
    return _frame && _tex;
}

void GpuRenderer::uploadSlotRows(Pipeline &p, int slot, int firstRow, int rows) {
    const uint16_t *host = p.slotPixels(slot);
    const size_t at = size_t(firstRow) * size_t(_width);
    const size_t n = size_t(rows) * size_t(_width);
    _snapshot[size_t(slot)].resize(size_t(_width) * size_t(_height));
    _scratch.resize(n);
    for (size_t i = 0; i < n; ++i)
        _scratch[i] = host ? host[at + i] : 0;
    if (host)
        std::memcpy(_snapshot[size_t(slot)].data() + at, host + at, n * 2);
    const uint32_t base = uint32_t(slot) * uint32_t(_width * _height);
    _gpu->upload(_frame, (base + uint32_t(at)) * 4u, _scratch.data(), uint32_t(n * 4));
    _hostKnown[size_t(slot)] = host != nullptr;
    _gpuNewer[size_t(slot)] = false;
}

void GpuRenderer::download(Pipeline &p, int slot) {
    uint16_t *host = p.slotPixels(slot);
    if (!host || !_frame)
        return;
    const uint32_t pixels = uint32_t(_width * _height);
    _scratch.resize(pixels);
    if (_gpu->download(_frame, uint32_t(slot) * pixels * 4u, _scratch.data(), pixels * 4u))
        return;
    for (uint32_t i = 0; i < pixels; ++i)
        host[i] = uint16_t(_scratch[i]);
    _snapshot[size_t(slot)].assign(host, host + pixels);
    _hostKnown[size_t(slot)] = true;
    _gpuNewer[size_t(slot)] = false;
}

void GpuRenderer::sync(Pipeline &p) {
    flush();
    if (&p != _pipeline || p.opened() != _opened)
        return;
    for (int s = 0; s < Pipeline::kSlots; ++s)
        if (_gpuNewer[size_t(s)])
            download(p, s);
    _checkHost = true;
}

void GpuRenderer::syncBuffer(Pipeline &p, int buffer) {
    flush();
    if (&p != _pipeline || p.opened() != _opened)
        return;
    const int s = p.slotOf(buffer);
    if (s >= 0 && _gpuNewer[size_t(s)])
        download(p, s);
    _checkHost = true;
}

void GpuRenderer::textureMemoryChanged(Pipeline &p, int tmu, uint32_t start, uint32_t bytes) {
    if (&p != _pipeline || !_tex || tmu < 0 || tmu >= _tmus)
        return; // sent whole when the GPU's copy is made
    flush();    // what's waiting was drawn with the old texels
    const uint32_t from = start & ~3u;
    const uint32_t to = std::min(_tmuBytes, (start + bytes + 3) & ~3u);
    if (to > from)
        _gpu->upload(_tex, uint32_t(tmu) * _tmuBytes + from, p.tmuMemory(tmu) + from, to - from);
}

uint32_t GpuRenderer::stateFor(Pipeline &p) {
    const State &s = p.state();
    std::vector<uint32_t> &r = _record;
    r.assign(kStateWords, 0);
    r[kSColorFunc] = uint32_t(s.color.settings.function);
    r[kSColorFactor] = uint32_t(s.color.settings.factor);
    r[kSColorInvert] = s.color.settings.invert;
    r[kSColorLocal] = uint32_t(s.color.local);
    r[kSColorOther] = uint32_t(s.color.other);
    r[kSAlphaFunc] = uint32_t(s.alpha.settings.function);
    r[kSAlphaFactor] = uint32_t(s.alpha.settings.factor);
    r[kSAlphaInvert] = s.alpha.settings.invert;
    r[kSAlphaLocal] = uint32_t(s.alpha.local);
    r[kSAlphaOther] = uint32_t(s.alpha.other);
    r[kSAlphaLighting] = s.alphaControlsLighting;
    r[kSConstant] = s.constantColor;
    r[kSChromakey] = s.chromakey;
    r[kSChromaColor] = s.chromakeyColor;
    r[kSChromaRange] = s.chromaRange;
    r[kSChromaRangeMin] = s.chromaRangeMin;
    r[kSChromaRangeMax] = s.chromaRangeMax;
    r[kSAlphaTest] = uint32_t(s.alphaTest);
    r[kSAlphaRef] = uint32_t(s.alphaRef);
    r[kSDepthMode] = uint32_t(s.depthMode);
    r[kSDepthFunc] = uint32_t(s.depthFunc);
    r[kSDepthMask] = s.depthMask;
    r[kSDepthBias] = uint32_t(s.depthBias);
    r[kSColorMask] = s.colorMask;
    r[kSAlphaMask] = s.alphaMask;
    r[kSDither] = uint32_t(s.dither);
    r[kSFogMode] = uint32_t(s.fogMode);
    r[kSFogColor] = s.fogColor;
    r[kSBlendRgbSrc] = uint32_t(s.blendRgbSrc);
    r[kSBlendRgbDst] = uint32_t(s.blendRgbDst);
    r[kSBlendAlphaSrc] = uint32_t(s.blendAlphaSrc);
    r[kSBlendAlphaDst] = uint32_t(s.blendAlphaDst);
    r[kSColorSlot] = uint32_t(std::max(0, p.slotOf(s.renderBuffer)));
    for (int i = 0; i < 64; ++i)
        r[size_t(kSFogTable + i / 4)] |= uint32_t(s.fogTable[size_t(i)]) << (8 * (i & 3));
    // Which units take part, as the software renderer's prepare() decides.
    auto factorUsesTexture = [](int f) {
        return f == kFactorTextureAlpha || f == kFactorTextureRgb || f == kFactorOneMinusTextureAlpha ||
               f == kFactorOneMinusLodFraction;
    };
    const bool texture = s.color.other == kOtherTexture || s.alpha.other == kOtherTexture ||
                         factorUsesTexture(s.color.settings.factor) || factorUsesTexture(s.alpha.settings.factor) ||
                         s.alphaControlsLighting;
    int tmusUsed = 0;
    if (texture) {
        tmusUsed = 1;
        while (tmusUsed < p.board().tmus) {
            const TmuState &t = s.tmus[size_t(tmusUsed - 1)];
            if (!usesOther(t.rgb) && !usesOther(t.alpha))
                break;
            ++tmusUsed;
        }
    }
    r[kSTexture] = texture;
    r[kSTmusUsed] = uint32_t(tmusUsed);
    r[kSAlphaBuffer] = s.depthMode == kDepthDisable && blendUsesDstAlpha(s);
    r[kSBlend] = !(s.blendRgbSrc == kBlendOne && s.blendRgbDst == kBlendZero && s.blendAlphaSrc == kBlendOne &&
                   s.blendAlphaDst == kBlendZero);
    for (int i = 0; i < tmusUsed; ++i) {
        const TmuState &t = s.tmus[size_t(i)];
        uint32_t *m = r.data() + kSTmu + kSTmuWords * i;
        if (!(t.hasSource && knownFormat(t.info.format)))
            continue;
        m[kTValid] = 1;
        const int lodMin = 8 - t.info.largeLod;
        const int lodMax = t.mipmapMode != kMipmapDisable ? 8 - t.info.smallLod : lodMin;
        auto isTextureFactor = [](int f) {
            return f == kFactorTextureRgb || f == kFactorOneMinusLodFraction || f == kFactorTextureAlpha ||
                   f == kFactorOneMinusTextureAlpha;
        };
        m[kTPerPixelLod] = lodMax > lodMin || t.minFilter != t.magFilter || t.lodBlend ||
                           isTextureFactor(t.rgb.factor) || isTextureFactor(t.alpha.factor);
        m[kTLodMin] = uint32_t(lodMin);
        m[kTLodMax] = uint32_t(lodMax);
        const int format = p.textureFormat(i); // P_8 through the 6666 palette reads as format 6
        m[kTFormat] = uint32_t(format);
        m[kTBytes] = uint32_t(texelBytes(t.info.format));
        m[kTMemBase] = uint32_t(i) * _tmuBytes;
        m[kTMemMask] = _tmuBytes - 1;
        if (usesTables(format)) {
            if (_tableAt[size_t(i)] < 0) {
                _tableAt[size_t(i)] = int(_tables.size());
                const uint32_t *pal = p.palette(i);
                _tables.insert(_tables.end(), pal, pal + 256);
                for (int n = 0; n < 2; ++n) {
                    const NccTable &nt = p.ncc(i, n);
                    for (int k = 0; k < 16; ++k)
                        _tables.push_back(nt.y[k]);
                    for (int k = 0; k < 12; ++k)
                        _tables.push_back(uint32_t(int32_t(nt.i[k / 3][k % 3])));
                    for (int k = 0; k < 12; ++k)
                        _tables.push_back(uint32_t(int32_t(nt.q[k / 3][k % 3])));
                }
            }
            m[kTPalette] = uint32_t(_tableAt[size_t(i)]);
            m[kTNcc] = uint32_t(_tableAt[size_t(i)]) + 256 + 40 * uint32_t(t.nccTable & 1);
        }
        // 0 wrap, 1 clamp, 2 mirror (rasterize.comp's fetch).
        m[kTClampS] = t.clampS ? 1u : t.mirrorS ? 2u : 0u;
        m[kTClampT] = t.clampT ? 1u : t.mirrorT ? 2u : 0u;
        m[kTMinFilter] = uint32_t(t.minFilter);
        m[kTMagFilter] = uint32_t(t.magFilter);
        m[kTMipmapMode] = uint32_t(t.mipmapMode);
        m[kTLodBias] = floatBits(t.lodBias);
        m[kTRgbFunc] = uint32_t(t.rgb.function);
        m[kTRgbFactor] = uint32_t(t.rgb.factor);
        m[kTRgbInvert] = t.rgb.invert;
        m[kTAlphaFunc] = uint32_t(t.alpha.function);
        m[kTAlphaFactor] = uint32_t(t.alpha.factor);
        m[kTAlphaInvert] = t.alpha.invert;
        m[kTDetailBias] = uint32_t(t.detailBias);
        m[kTDetailScale] = uint32_t(t.detailScale);
        m[kTDetailMax] = floatBits(t.detailMax);
        m[kTSmallLod] = uint32_t(t.info.smallLod);
        m[kTLargeLod] = uint32_t(t.info.largeLod);
        for (int l = t.info.smallLod; l <= t.info.largeLod; ++l) {
            int w = 0;
            int h = 0;
            levelSize(l, t.info.aspect, w, h);
            m[kTBase + l] = p.levelAddress(i, l);
            m[kTSize + l] = uint32_t(w) | uint32_t(h) << 16;
        }
        m[kTNeedsDetail] = t.rgb.factor == kFactorTextureAlpha || t.alpha.factor == kFactorTextureAlpha ||
                           t.rgb.factor == kFactorOneMinusTextureAlpha || t.alpha.factor == kFactorOneMinusTextureAlpha;
    }
    if (_haveState && r == _lastRecord)
        return _lastState;
    _lastState = uint32_t(_states.size());
    _states.insert(_states.end(), r.begin(), r.end());
    _lastRecord = r;
    _haveState = true;
    return _lastState;
}

size_t GpuRenderer::addPrim(Pipeline &p, uint32_t flags, int rowFirst, int rows, uint32_t spanAt, int x0, int x1,
                            float fx0, float fy0, const Values &base, const float *dx, const float *dy,
                            bool writesAux) {
    const State &s = p.state();
    const uint32_t state = stateFor(p);
    const size_t at = _prims.size();
    _prims.resize(at + kPrimWords, 0);
    uint32_t *w = _prims.data() + at;
    w[0] = uint32_t(rowFirst);
    w[1] = uint32_t(rows);
    w[2] = spanAt;
    w[3] = state;
    w[4] = flags | (s.originLowerLeft ? kFlip : 0u);
    w[5] = floatBits(fx0);
    w[6] = floatBits(fy0);
    for (int k = 0; k < kParams; ++k) {
        w[7 + k] = floatBits(base.v[k]);
        w[22 + k] = floatBits(dx ? dx[k] : 0.0f);
        w[37 + k] = floatBits(dy ? dy[k] : 0.0f);
    }
    _boxes.push_back({x0, rowFirst, x1, rowFirst + rows - 1});
    _touched[size_t(std::max(0, p.slotOf(s.renderBuffer)))] = true;
    if (writesAux)
        _touched[3] = true;
    return at;
}

void GpuRenderer::flushIfFull() {
    if (_prims.size() / kPrimWords >= 32768 || _spans.size() >= (4u << 20))
        flush();
}

void GpuRenderer::addPixel(Pipeline &p, int x, int y, const Values &v) {
    const State &s = p.state();
    const int row = s.originLowerLeft ? _height - 1 - y : y;
    const uint32_t spanAt = uint32_t(_spans.size());
    _spans.push_back(uint32_t(x) | uint32_t(x) << 16);
    addPrim(p, 0, row, 1, spanAt, x, x, 0, 0, v, nullptr, nullptr,
            s.depthMode != kDepthDisable || blendUsesDstAlpha(s));
    flushIfFull();
}

void GpuRenderer::triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) {
    if (!ready(p)) {
        ++p.stats.trianglesIn;
        return;
    }
    const State &s = p.state();
    Triangle t;
    if (!setupTriangle(s, _width, _height, a, b, c, t, p.stats))
        return;
    // Each row's span, by buffer row (flipped for a lower-left origin).
    const bool flip = s.originLowerLeft;
    const int rows = t.y1 - t.y0;
    const int rowFirst = flip ? _height - t.y1 : t.y0;
    const uint32_t spanAt = uint32_t(_spans.size());
    int minX = INT32_MAX;
    int maxX = -1;
    int firstUsed = -1;
    int lastUsed = -1;
    for (int r = 0; r < rows; ++r) {
        const int y = flip ? _height - 1 - (rowFirst + r) : rowFirst + r;
        int xs = 0;
        int xe = 0;
        if (t.span(y, xs, xe)) {
            _spans.push_back(uint32_t(xs) | uint32_t(xe) << 16);
            minX = std::min(minX, xs);
            maxX = std::max(maxX, xe);
            if (firstUsed < 0)
                firstUsed = r;
            lastUsed = r;
        } else {
            _spans.push_back(0xffffu); // xs past xe: none
        }
    }
    if (firstUsed < 0) {
        _spans.resize(spanAt);
        return;
    }
    // Rows with no pixels at either end are left out.
    if (firstUsed > 0)
        _spans.erase(_spans.begin() + spanAt, _spans.begin() + spanAt + uint32_t(firstUsed));
    _spans.resize(spanAt + uint32_t(lastUsed - firstUsed + 1));
    addPrim(p, kDerivatives, rowFirst + firstUsed, lastUsed - firstUsed + 1, spanAt, minX, maxX, float(t.fx0),
            float(t.fy0), t.at0, t.dx, t.dy, s.depthMode != kDepthDisable || blendUsesDstAlpha(s));
    flushIfFull();
}

void GpuRenderer::line(Pipeline &p, const Vertex &a, const Vertex &b) {
    if (!ready(p))
        return;
    forLinePixels(p.state(), _width, _height, a, b, [&](int x, int y, const Values &v) { addPixel(p, x, y, v); });
}

void GpuRenderer::point(Pipeline &p, const Vertex &a) {
    if (!ready(p))
        return;
    int x = 0;
    int y = 0;
    if (!pointPixel(p.state(), _width, _height, a, x, y))
        return;
    Values v;
    load(p.state(), a, v);
    addPixel(p, x, y, v);
}

void GpuRenderer::fragments(Pipeline &p, const Fragments &f) {
    if (!ready(p))
        return;
    const State &s = p.state();
    const bool aux = s.depthMode != kDepthDisable || blendUsesDstAlpha(s);
    for (const Fragment &frag : f.list) {
        if (!f.derivatives) {
            addPixel(p, frag.x, frag.y, frag.v);
            continue;
        }
        // A one-pixel primitive whose plane is based at the pixel's centre,
        // with the primitive's gradients for the LOD.
        const int row = s.originLowerLeft ? _height - 1 - frag.y : frag.y;
        const uint32_t spanAt = uint32_t(_spans.size());
        _spans.push_back(uint32_t(frag.x) | uint32_t(frag.x) << 16);
        addPrim(p, kDerivatives, row, 1, spanAt, frag.x, frag.x, float(frag.x) + 0.5f, float(frag.y) + 0.5f, frag.v,
                f.dx, f.dy, aux);
        flushIfFull();
    }
}

void GpuRenderer::clear(Pipeline &p, uint32_t argb, int alpha, int depth) {
    if (!ready(p))
        return;
    const State &s = p.state();
    const int x0 = std::max(s.clip[0], 0);
    const int x1 = std::min(s.clip[2], _width);
    const int y0 = std::max(s.clip[1], 0);
    const int y1 = std::min(s.clip[3], _height);
    if (x0 >= x1 || y0 >= y1)
        return;
    const bool clearDepth = s.depthMode != kDepthDisable && s.depthMask;
    const bool clearAlpha = !clearDepth && s.depthMode == kDepthDisable && blendUsesDstAlpha(s) && s.alphaMask;
    uint32_t flags = kClear;
    if (s.colorMask && p.slotOf(s.renderBuffer) >= 0)
        flags |= kClearColor;
    if (clearDepth || clearAlpha)
        flags |= kClearAux;
    const int rowFirst = s.originLowerLeft ? _height - y1 : y0;
    const uint32_t spanAt = uint32_t(_spans.size());
    _spans.insert(_spans.end(), size_t(y1 - y0), uint32_t(x0) | uint32_t(x1 - 1) << 16);
    // The clear's colour and its depth (or alpha) in the first two value
    // words.
    const Values none{};
    const size_t at = addPrim(p, flags, rowFirst, y1 - y0, spanAt, x0, x1 - 1, 0, 0, none, nullptr, nullptr,
                              (flags & kClearAux) != 0);
    _prims[at + 7] = argb;
    _prims[at + 8] = uint32_t(clearDepth ? depth & 0xffff : alpha & 0xff);
    p.stats.pixelsOut += uint64_t(x1 - x0) * uint64_t(y1 - y0);
    flushIfFull();
}

void GpuRenderer::flush() {
    const uint32_t primCount = uint32_t(_prims.size() / kPrimWords);
    if (!primCount || !_frame) {
        _prims.clear();
        _spans.clear();
        _boxes.clear();
        _states.clear();
        _tables.clear();
        _haveState = false;
        _tableAt.fill(-1);
        return;
    }
    // Bin the primitives into tiles, in the order they were drawn.
    const uint32_t tilesX = uint32_t((_width + kTile - 1) / kTile);
    const uint32_t tilesY = uint32_t((_height + kTile - 1) / kTile);
    const uint32_t tiles = tilesX * tilesY;
    std::vector<uint32_t> count(tiles + 1, 0);
    for (const Box &b : _boxes)
        for (int ty = b.y0 / kTile; ty <= b.y1 / kTile; ++ty)
            for (int tx = b.x0 / kTile; tx <= b.x1 / kTile; ++tx)
                ++count[uint32_t(ty) * tilesX + uint32_t(tx)];
    std::vector<uint32_t> offsets(tiles + 1, 0);
    for (uint32_t i = 0; i < tiles; ++i)
        offsets[i + 1] = offsets[i] + count[i];
    std::vector<uint32_t> list(offsets[tiles]);
    std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1);
    for (uint32_t i = 0; i < primCount; ++i) {
        const Box &b = _boxes[i];
        for (int ty = b.y0 / kTile; ty <= b.y1 / kTile; ++ty)
            for (int tx = b.x0 / kTile; tx <= b.x1 / kTile; ++tx)
                list[fill[uint32_t(ty) * tilesX + uint32_t(tx)]++] = i;
    }
    std::vector<uint32_t> active;
    for (uint32_t i = 0; i < tiles; ++i)
        if (count[i])
            active.push_back(i);
    // The batch: states, tables, primitives, spans, tile offsets, tile
    // lists, the tiles to run.
    Params u{};
    u.width = uint32_t(_width);
    u.height = uint32_t(_height);
    u.tilesX = tilesX;
    u.activeCount = uint32_t(active.size());
    std::vector<uint32_t> &b = _scratch;
    b.clear();
    u.statesBase = uint32_t(b.size());
    b.insert(b.end(), _states.begin(), _states.end());
    u.tablesBase = uint32_t(b.size());
    b.insert(b.end(), _tables.begin(), _tables.end());
    u.primsBase = uint32_t(b.size());
    b.insert(b.end(), _prims.begin(), _prims.end());
    u.spansBase = uint32_t(b.size());
    b.insert(b.end(), _spans.begin(), _spans.end());
    u.tileOffsets = uint32_t(b.size());
    b.insert(b.end(), offsets.begin(), offsets.end());
    u.tileList = uint32_t(b.size());
    b.insert(b.end(), list.begin(), list.end());
    u.activeTiles = uint32_t(b.size());
    b.insert(b.end(), active.begin(), active.end());
    const uint32_t bytes = uint32_t(b.size() * 4);
    if (bytes > _batchCapacity) {
        if (_batchBuffer)
            _gpu->destroyBuffer(_batchBuffer);
        _batchBuffer = 0;
        _batchCapacity = std::max<uint32_t>(bytes, 1u << 20);
        _batchCapacity = std::bit_ceil(_batchCapacity);
        if (_gpu->createBuffer(_batchCapacity, _batchBuffer))
            _batchCapacity = 0;
    }
    if (_batchBuffer) {
        _gpu->upload(_batchBuffer, 0, b.data(), bytes, true);
        _gpu->dispatch(_kernel, {_batchBuffer, _tex}, {_frame}, &u, sizeof u, u.activeCount);
        _gpu->flush();
        for (int s = 0; s < Pipeline::kSlots; ++s)
            if (_touched[size_t(s)])
                _gpuNewer[size_t(s)] = true;
    }
    _touched.fill(false);
    _prims.clear();
    _spans.clear();
    _boxes.clear();
    _states.clear();
    _tables.clear();
    _haveState = false;
    _tableAt.fill(-1);
}

// --- Comparing ------------------------------------------------------------------

class ComparingRenderer : public Renderer {
public:
    ComparingRenderer(std::unique_ptr<Renderer> tested, std::function<void(const FrameComparison &)> onSwap)
        : _tested(std::move(tested)), _onSwap(std::move(onSwap)) {}

    void triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) override {
        _tested->triangle(p, a, b, c);
        if (reference(p))
            _ref->triangle(a, b, c);
    }
    void line(Pipeline &p, const Vertex &a, const Vertex &b) override {
        _tested->line(p, a, b);
        if (reference(p))
            _ref->line(a, b);
    }
    void point(Pipeline &p, const Vertex &a) override {
        _tested->point(p, a);
        if (reference(p))
            _ref->point(a);
    }
    void fragments(Pipeline &p, const Fragments &f) override {
        _tested->fragments(p, f);
        if (reference(p))
            _ref->fragments(f);
    }
    void clear(Pipeline &p, uint32_t argb, int alpha, int depth) override {
        _tested->clear(p, argb, alpha, depth);
        if (reference(p))
            _ref->clear(argb, alpha, depth);
    }
    void sync(Pipeline &p) override { _tested->sync(p); }
    void syncBuffer(Pipeline &p, int buffer) override { _tested->syncBuffer(p, buffer); }
    void hostWrote(Pipeline &p, int slot, int x, int y) override {
        _tested->hostWrote(p, slot, x, y);
        if (_ref && _opened == p.opened())
            if (const uint16_t *h = p.slotPixels(slot)) {
                const size_t at = size_t(y) * size_t(p.width()) + size_t(x);
                _ref->slotPixels(slot)[at] = h[at];
            }
    }
    void textureMemoryChanged(Pipeline &p, int tmu, uint32_t start, uint32_t bytes) override {
        _tested->textureMemoryChanged(p, tmu, start, bytes);
        if (_ref && _opened == p.opened())
            _ref->writeTmuMemory(tmu, start, p.tmuMemory(tmu) + start, bytes);
    }
    void tablesChanged(Pipeline &p, int tmu) override {
        _tested->tablesChanged(p, tmu);
        if (_ref && _opened == p.opened())
            copyTables(p, tmu);
    }
    void swapped(Pipeline &p) override {
        _tested->swapped(p);
        if (!_ref || _opened != p.opened())
            return;
        // The frame just finished: the tested renderer's, read back, against
        // the reference's (its buffers not swapped yet).
        _tested->syncBuffer(p, kBufferFront);
        const uint16_t *a = p.buffer(kBufferFront);
        const uint16_t *b = _ref->buffer(kBufferBack);
        FrameComparison f;
        f.swap = p.swaps();
        uint64_t sum = 0;
        const size_t n = size_t(p.width()) * size_t(p.height());
        for (size_t i = 0; i < n; ++i) {
            if (a[i] == b[i])
                continue;
            const Rgba x = from565(a[i]);
            const Rgba y = from565(b[i]);
            const int dr = std::abs(x.r - y.r);
            const int dg = std::abs(x.g - y.g);
            const int db = std::abs(x.b - y.b);
            const int m = std::max({dr, dg, db});
            sum += uint64_t(dr + dg + db);
            ++f.pixelsDiffering;
            f.pixelsOver24 += m > 24;
            f.maxDifference = std::max(f.maxDifference, m);
        }
        f.meanDifference = n ? double(sum) / double(n * 3) : 0;
        _ref->swap();
        if (_onSwap)
            _onSwap(f);
    }

private:
    // The reference pipeline, set up to match `p` (made when p is opened),
    // with p's state and anything the host wrote copied in: false if there
    // is none.
    bool reference(Pipeline &p) {
        if (!p.isOpen())
            return false;
        if (!_ref || _opened != p.opened()) {
            _ref = std::make_unique<Pipeline>(p.board());
            if (!_ref->open(p.width(), p.height(), p.colorBuffers(), 1)) {
                _ref.reset();
                return false;
            }
            _opened = p.opened();
            for (int i = 0; i < p.board().tmus; ++i) {
                _ref->writeTmuMemory(i, 0, p.tmuMemory(i), p.tmuMemorySize());
                copyTables(p, i);
            }
            // The same slots front and back.
            for (int i = 0; i < 3 && _ref->slotOf(kBufferFront) != p.slotOf(kBufferFront); ++i)
                _ref->swap();
            _tested->sync(p);
            for (int s = 0; s < Pipeline::kSlots; ++s)
                if (const uint16_t *h = p.slotPixels(s))
                    std::memcpy(_ref->slotPixels(s), h, size_t(p.width()) * size_t(p.height()) * 2);
        }
        _ref->state() = p.state();
        return true;
    }
    void copyTables(Pipeline &p, int tmu) {
        _ref->setPalette(tmu, 0, 255, p.palette(tmu));
        _ref->setNcc(tmu, 0, p.ncc(tmu, 0));
        _ref->setNcc(tmu, 1, p.ncc(tmu, 1));
    }

    std::unique_ptr<Renderer> _tested;
    std::function<void(const FrameComparison &)> _onSwap;
    std::unique_ptr<Pipeline> _ref;
    uint64_t _opened = 0;
};

} // namespace

std::unique_ptr<Renderer> makeGpuRenderer(std::unique_ptr<gfx::Compute> gpu, std::string &error) {
    if (!gpu) {
        error = "no GPU";
        return nullptr;
    }
    auto r = std::make_unique<GpuRenderer>(std::move(gpu));
    if (Error e = r->init()) {
        error = e.message;
        return nullptr;
    }
    return r;
}

std::unique_ptr<Renderer> makeComparingRenderer(std::unique_ptr<Renderer> tested,
                                                std::function<void(const FrameComparison &)> onSwap) {
    return std::make_unique<ComparingRenderer>(std::move(tested), std::move(onSwap));
}

} // namespace reluva::glide
