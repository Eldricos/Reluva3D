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
    sColorFunc = 0,
    sColorFactor = 1,
    sColorInvert = 2,
    sColorLocal = 3,
    sColorOther = 4,
    sAlphaFunc = 5,
    sAlphaFactor = 6,
    sAlphaInvert = 7,
    sAlphaLocal = 8,
    sAlphaOther = 9,
    sAlphaLighting = 10,
    sConstant = 11,
    sChromakey = 12,
    sChromaColor = 13,
    sAlphaTest = 14,
    sAlphaRef = 15,
    sDepthMode = 16,
    sDepthFunc = 17,
    sDepthMask = 18,
    sDepthBias = 19,
    sColorMask = 20,
    sAlphaMask = 21,
    sDither = 22,
    sFogMode = 23,
    sFogColor = 24,
    sBlendRgbSrc = 25,
    sBlendRgbDst = 26,
    sBlendAlphaSrc = 27,
    sBlendAlphaDst = 28,
    sTexture = 29,
    sTmusUsed = 30,
    sAlphaBuffer = 31,
    sBlend = 32,
    sColorSlot = 33,
    sChromaRange = 34,
    sChromaRangeMax = 35,
    sFogTable = 36,
    sChromaRangeMin = 52,
    sTmu = 64,
    sTmuWords = 48,
};
enum TmuWord : int {
    tValid = 0,
    tPerPixelLod = 1,
    tLodMin = 2,
    tLodMax = 3,
    tFormat = 4,
    tBytes = 5,
    tMemBase = 6,
    tMemMask = 7,
    tPalette = 8,
    tNcc = 9,
    tClampS = 10,
    tClampT = 11,
    tMinFilter = 12,
    tMagFilter = 13,
    tMipmapMode = 14,
    tLodBias = 15,
    tRgbFunc = 16,
    tRgbFactor = 17,
    tRgbInvert = 18,
    tAlphaFunc = 19,
    tAlphaFactor = 20,
    tAlphaInvert = 21,
    tDetailBias = 22,
    tDetailScale = 23,
    tDetailMax = 24,
    tSmallLod = 25,
    tLargeLod = 26,
    tBase = 27,
    tSize = 36,
    tNeedsDetail = 45,
};
constexpr uint32_t kStateWords = sTmu + sTmuWords * kMaxTmus;
constexpr uint32_t kPrimWords = 52;
// A TMU's tables in the batch: the palette, then NCC tables 0 and 1 (y[16],
// i[4][3], q[4][3] as words).
constexpr uint32_t kTableWords = 256 + 40 * 2;
enum PrimFlag : uint32_t { kFlip = 1, kDerivatives = 2, kClear = 4, kClearColor = 8, kClearAux = 16 };
constexpr int kTile = 16;

