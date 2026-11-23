// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/directx/gpu_raster.h"

#include "emulation/x86/directx/internal.h"
#include "emulation/x86/directx/shaders/dx_kernels.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::x86::dx {

namespace {

// A state record's words (shaders/rasterize.comp has the same).
enum StateWord : uint32_t {
    kSZTest = 0,
    kSZWrite = 1,
    kSZFunc = 2,
    kSZMax = 3,
    kSZMask = 4,
    kSAlphaTest = 5,
    kSAlphaFunc = 6,
    kSAlphaRef = 7,
    kSBlend = 8,
    kSSrcBlend = 9,
    kSDestBlend = 10,
    kSFog = 11,
    kSFogTableMode = 12,
    kSFogStart = 13,
    kSFogEnd = 14,
    kSFogDensity = 15,
    kSFogByZ = 16,
    kSFogColor = 17,
    kSColorKey = 21,
    kSSpecular = 22,
    kSFlat = 23,
    kSFactor = 24,
    kSStages = 28,
    kSTarget = 29,
    kSBound = 30,
    kSStage = 32,
    kStageWords = 9,
    kStateWords = kSStage + 8 * kStageWords,
};
// A triangle's words.
enum PrimWord : uint32_t {
    kPState = 0,
    kPY0 = 1,
    kPRows = 2,
    kPSpans = 3,
    kPSampler = 4,
    kSamplerWords = 5,
    kPFlat = 44,
    kPPlanes = 52,
    kValues = 26, // z, 1/w, 8 colours, two coordinates for each of 8 stages
    kPrimWords = kPPlanes + 3 * kValues,
};
constexpr int kAttributes = int(kValues) - 2; // diffuse, specular, a u and v per stage
constexpr uint32_t kTarget565 = 1;
constexpr uint32_t kTarget888 = 2;
constexpr int kTile = 16;

uint32_t bitsOf(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The kernel's uniform block (std140: words in fours).
struct Params {
    uint32_t width;
    uint32_t height;
    uint32_t tilesX;
    uint32_t activeCount;
    uint32_t depthBase;
    uint32_t depthWidth;
    uint32_t depthHeight;
    uint32_t pad0;
    uint32_t tileOffsets;
    uint32_t tileList;
    uint32_t activeTiles;
    uint32_t pad1;
};

} // namespace

std::unique_ptr<GpuRaster> GpuRaster::create(std::string &error) {
    std::unique_ptr<gfx::Compute> gpu = gfx::createCompute(error);
    if (!gpu)
        return nullptr;
    std::unique_ptr<GpuRaster> r(new GpuRaster(std::move(gpu)));
    gfx::ComputeKernelCode code;
    code.spirv = shaders::kRasterizeSpirv;
    code.spirvSize = sizeof shaders::kRasterizeSpirv;
    code.msl = shaders::kRasterizeMsl;
    code.readOnlyBuffers = 2;
    code.readWriteBuffers = 1;
    code.uniformBuffers = 1;
    code.threadsX = kTile;
    code.threadsY = kTile;
    if (Error e = r->_gpu->createKernel(code, r->_kernel)) {
        error = e.message;
        return nullptr;
    }
    return r;
}

bool GpuRaster::canDraw(const Draw &d) {
    using Target = Draw::PixelConfig::Target;
    if (!d.target || (d.cfg.target != Target::kRgb565 && d.cfg.target != Target::kRgb888))
        return false;
    return !d.cfg.zTest || d.cfg.zBytes == 2 || d.cfg.zBytes == 4;
}

Error GpuRaster::ensureBuffer(gfx::ComputeBuffer &buffer, uint32_t &capacity, uint32_t bytes) {
    if (buffer && bytes <= capacity)
        return Error::success();
    if (buffer)
        _gpu->destroyBuffer(buffer);
    buffer = 0;
    capacity = std::max<uint32_t>(bytes, 4096);
    capacity += capacity / 2;
    return _gpu->createBuffer(capacity, buffer);
}

const GpuRaster::TextureSlot &GpuRaster::textureSlot(const DecodedTexture &t) {
    TextureSlot &slot = _textures[&t];
    if (slot.versions == t.versions && slot.levels.size() == t.levels.size())
        return slot;
    // New, or decoded again: its levels added at the end (flush starts
    // texture memory again once it's large).
    slot.versions = t.versions;
    slot.levels.clear();
    for (const DecodedTexture::Level &l : t.levels) {
        const uint32_t base = uint32_t(_texWords.size());
        _texWords.insert(_texWords.end(), l.argb.begin(), l.argb.end());
        const uint32_t keyBase = uint32_t(_texWords.size());
        _texWords.resize(_texWords.size() + (l.argb.size() + 31) / 32, 0);
        for (size_t i = 0; i < l.keyed.size(); ++i)
            if (l.keyed[i])
                _texWords[keyBase + i / 32] |= 1u << (i % 32);
        slot.levels.push_back({base, keyBase, uint32_t(l.width), uint32_t(l.height)});
    }
    return slot;
}

uint32_t GpuRaster::stateRecord(const Draw &d) {
    const Draw::PixelConfig &c = d.cfg;
    std::vector<uint32_t> w(kStateWords, 0);
    w[kSZTest] = c.zTest;
    w[kSZWrite] = c.zWrite;
    w[kSZFunc] = c.zFunc;
    w[kSZMax] = bitsOf(float(c.zMax));
    w[kSZMask] = c.zMask;
    w[kSAlphaTest] = c.alphaTest;
    w[kSAlphaFunc] = c.alphaFunc;
    w[kSAlphaRef] = bitsOf(c.alphaRef);
    w[kSBlend] = c.blend;
    w[kSSrcBlend] = c.srcBlend;
    w[kSDestBlend] = c.destBlend;
    w[kSFog] = c.fog;
    w[kSFogTableMode] = c.fogTableMode;
    w[kSFogStart] = bitsOf(c.fogStart);
    w[kSFogEnd] = bitsOf(c.fogEnd);
    w[kSFogDensity] = bitsOf(c.fogDensity);
    w[kSFogByZ] = c.fogByZ;
    const float fog[4] = {c.fogColor.r, c.fogColor.g, c.fogColor.b, c.fogColor.a};
    const float factor[4] = {c.factor.r, c.factor.g, c.factor.b, c.factor.a};
    for (int i = 0; i < 4; ++i) {
        w[kSFogColor + uint32_t(i)] = bitsOf(fog[i]);
        w[kSFactor + uint32_t(i)] = bitsOf(factor[i]);
    }
    w[kSColorKey] = c.colorKey;
    w[kSSpecular] = c.specular;
    w[kSFlat] = c.flat;
    w[kSStages] = uint32_t(d.stages);
    w[kSTarget] = c.target == Draw::PixelConfig::Target::kRgb565 ? kTarget565 : kTarget888;
    for (int st = 0; st < d.stages; ++st) {
        if (d.bound[st])
            w[kSBound] |= 1u << st;
        const Draw::StageConfig &sc = c.stage[st];
        const uint32_t words[kStageWords] = {sc.colorOp,   sc.colorArg1, sc.colorArg2, sc.alphaOp, sc.alphaArg1,
                                             sc.alphaArg2, sc.addressU,  sc.addressV,  sc.border};
        std::copy(std::begin(words), std::end(words), w.begin() + kSStage + st * kStageWords);
    }
    if (!_states.empty() && w == _lastState)
        return _lastStateAt;
    _lastStateAt = uint32_t(_states.size());
    _states.insert(_states.end(), w.begin(), w.end());
    _lastState = std::move(w);
    return _lastStateAt;
}

void GpuRaster::add(const Draw &d, const std::vector<std::array<ClipVertex, 3>> &triangles) {
    if (triangles.empty())
        return;
    if (pending() && (d.target != _target || (d.cfg.zTest ? d.depth : nullptr) != _depth))
        flush();
    if (!pending()) {
        _target = d.target;
        _depth = d.cfg.zTest ? d.depth : nullptr;
        _surfaces = d.cfg;
    }
    const uint32_t state = stateRecord(d);
    for (const auto &t : triangles)
        addTriangle(d, state, t[0], t[1], t[2]);
}

// As Draw::rasterize sets a triangle up: its bounds, its edges (in
// doubles), the span of pixels inside on each row, its values' planes, and
// each stage's mip level.
void GpuRaster::addTriangle(const Draw &d, uint32_t state, const ClipVertex &a, const ClipVertex &b,
                            const ClipVertex &c) {
    const Viewport &vp = d.viewport;
    const int left = std::max(vp.x, 0);
    const int top = std::max(vp.y, 0);
    const int right = std::min(vp.x + vp.width, d.target->d.width);
    const int bottom = std::min(vp.y + vp.height, d.target->d.height);
    const int x0 = std::max(left, int(std::ceil(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(right - 1, int(std::floor(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(top, int(std::ceil(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(bottom - 1, int(std::floor(std::max({a.y, b.y, c.y}))));
    if (x0 > x1 || y0 > y1)
        return;
    double area = double(b.x - a.x) * (c.y - a.y) - double(c.x - a.x) * (b.y - a.y);
    if (area == 0)
        return;
    const ClipVertex *v[3] = {&a, &b, &c};
    const double sign = area > 0 ? 1 : -1;
    area *= sign;
    struct Edge {
        double a;
        double b;
        double c;
        bool topLeft;
    } e[3];
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &p = *v[(i + 1) % 3];
        const ClipVertex &q = *v[(i + 2) % 3];
        e[i].a = sign * (p.y - q.y);
        e[i].b = sign * (q.x - p.x);
        e[i].c = sign * (double(p.x) * q.y - double(q.x) * p.y);
        e[i].topLeft = e[i].a > 0 || (e[i].a == 0 && e[i].b > 0);
    }
    // Each row's pixels inside, as the software one finds them.
    const uint32_t spansAt = uint32_t(_spans.size());
    bool any = false;
    for (int y = y0; y <= y1; ++y) {
        double from = x0;
        double to = x1;
        bool empty = false;
        for (const Edge &ed : e) {
            const double rest = ed.b * y + ed.c;
            if (ed.a > 0)
                from = std::max(from, std::floor(-rest / ed.a) - 1);
            else if (ed.a < 0)
                to = std::min(to, std::ceil(-rest / ed.a) + 1);
            else if (rest < 0 || (rest == 0 && !ed.topLeft))
                empty = true;
        }
        uint32_t span = 1; // none: from 1 to 0
        if (!empty && from <= to) {
            auto inside = [&](int x) {
                for (const Edge &ed : e) {
                    const double w = ed.a * x + ed.b * y + ed.c;
                    if (!(w > 0 || (w == 0 && ed.topLeft)))
                        return false;
                }
                return true;
            };
            int first = int(from);
            int last = int(to);
            while (first <= last && !inside(first))
                ++first;
            while (last >= first && !inside(last))
                --last;
            if (first <= last) {
                span = uint32_t(first) | uint32_t(last) << 16;
                any = true;
            }
        }
        _spans.push_back(span);
    }
    if (!any) {
        _spans.resize(spansAt);
        return;
    }
    std::vector<uint32_t> w(kPrimWords, 0);
    w[kPState] = state;
    w[kPY0] = uint32_t(y0);
    w[kPRows] = uint32_t(y1 - y0 + 1);
    w[kPSpans] = spansAt;
    // Attributes divided by w, and each stage's mip level.
    const int stages = d.stages;
    const int count = 8 + 2 * stages;
    float attr[3][kAttributes];
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &p = *v[i];
        const float rhw = p.w;
        const float plain[8] = {p.diffuse.r,  p.diffuse.g,  p.diffuse.b,  p.diffuse.a,
                                p.specular.r, p.specular.g, p.specular.b, p.specular.a};
        for (int k = 0; k < 8; ++k)
            attr[i][k] = plain[k] * rhw;
        for (int st = 0; st < stages; ++st)
            attr[i][8 + 2 * st] = p.tex[st][0] * rhw, attr[i][9 + 2 * st] = p.tex[st][1] * rhw;
    }
    for (int st = 0; st < stages; ++st) {
        const DecodedTexture *t = d.bound[st];
        if (!t || t->levels.empty())
            continue; // width 0: no texture
        float lod = 0;
        const double du1 = (b.tex[st][0] - a.tex[st][0]) * t->levels[0].width;
        const double dv1 = (b.tex[st][1] - a.tex[st][1]) * t->levels[0].height;
        const double du2 = (c.tex[st][0] - a.tex[st][0]) * t->levels[0].width;
        const double dv2 = (c.tex[st][1] - a.tex[st][1]) * t->levels[0].height;
        const double texels = std::fabs(du1 * dv2 - du2 * dv1);
        if (texels > 0)
            lod = float(0.5 * std::log2(texels / area));
        const Draw::StageConfig &sc = d.cfg.stage[st];
        size_t level = 0;
        if (sc.mipFilter >= 2 && lod > 0)
            level = std::min(t->levels.size() - 1, size_t(sc.mipFilter == 3 ? std::lround(lod) : int(lod)));
        const TextureSlot &slot = textureSlot(*t);
        const auto &l = slot.levels[level];
        const uint32_t at = kPSampler + uint32_t(st) * kSamplerWords;
        w[at] = l[0];
        w[at + 1] = l[1];
        w[at + 2] = l[2];
        w[at + 3] = l[3];
        w[at + 4] = (lod > 0 ? sc.minFilter : sc.magFilter) >= 2;
    }
    for (int k = 0; k < 8; ++k)
        w[kPFlat + uint32_t(k)] = bitsOf(attr[0][k] / a.w); // the first vertex's colours
    // z, 1/w and the attributes as planes: f = fx x + fy y + f0.
    float values[3][2 + kAttributes];
    for (int i = 0; i < 3; ++i) {
        values[i][0] = v[i]->z;
        values[i][1] = v[i]->w;
        for (int k = 0; k < count; ++k)
            values[i][2 + k] = attr[i][k];
    }
    for (int k = 0; k < 2 + count; ++k) {
        const double fx = (e[0].a * values[0][k] + e[1].a * values[1][k] + e[2].a * values[2][k]) / area;
        const double fy = (e[0].b * values[0][k] + e[1].b * values[1][k] + e[2].b * values[2][k]) / area;
        const double f0 = (e[0].c * values[0][k] + e[1].c * values[1][k] + e[2].c * values[2][k]) / area;
        w[kPPlanes + uint32_t(k)] = bitsOf(float(fx));
        w[kPPlanes + kValues + uint32_t(k)] = bitsOf(float(fy));
        w[kPPlanes + 2 * kValues + uint32_t(k)] = bitsOf(float(f0));
    }
    _prims.insert(_prims.end(), w.begin(), w.end());
    _bounds.push_back({x0, y0, x1, y1});
}

Error GpuRaster::flush() {
    if (!pending())
        return Error::success();
    const Draw::PixelConfig &s = _surfaces;
    const uint32_t width = uint32_t(s.targetWidth);
    const uint32_t height = uint32_t(s.targetHeight);
    const uint32_t tilesX = (width + kTile - 1) / kTile;
    const uint32_t tilesY = (height + kTile - 1) / kTile;
    // The tiles' lists, each in drawing order.
    const size_t prims = _bounds.size();
    std::vector<uint32_t> counts(size_t(tilesX) * tilesY + 1, 0);
    for (const auto &b : _bounds)
        for (int ty = b[1] / kTile; ty <= b[3] / kTile; ++ty)
            for (int tx = b[0] / kTile; tx <= b[2] / kTile; ++tx)
                ++counts[size_t(ty) * tilesX + size_t(tx)];
    // The batch: states, triangles, spans, tile offsets, tile lists, active tiles.
    const uint32_t primsBase = uint32_t(_states.size());
    const uint32_t spansBase = primsBase + uint32_t(_prims.size());
    const uint32_t offsetsBase = spansBase + uint32_t(_spans.size());
    const uint32_t tiles = tilesX * tilesY;
    std::vector<uint32_t> offsets(size_t(tiles) + 1, 0);
    for (uint32_t t = 0; t < tiles; ++t)
        offsets[t + 1] = offsets[t] + counts[t];
    const uint32_t listBase = offsetsBase + tiles + 1;
    const uint32_t activeBase = listBase + offsets[tiles];
    std::vector<uint32_t> active;
    for (uint32_t t = 0; t < tiles; ++t)
        if (counts[t])
            active.push_back(t);
    std::vector<uint32_t> batch;
    batch.reserve(size_t(activeBase) + active.size());
    batch.insert(batch.end(), _states.begin(), _states.end());
    batch.insert(batch.end(), _prims.begin(), _prims.end());
    for (size_t p = 0; p < prims; ++p) // the spans' offsets, in the batch
        batch[primsBase + p * kPrimWords + kPSpans] += spansBase;
    batch.insert(batch.end(), _spans.begin(), _spans.end());
    batch.insert(batch.end(), offsets.begin(), offsets.end());
    std::vector<uint32_t> list(offsets[tiles]);
    std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1);
    for (size_t p = 0; p < prims; ++p) {
        const auto &b = _bounds[p];
        for (int ty = b[1] / kTile; ty <= b[3] / kTile; ++ty)
            for (int tx = b[0] / kTile; tx <= b[2] / kTile; ++tx)
                list[fill[size_t(ty) * tilesX + size_t(tx)]++] = primsBase + uint32_t(p) * kPrimWords;
    }
    batch.insert(batch.end(), list.begin(), list.end());
    batch.insert(batch.end(), active.begin(), active.end());

    // The target's and depth buffer's pixels, a word each.
    const bool depth = _depth != nullptr;
    const uint32_t depthWidth = depth ? uint32_t(s.depthWidth) : 0;
    const uint32_t depthHeight = depth ? uint32_t(s.depthHeight) : 0;
    std::vector<uint32_t> frame(size_t(width) * height + size_t(depthWidth) * depthHeight);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t raw = 0;
            std::memcpy(&raw, s.targetAt + y * uint32_t(s.targetPitch) + x * s.targetBytes, s.targetBytes);
            frame[size_t(y) * width + x] = raw;
        }
    const size_t depthBase = size_t(width) * height;
    for (uint32_t y = 0; y < depthHeight; ++y)
        for (uint32_t x = 0; x < depthWidth; ++x) {
            uint32_t raw = 0;
            std::memcpy(&raw, s.depthAt + y * uint32_t(s.depthPitch) + x * s.zBytes, s.zBytes);
            frame[depthBase + size_t(y) * depthWidth + x] = raw;
        }

    // Texture memory, at least a word (all of it again in a new buffer); then the run.
    if (_texWords.empty())
        _texWords.push_back(0);
    const gfx::ComputeBuffer oldTex = _texBuffer;
    Error e = ensureBuffer(_texBuffer, _texCapacity, uint32_t(_texWords.size() * 4));
    if (_texBuffer != oldTex)
        _texUploaded = 0;
    if (!e && _texCapacity < _texWords.size() * 4)
        e = Error::fail("texture memory too large");
    if (e) {
        _prims.clear();
        return e;
    }
    if (_texUploaded > _texWords.size())
        _texUploaded = 0;
    if (_texUploaded < _texWords.size()) {
        _gpu->upload(_texBuffer, _texUploaded * 4, _texWords.data() + _texUploaded,
                     uint32_t(_texWords.size() - _texUploaded) * 4);
        _texUploaded = uint32_t(_texWords.size());
    }
    if (Error be = ensureBuffer(_batchBuffer, _batchCapacity, uint32_t(batch.size() * 4)))
        return be;
    if (Error fe = ensureBuffer(_frameBuffer, _frameCapacity, uint32_t(frame.size() * 4)))
        return fe;
    _gpu->upload(_batchBuffer, 0, batch.data(), uint32_t(batch.size() * 4), true);
    _gpu->upload(_frameBuffer, 0, frame.data(), uint32_t(frame.size() * 4), true);
    Params p{};
    p.width = width, p.height = height, p.tilesX = tilesX, p.activeCount = uint32_t(active.size());
    p.depthBase = uint32_t(depthBase), p.depthWidth = depthWidth, p.depthHeight = depthHeight;
    p.tileOffsets = offsetsBase, p.tileList = listBase, p.activeTiles = activeBase;
    if (!active.empty())
        _gpu->dispatch(_kernel, {_batchBuffer, _texBuffer}, {_frameBuffer}, &p, sizeof p, uint32_t(active.size()));
    Error de = _gpu->download(_frameBuffer, 0, frame.data(), uint32_t(frame.size() * 4));
    _states.clear();
    _lastState.clear();
    _prims.clear();
    _spans.clear();
    _bounds.clear();
    if (de)
        return de;
    // Back into the surfaces' memory.
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            std::memcpy(s.targetAt + y * uint32_t(s.targetPitch) + x * s.targetBytes, &frame[size_t(y) * width + x],
                        s.targetBytes);
    for (uint32_t y = 0; y < depthHeight; ++y)
        for (uint32_t x = 0; x < depthWidth; ++x)
            std::memcpy(s.depthAt + y * uint32_t(s.depthPitch) + x * s.zBytes,
                        &frame[depthBase + size_t(y) * depthWidth + x], s.zBytes);
    _target.reset();
    _depth.reset();
    // Texture memory starts again once it's large: what's in use is added
    // again as it's used.
    constexpr size_t kMostWords = size_t(64) << 20;
    if (_texWords.size() > kMostWords) {
        _texWords.clear();
        _texUploaded = 0;
        _textures.clear();
    }
    return Error::success();
}

} // namespace reluva::x86::dx

namespace reluva::x86 {

dx::GpuRaster *DirectX::State::gpuRaster() {
    if (!gpuTried) {
        gpuTried = true;
        std::string error;
        gpu = dx::GpuRaster::create(error);
    }
    return gpu.get();
}

} // namespace reluva::x86