// The uniform block (std140: 12 words).
struct Params {
    uint32_t width, height, tilesX, activeCount;
    uint32_t statesBase, tablesBase, primsBase, spansBase;
    uint32_t tileOffsets, tileList, activeTiles, pad0;
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
    explicit GpuRenderer(std::unique_ptr<gfx::Compute> gpu) : gpu_(std::move(gpu)) {}
    ~GpuRenderer() override {
        for (gfx::ComputeBuffer b : {frame_, tex_, batchBuffer_})
            if (b)
                gpu_->destroyBuffer(b);
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
        return gpu_->createKernel(code, kernel_);
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
            tableAt_[size_t(tmu)] = -1;
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

    std::unique_ptr<gfx::Compute> gpu_;
    gfx::ComputeKernel kernel_ = 0;
    gfx::ComputeBuffer frame_ = 0, tex_ = 0, batchBuffer_ = 0;
    uint32_t batchCapacity_ = 0; // bytes
    const Pipeline *pipeline_ = nullptr;
    uint64_t opened_ = 0;
    int width_ = 0, height_ = 0, tmus_ = 0;
    uint32_t tmuBytes_ = 0;
    std::array<bool, Pipeline::kSlots> gpuNewer_{};  // drawn on the GPU since read back
    std::array<bool, Pipeline::kSlots> hostKnown_{}; // the host's copy was read back (snapshot_ is it)
    std::array<std::vector<uint16_t>, Pipeline::kSlots> snapshot_;
    bool checkHost_ = false;

    // The batch being collected.
    std::vector<uint32_t> states_, tables_, prims_, spans_;
    struct Box {
        int x0, y0, x1, y1; // inclusive
    };
    std::vector<Box> boxes_;
    std::vector<uint32_t> record_, lastRecord_;
    uint32_t lastState_ = 0;
    bool haveState_ = false;
    std::array<int, kMaxTmus> tableAt_{-1, -1, -1};
    std::array<bool, Pipeline::kSlots> touched_{};
    std::vector<uint32_t> scratch_;
};

bool GpuRenderer::ready(Pipeline &p) {
    if (!p.isOpen())
        return false;
    if (&p != pipeline_ || p.opened() != opened_ || p.width() != width_ || p.height() != height_) {
        flush();
        pipeline_ = &p;
        opened_ = p.opened();
        width_ = p.width();
        height_ = p.height();
        const uint32_t pixels = uint32_t(width_ * height_);
        if (frame_)
            gpu_->destroyBuffer(frame_);
        frame_ = 0;
        if (gpu_->createBuffer(pixels * 4u * uint32_t(Pipeline::kSlots), frame_))
            return false;
        for (int s = 0; s < Pipeline::kSlots; ++s)
            uploadSlotRows(p, s, 0, height_);
        {
            if (tex_)
                gpu_->destroyBuffer(tex_);
            tex_ = 0;
            tmus_ = p.board().tmus;
            tmuBytes_ = p.tmuMemorySize();
            if (gpu_->createBuffer(tmuBytes_ * uint32_t(tmus_), tex_))
                return false;
            for (int i = 0; i < tmus_; ++i)
                gpu_->upload(tex_, uint32_t(i) * tmuBytes_, p.tmuMemory(i), tmuBytes_);
        }
        tableAt_.fill(-1);
        checkHost_ = false;
    }
    if (checkHost_) {
        // What the host wrote since the buffers were read back: rows that
        // differ from what was read, sent over.
        checkHost_ = false;
        for (int s = 0; s < Pipeline::kSlots; ++s) {
            const uint16_t *host = p.slotPixels(s);
            if (!host || !hostKnown_[size_t(s)] || gpuNewer_[size_t(s)])
                continue;
            const uint16_t *known = snapshot_[size_t(s)].data();
            int first = -1;
            for (int y = 0; y <= height_; ++y) {
                const bool differs =
                    y < height_ && std::memcmp(host + size_t(y) * size_t(width_), known + size_t(y) * size_t(width_),
                                               size_t(width_) * 2) != 0;
                if (differs && first < 0)
                    first = y;
                else if (!differs && first >= 0) {
                    uploadSlotRows(p, s, first, y - first);
                    first = -1;
                }
            }
        }
    }
    return frame_ && tex_;
}

void GpuRenderer::uploadSlotRows(Pipeline &p, int slot, int firstRow, int rows) {
    const uint16_t *host = p.slotPixels(slot);
    const size_t at = size_t(firstRow) * size_t(width_);
    const size_t n = size_t(rows) * size_t(width_);
    snapshot_[size_t(slot)].resize(size_t(width_) * size_t(height_));
    scratch_.resize(n);
    for (size_t i = 0; i < n; ++i)
        scratch_[i] = host ? host[at + i] : 0;
    if (host)
        std::memcpy(snapshot_[size_t(slot)].data() + at, host + at, n * 2);
    const uint32_t base = uint32_t(slot) * uint32_t(width_ * height_);
    gpu_->upload(frame_, (base + uint32_t(at)) * 4u, scratch_.data(), uint32_t(n * 4));
    hostKnown_[size_t(slot)] = host != nullptr;
    gpuNewer_[size_t(slot)] = false;
}

void GpuRenderer::download(Pipeline &p, int slot) {
    uint16_t *host = p.slotPixels(slot);
    if (!host || !frame_)
        return;
    const uint32_t pixels = uint32_t(width_ * height_);
    scratch_.resize(pixels);
    if (gpu_->download(frame_, uint32_t(slot) * pixels * 4u, scratch_.data(), pixels * 4u))
        return;
    for (uint32_t i = 0; i < pixels; ++i)
        host[i] = uint16_t(scratch_[i]);
    snapshot_[size_t(slot)].assign(host, host + pixels);
    hostKnown_[size_t(slot)] = true;
    gpuNewer_[size_t(slot)] = false;
}

void GpuRenderer::sync(Pipeline &p) {
    flush();
    if (&p != pipeline_ || p.opened() != opened_)
        return;
    for (int s = 0; s < Pipeline::kSlots; ++s)
        if (gpuNewer_[size_t(s)])
            download(p, s);
    checkHost_ = true;
}

void GpuRenderer::syncBuffer(Pipeline &p, int buffer) {
    flush();
    if (&p != pipeline_ || p.opened() != opened_)
        return;
    const int s = p.slotOf(buffer);
    if (s >= 0 && gpuNewer_[size_t(s)])
        download(p, s);
    checkHost_ = true;
}

void GpuRenderer::textureMemoryChanged(Pipeline &p, int tmu, uint32_t start, uint32_t bytes) {
    if (&p != pipeline_ || !tex_ || tmu < 0 || tmu >= tmus_)
        return; // sent whole when the GPU's copy is made
    flush();    // what's waiting was drawn with the old texels
    const uint32_t from = start & ~3u;
    const uint32_t to = std::min(tmuBytes_, (start + bytes + 3) & ~3u);
    if (to > from)
        gpu_->upload(tex_, uint32_t(tmu) * tmuBytes_ + from, p.tmuMemory(tmu) + from, to - from);
}

uint32_t GpuRenderer::stateFor(Pipeline &p) {
    const State &s = p.state();
    std::vector<uint32_t> &r = record_;
    r.assign(kStateWords, 0);
    r[sColorFunc] = uint32_t(s.color.settings.function);
    r[sColorFactor] = uint32_t(s.color.settings.factor);
    r[sColorInvert] = s.color.settings.invert;
    r[sColorLocal] = uint32_t(s.color.local);
    r[sColorOther] = uint32_t(s.color.other);
    r[sAlphaFunc] = uint32_t(s.alpha.settings.function);
    r[sAlphaFactor] = uint32_t(s.alpha.settings.factor);
    r[sAlphaInvert] = s.alpha.settings.invert;
    r[sAlphaLocal] = uint32_t(s.alpha.local);
    r[sAlphaOther] = uint32_t(s.alpha.other);
    r[sAlphaLighting] = s.alphaControlsLighting;
    r[sConstant] = s.constantColor;
    r[sChromakey] = s.chromakey;
    r[sChromaColor] = s.chromakeyColor;
    r[sChromaRange] = s.chromaRange;
    r[sChromaRangeMin] = s.chromaRangeMin;
    r[sChromaRangeMax] = s.chromaRangeMax;
    r[sAlphaTest] = uint32_t(s.alphaTest);
    r[sAlphaRef] = uint32_t(s.alphaRef);
    r[sDepthMode] = uint32_t(s.depthMode);
    r[sDepthFunc] = uint32_t(s.depthFunc);
    r[sDepthMask] = s.depthMask;
    r[sDepthBias] = uint32_t(s.depthBias);
    r[sColorMask] = s.colorMask;
    r[sAlphaMask] = s.alphaMask;
    r[sDither] = uint32_t(s.dither);
    r[sFogMode] = uint32_t(s.fogMode);
    r[sFogColor] = s.fogColor;
    r[sBlendRgbSrc] = uint32_t(s.blendRgbSrc);
    r[sBlendRgbDst] = uint32_t(s.blendRgbDst);
    r[sBlendAlphaSrc] = uint32_t(s.blendAlphaSrc);
    r[sBlendAlphaDst] = uint32_t(s.blendAlphaDst);
    r[sColorSlot] = uint32_t(std::max(0, p.slotOf(s.renderBuffer)));
    for (int i = 0; i < 64; ++i)
        r[size_t(sFogTable + i / 4)] |= uint32_t(s.fogTable[size_t(i)]) << (8 * (i & 3));
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
    r[sTexture] = texture;
    r[sTmusUsed] = uint32_t(tmusUsed);
    r[sAlphaBuffer] = s.depthMode == kDepthDisable && blendUsesDstAlpha(s);
    r[sBlend] = !(s.blendRgbSrc == kBlendOne && s.blendRgbDst == kBlendZero && s.blendAlphaSrc == kBlendOne &&
                  s.blendAlphaDst == kBlendZero);
    for (int i = 0; i < tmusUsed; ++i) {
        const TmuState &t = s.tmus[size_t(i)];
        uint32_t *m = r.data() + sTmu + sTmuWords * i;
        if (!(t.hasSource && knownFormat(t.info.format)))
            continue;
        m[tValid] = 1;
        const int lodMin = 8 - t.info.largeLod;
        const int lodMax = t.mipmapMode != kMipmapDisable ? 8 - t.info.smallLod : lodMin;
        auto isTextureFactor = [](int f) {
            return f == kFactorTextureRgb || f == kFactorOneMinusLodFraction || f == kFactorTextureAlpha ||
                   f == kFactorOneMinusTextureAlpha;
        };
        m[tPerPixelLod] = lodMax > lodMin || t.minFilter != t.magFilter || t.lodBlend ||
                          isTextureFactor(t.rgb.factor) || isTextureFactor(t.alpha.factor);
        m[tLodMin] = uint32_t(lodMin);
        m[tLodMax] = uint32_t(lodMax);
        const int format = p.textureFormat(i); // P_8 through the 6666 palette reads as format 6
        m[tFormat] = uint32_t(format);
        m[tBytes] = uint32_t(texelBytes(t.info.format));
        m[tMemBase] = uint32_t(i) * tmuBytes_;
        m[tMemMask] = tmuBytes_ - 1;
        if (usesTables(format)) {
            if (tableAt_[size_t(i)] < 0) {
                tableAt_[size_t(i)] = int(tables_.size());
                const uint32_t *pal = p.palette(i);
                tables_.insert(tables_.end(), pal, pal + 256);
                for (int n = 0; n < 2; ++n) {
                    const NccTable &nt = p.ncc(i, n);
                    for (int k = 0; k < 16; ++k)
                        tables_.push_back(nt.y[k]);
                    for (int k = 0; k < 12; ++k)
                        tables_.push_back(uint32_t(int32_t(nt.i[k / 3][k % 3])));
                    for (int k = 0; k < 12; ++k)
                        tables_.push_back(uint32_t(int32_t(nt.q[k / 3][k % 3])));
                }
            }
            m[tPalette] = uint32_t(tableAt_[size_t(i)]);
            m[tNcc] = uint32_t(tableAt_[size_t(i)]) + 256 + 40 * uint32_t(t.nccTable & 1);
        }
        // 0 wrap, 1 clamp, 2 mirror (rasterize.comp's fetch).
        m[tClampS] = t.clampS ? 1u : t.mirrorS ? 2u : 0u;
        m[tClampT] = t.clampT ? 1u : t.mirrorT ? 2u : 0u;
        m[tMinFilter] = uint32_t(t.minFilter);
        m[tMagFilter] = uint32_t(t.magFilter);
        m[tMipmapMode] = uint32_t(t.mipmapMode);
        m[tLodBias] = floatBits(t.lodBias);
        m[tRgbFunc] = uint32_t(t.rgb.function);
        m[tRgbFactor] = uint32_t(t.rgb.factor);
        m[tRgbInvert] = t.rgb.invert;
        m[tAlphaFunc] = uint32_t(t.alpha.function);
        m[tAlphaFactor] = uint32_t(t.alpha.factor);
        m[tAlphaInvert] = t.alpha.invert;
        m[tDetailBias] = uint32_t(t.detailBias);
        m[tDetailScale] = uint32_t(t.detailScale);
        m[tDetailMax] = floatBits(t.detailMax);
        m[tSmallLod] = uint32_t(t.info.smallLod);
        m[tLargeLod] = uint32_t(t.info.largeLod);
        for (int l = t.info.smallLod; l <= t.info.largeLod; ++l) {
            int w = 0;
            int h = 0;
            levelSize(l, t.info.aspect, w, h);
            m[tBase + l] = p.levelAddress(i, l);
            m[tSize + l] = uint32_t(w) | uint32_t(h) << 16;
        }
        m[tNeedsDetail] = t.rgb.factor == kFactorTextureAlpha || t.alpha.factor == kFactorTextureAlpha ||
                          t.rgb.factor == kFactorOneMinusTextureAlpha || t.alpha.factor == kFactorOneMinusTextureAlpha;
    }
    if (haveState_ && r == lastRecord_)
        return lastState_;
    lastState_ = uint32_t(states_.size());
    states_.insert(states_.end(), r.begin(), r.end());
    lastRecord_ = r;
    haveState_ = true;
    return lastState_;
}

size_t GpuRenderer::addPrim(Pipeline &p, uint32_t flags, int rowFirst, int rows, uint32_t spanAt, int x0, int x1,
                            float fx0, float fy0, const Values &base, const float *dx, const float *dy,
                            bool writesAux) {
    const State &s = p.state();
    const uint32_t state = stateFor(p);
    const size_t at = prims_.size();
    prims_.resize(at + kPrimWords, 0);
    uint32_t *w = prims_.data() + at;
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
    boxes_.push_back({x0, rowFirst, x1, rowFirst + rows - 1});
    touched_[size_t(std::max(0, p.slotOf(s.renderBuffer)))] = true;
    if (writesAux)
        touched_[3] = true;
    return at;
}

void GpuRenderer::flushIfFull() {
    if (prims_.size() / kPrimWords >= 32768 || spans_.size() >= (4u << 20))
        flush();
}

void GpuRenderer::addPixel(Pipeline &p, int x, int y, const Values &v) {
    const State &s = p.state();
    const int row = s.originLowerLeft ? height_ - 1 - y : y;
    const uint32_t spanAt = uint32_t(spans_.size());
    spans_.push_back(uint32_t(x) | uint32_t(x) << 16);
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
    if (!setupTriangle(s, width_, height_, a, b, c, t, p.stats))
        return;
    // Each row's span, by buffer row (flipped for a lower-left origin).
    const bool flip = s.originLowerLeft;
    const int rows = t.y1 - t.y0;
    const int rowFirst = flip ? height_ - t.y1 : t.y0;
    const uint32_t spanAt = uint32_t(spans_.size());
    int minX = INT32_MAX;
    int maxX = -1;
    int firstUsed = -1;
    int lastUsed = -1;
    for (int r = 0; r < rows; ++r) {
        const int y = flip ? height_ - 1 - (rowFirst + r) : rowFirst + r;
        int xs = 0;
        int xe = 0;
        if (t.span(y, xs, xe)) {
            spans_.push_back(uint32_t(xs) | uint32_t(xe) << 16);
            minX = std::min(minX, xs);
            maxX = std::max(maxX, xe);
            if (firstUsed < 0)
                firstUsed = r;
            lastUsed = r;
        } else {
            spans_.push_back(0xffffu); // xs past xe: none
        }
    }
    if (firstUsed < 0) {
        spans_.resize(spanAt);
        return;
    }
    // Rows with no pixels at either end are left out.
    if (firstUsed > 0)
        spans_.erase(spans_.begin() + spanAt, spans_.begin() + spanAt + uint32_t(firstUsed));
    spans_.resize(spanAt + uint32_t(lastUsed - firstUsed + 1));
    addPrim(p, kDerivatives, rowFirst + firstUsed, lastUsed - firstUsed + 1, spanAt, minX, maxX, float(t.fx0),
            float(t.fy0), t.at0, t.dx, t.dy, s.depthMode != kDepthDisable || blendUsesDstAlpha(s));
    flushIfFull();
}

void GpuRenderer::line(Pipeline &p, const Vertex &a, const Vertex &b) {
    if (!ready(p))
        return;
    forLinePixels(p.state(), width_, height_, a, b, [&](int x, int y, const Values &v) { addPixel(p, x, y, v); });
}

void GpuRenderer::point(Pipeline &p, const Vertex &a) {
    if (!ready(p))
        return;
    int x = 0;
    int y = 0;
    if (!pointPixel(p.state(), width_, height_, a, x, y))
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
        const int row = s.originLowerLeft ? height_ - 1 - frag.y : frag.y;
        const uint32_t spanAt = uint32_t(spans_.size());
        spans_.push_back(uint32_t(frag.x) | uint32_t(frag.x) << 16);
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
    const int x1 = std::min(s.clip[2], width_);
    const int y0 = std::max(s.clip[1], 0);
    const int y1 = std::min(s.clip[3], height_);
    if (x0 >= x1 || y0 >= y1)
        return;
    const bool clearDepth = s.depthMode != kDepthDisable && s.depthMask;
    const bool clearAlpha = !clearDepth && s.depthMode == kDepthDisable && blendUsesDstAlpha(s) && s.alphaMask;
    uint32_t flags = kClear;
    if (s.colorMask && p.slotOf(s.renderBuffer) >= 0)
        flags |= kClearColor;
    if (clearDepth || clearAlpha)
        flags |= kClearAux;
    const int rowFirst = s.originLowerLeft ? height_ - y1 : y0;
    const uint32_t spanAt = uint32_t(spans_.size());
    spans_.insert(spans_.end(), size_t(y1 - y0), uint32_t(x0) | uint32_t(x1 - 1) << 16);
    // The clear's colour and its depth (or alpha) in the first two value
    // words.
    const Values none{};
    const size_t at = addPrim(p, flags, rowFirst, y1 - y0, spanAt, x0, x1 - 1, 0, 0, none, nullptr, nullptr,
                              (flags & kClearAux) != 0);
    prims_[at + 7] = argb;
    prims_[at + 8] = uint32_t(clearDepth ? depth & 0xffff : alpha & 0xff);
    p.stats.pixelsOut += uint64_t(x1 - x0) * uint64_t(y1 - y0);
    flushIfFull();
}

void GpuRenderer::flush() {
    const uint32_t primCount = uint32_t(prims_.size() / kPrimWords);
    if (!primCount || !frame_) {
        prims_.clear();
        spans_.clear();
        boxes_.clear();
        states_.clear();
        tables_.clear();
        haveState_ = false;
        tableAt_.fill(-1);
        return;
    }
    // Bin the primitives into tiles, in the order they were drawn.
    const uint32_t tilesX = uint32_t((width_ + kTile - 1) / kTile);
    const uint32_t tilesY = uint32_t((height_ + kTile - 1) / kTile);
    const uint32_t tiles = tilesX * tilesY;
    std::vector<uint32_t> count(tiles + 1, 0);
    for (const Box &b : boxes_)
        for (int ty = b.y0 / kTile; ty <= b.y1 / kTile; ++ty)
            for (int tx = b.x0 / kTile; tx <= b.x1 / kTile; ++tx)
                ++count[uint32_t(ty) * tilesX + uint32_t(tx)];
    std::vector<uint32_t> offsets(tiles + 1, 0);
    for (uint32_t i = 0; i < tiles; ++i)
        offsets[i + 1] = offsets[i] + count[i];
    std::vector<uint32_t> list(offsets[tiles]);
    std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1);
    for (uint32_t i = 0; i < primCount; ++i) {
        const Box &b = boxes_[i];
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
    u.width = uint32_t(width_);
    u.height = uint32_t(height_);
    u.tilesX = tilesX;
    u.activeCount = uint32_t(active.size());
    std::vector<uint32_t> &b = scratch_;
    b.clear();
    u.statesBase = uint32_t(b.size());
    b.insert(b.end(), states_.begin(), states_.end());
    u.tablesBase = uint32_t(b.size());
    b.insert(b.end(), tables_.begin(), tables_.end());
    u.primsBase = uint32_t(b.size());
    b.insert(b.end(), prims_.begin(), prims_.end());
    u.spansBase = uint32_t(b.size());
    b.insert(b.end(), spans_.begin(), spans_.end());
    u.tileOffsets = uint32_t(b.size());
    b.insert(b.end(), offsets.begin(), offsets.end());
    u.tileList = uint32_t(b.size());
    b.insert(b.end(), list.begin(), list.end());
    u.activeTiles = uint32_t(b.size());
    b.insert(b.end(), active.begin(), active.end());
    const uint32_t bytes = uint32_t(b.size() * 4);
    if (bytes > batchCapacity_) {
        if (batchBuffer_)
            gpu_->destroyBuffer(batchBuffer_);
        batchBuffer_ = 0;
        batchCapacity_ = std::max<uint32_t>(bytes, 1u << 20);
        batchCapacity_ = std::bit_ceil(batchCapacity_);
        if (gpu_->createBuffer(batchCapacity_, batchBuffer_))
            batchCapacity_ = 0;
    }
    if (batchBuffer_) {
        gpu_->upload(batchBuffer_, 0, b.data(), bytes, true);
        gpu_->dispatch(kernel_, {batchBuffer_, tex_}, {frame_}, &u, sizeof u, u.activeCount);
        gpu_->flush();
        for (int s = 0; s < Pipeline::kSlots; ++s)
            if (touched_[size_t(s)])
                gpuNewer_[size_t(s)] = true;
    }
    touched_.fill(false);
    prims_.clear();
    spans_.clear();
    boxes_.clear();
    states_.clear();
    tables_.clear();
    haveState_ = false;
    tableAt_.fill(-1);
}

// --- Comparing ------------------------------------------------------------------

class ComparingRenderer : public Renderer {
public:
    ComparingRenderer(std::unique_ptr<Renderer> tested, std::function<void(const FrameComparison &)> onSwap)
        : tested_(std::move(tested)), onSwap_(std::move(onSwap)) {}

    void triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) override {
        tested_->triangle(p, a, b, c);
        if (reference(p))
            ref_->triangle(a, b, c);
    }
    void line(Pipeline &p, const Vertex &a, const Vertex &b) override {
        tested_->line(p, a, b);
        if (reference(p))
            ref_->line(a, b);
    }
    void point(Pipeline &p, const Vertex &a) override {
        tested_->point(p, a);
        if (reference(p))
            ref_->point(a);
    }
    void fragments(Pipeline &p, const Fragments &f) override {
        tested_->fragments(p, f);
        if (reference(p))
            ref_->fragments(f);
    }
    void clear(Pipeline &p, uint32_t argb, int alpha, int depth) override {
        tested_->clear(p, argb, alpha, depth);
        if (reference(p))
            ref_->clear(argb, alpha, depth);
    }
    void sync(Pipeline &p) override { tested_->sync(p); }
    void syncBuffer(Pipeline &p, int buffer) override { tested_->syncBuffer(p, buffer); }
    void hostWrote(Pipeline &p, int slot, int x, int y) override {
        tested_->hostWrote(p, slot, x, y);
        if (ref_ && opened_ == p.opened())
            if (const uint16_t *h = p.slotPixels(slot)) {
                const size_t at = size_t(y) * size_t(p.width()) + size_t(x);
                ref_->slotPixels(slot)[at] = h[at];
            }
    }
    void textureMemoryChanged(Pipeline &p, int tmu, uint32_t start, uint32_t bytes) override {
        tested_->textureMemoryChanged(p, tmu, start, bytes);
        if (ref_ && opened_ == p.opened())
            ref_->writeTmuMemory(tmu, start, p.tmuMemory(tmu) + start, bytes);
    }
    void tablesChanged(Pipeline &p, int tmu) override {
        tested_->tablesChanged(p, tmu);
        if (ref_ && opened_ == p.opened())
            copyTables(p, tmu);
    }
    void swapped(Pipeline &p) override {
        tested_->swapped(p);
        if (!ref_ || opened_ != p.opened())
            return;
        // The frame just finished: the tested renderer's, read back, against
        // the reference's (its buffers not swapped yet).
        tested_->syncBuffer(p, kBufferFront);
        const uint16_t *a = p.buffer(kBufferFront);
        const uint16_t *b = ref_->buffer(kBufferBack);
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
        ref_->swap();
        if (onSwap_)
            onSwap_(f);
    }

private:
    // The reference pipeline, set up to match `p` (made when p is opened),
    // with p's state and anything the host wrote copied in: false if there
    // is none.
    bool reference(Pipeline &p) {
        if (!p.isOpen())
            return false;
        if (!ref_ || opened_ != p.opened()) {
            ref_ = std::make_unique<Pipeline>(p.board());
            if (!ref_->open(p.width(), p.height(), p.colorBuffers(), 1)) {
                ref_.reset();
                return false;
            }
            opened_ = p.opened();
            for (int i = 0; i < p.board().tmus; ++i) {
                ref_->writeTmuMemory(i, 0, p.tmuMemory(i), p.tmuMemorySize());
                copyTables(p, i);
            }
            // The same slots front and back.
            for (int i = 0; i < 3 && ref_->slotOf(kBufferFront) != p.slotOf(kBufferFront); ++i)
                ref_->swap();
            tested_->sync(p);
            for (int s = 0; s < Pipeline::kSlots; ++s)
                if (const uint16_t *h = p.slotPixels(s))
                    std::memcpy(ref_->slotPixels(s), h, size_t(p.width()) * size_t(p.height()) * 2);
        }
        ref_->state() = p.state();
        return true;
    }
    void copyTables(Pipeline &p, int tmu) {
        ref_->setPalette(tmu, 0, 255, p.palette(tmu));
        ref_->setNcc(tmu, 0, p.ncc(tmu, 0));
        ref_->setNcc(tmu, 1, p.ncc(tmu, 1));
    }

    std::unique_ptr<Renderer> tested_;
    std::function<void(const FrameComparison &)> onSwap_;
    std::unique_ptr<Pipeline> ref_;
    uint64_t opened_ = 0;
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
