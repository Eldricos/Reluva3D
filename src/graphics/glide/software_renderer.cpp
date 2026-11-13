// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The software reference rasterizer: Glide's triangles, lines and points
// drawn pixel by pixel through the Voodoo's pipeline, in the order the
// Voodoo2 specification draws it (its section 3 and the fbzMode, fogMode
// and alphaMode figures): clip window, texture lookup and the TMUs'
// combine, chroma-key, the colour and alpha combine, alpha test, depth
// test, fog, alpha blending, the dither to 5:6:5 and the writes.
// docs/common/graphics/glide.md, The software rasterizer, says what follows the
// documents and what is Reluva's own choice.

#include "graphics/glide/pipeline.h"
#include "graphics/glide/raster_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace reluva::glide {

namespace {

using namespace raster;

// floor() to an int without the library call (in range; NaN gives 0).
inline int floorInt(float f) {
    if (!(f > -2.0e9f && f < 2.0e9f))
        return 0;
    const int i = int(f);
    return i - (f < float(i));
}

// A texel index mirrored (GR_TEXTURECLAMP_MIRROR_EXT): the texture, then
// the texture turned round, and so on (Glide 3.0 Programming Guide, Figure
// 13.2). `size` is a power of two. Reluva's reading: the index mirrored, so
// that a bilinear sample at the seam takes the edge texel twice.
inline int mirrorIndex(int i, int size) {
    const int m = i & (2 * size - 1);
    return m < size ? m : 2 * size - 1 - m;
}

// The chroma-key test on the colour combine's "other" colour: equal to the
// key, or with chroma ranging on, every component within the range
// (GR_CHROMARANGE_RGB_ALL_EXT: "If all components of the incoming pixel
// color fall within their respective ranges, the chroma test succeeds and
// the pixel is invalidated", Glide 3.0 Programming Guide, chapter 13).
inline bool chromaKeyed(const State &s, const Rgba &c) {
    if (!s.chromaRange)
        return (uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | uint32_t(c.b)) == (s.chromakeyColor & 0xffffff);
    auto in = [&](int v, int shift) {
        return v >= int(s.chromaRangeMin >> shift & 0xff) && v <= int(s.chromaRangeMax >> shift & 0xff);
    };
    return in(c.r, 16) && in(c.g, 8) && in(c.b, 0);
}

// Whether a combine function reads its "other" input, and its "local".
bool usesOther(const CombineSettings &c) {
    return (c.function >= kFuncScaleOther && c.function <= kFuncScaleOtherMinusLocalAddLocalAlpha) ||
           c.factor == kFactorOtherAlpha || c.factor == kFactorOneMinusOtherAlpha;
}

bool blendUsesDstAlpha(const State &s) {
    auto dst = [](int f) { return f == kBlendDstAlpha || f == kBlendOneMinusDstAlpha; };
    return dst(s.blendRgbSrc) || dst(s.blendRgbDst) || dst(s.blendAlphaSrc) || dst(s.blendAlphaDst) ||
           s.blendRgbSrc == kBlendAlphaSaturate || s.blendAlphaSrc == kBlendAlphaSaturate;
}

class SoftwareRenderer : public Renderer {
public:
    void triangle(Pipeline &p, const Vertex &a, const Vertex &b, const Vertex &c) override;
    void line(Pipeline &p, const Vertex &a, const Vertex &b) override;
    void point(Pipeline &p, const Vertex &a) override;
    void fragments(Pipeline &p, const Fragments &f) override;
    void clear(Pipeline &p, uint32_t argb, int alpha, int depth) override;

private:
    // What a primitive needs, worked out once from the state.
    struct Setup {
        Pipeline *p = nullptr;
        const State *s = nullptr;
        uint16_t *color = nullptr;
        uint16_t *aux = nullptr;
        int width = 0, height = 0;
        bool texture = false;     // the combine reads the TMUs
        int tmusUsed = 0;         // how many TMUs, from TMU0 up, take part
        bool alphaBuffer = false; // the auxiliary buffer holds alpha
        bool blend = false;
        PixelCombine combine;
        // Common configurations done directly (the generic units give the
        // same): TMU0 alone as a decal; the colour and alpha combine as the
        // texture times the local (iterated or constant) colour, or as the
        // local colour alone.
        bool decal0 = false;
        enum class Fast { None, Modulate, Local } fast = Fast::None;
        Rgba constant, fogColor;
        // Screen-space gradients of each value (for the LOD).
        float dx[kParams] = {}, dy[kParams] = {};
        bool derivatives = false;
        int params = kS0; // how many values to iterate: colour, z, w, then the TMUs used
        // Each TMU's source, worked out once a primitive.
        struct Tmu {
            bool valid = false;
            bool perPixelLod = false; // mipmapping, or filters that differ by LOD
            int lodMin = 0, lodMax = 0;
            int format = 0, bytes = 2;
            const uint8_t *mem = nullptr;
            uint32_t memMask = 0;
            const uint32_t *palette = nullptr;
            const NccTable *ncc = nullptr;
            uint32_t base[9] = {};
            int w[9] = {}, h[9] = {};
            float scale[9] = {}; // texels of the level per unit of s or t
        } tmu[kMaxTmus];
    };
    bool prepare(Pipeline &p, Setup &su);
    void shade(Setup &su, int x, int row, const Values &v);
    Rgba sampleTmu(Setup &su, int tmu, const Values &v, int x, int row, int &lodFraction, float &rawLod);
};

bool SoftwareRenderer::prepare(Pipeline &p, Setup &su) {
    su.p = &p;
    su.s = &p.state();
    const State &s = p.state();
    su.color = p.buffer(s.renderBuffer);
    su.aux = p.buffer(kBufferAux);
    su.width = p.width();
    su.height = p.height();
    if (!su.color)
        return false;
    su.combine.color = s.color.settings;
    su.combine.alpha = s.alpha.settings;
    su.combine.colorLocal = s.color.local;
    su.combine.colorOther = s.color.other;
    su.combine.alphaLocal = s.alpha.local;
    su.combine.alphaOther = s.alpha.other;
    su.combine.alphaControlsLighting = s.alphaControlsLighting;
    su.constant = unpackArgb(s.constantColor);
    su.fogColor = unpackArgb(s.fogColor);
    auto factorUsesTexture = [](int f) {
        return f == kFactorTextureAlpha || f == kFactorTextureRgb || f == kFactorOneMinusTextureAlpha ||
               f == kFactorOneMinusLodFraction;
    };
    su.texture = s.color.other == kOtherTexture || s.alpha.other == kOtherTexture ||
                 factorUsesTexture(s.color.settings.factor) || factorUsesTexture(s.alpha.settings.factor) ||
                 s.alphaControlsLighting;
    // TMU0 always, the next ones while the one below reads its "other".
    su.tmusUsed = 0;
    if (su.texture) {
        su.tmusUsed = 1;
        while (su.tmusUsed < p.board().tmus) {
            const TmuState &t = s.tmus[size_t(su.tmusUsed - 1)];
            if (!usesOther(t.rgb) && !usesOther(t.alpha))
                break;
            ++su.tmusUsed;
        }
    }
    if (su.tmusUsed == 1) {
        const TmuState &t0 = s.tmus[0];
        su.decal0 =
            t0.rgb.function == kFuncLocal && t0.alpha.function == kFuncLocal && !t0.rgb.invert && !t0.alpha.invert;
    }
    {
        const CombineUnit &c = s.color;
        const CombineUnit &a = s.alpha;
        const bool plain = !c.settings.invert && !a.settings.invert && !s.alphaControlsLighting &&
                           c.local != kLocalDepth && a.local != kLocalDepth && c.local == a.local;
        if (plain && c.settings.function == kFuncScaleOther && c.settings.factor == kFactorLocal &&
            c.other == kOtherTexture && a.settings.function == kFuncScaleOther && a.settings.factor == kFactorLocal &&
            a.other == kOtherTexture)
            su.fast = Setup::Fast::Modulate;
        else if (plain && c.settings.function == kFuncLocal && a.settings.function == kFuncLocal && !su.texture)
            su.fast = Setup::Fast::Local;
    }
    su.params = kS0 + 3 * su.tmusUsed;
    for (int i = 0; i < su.tmusUsed; ++i) {
        const TmuState &t = s.tmus[size_t(i)];
        Setup::Tmu &m = su.tmu[i];
        m.valid = t.hasSource && knownFormat(t.info.format);
        if (!m.valid)
            continue;
        m.lodMin = 8 - t.info.largeLod;
        m.lodMax = t.mipmapMode != kMipmapDisable ? 8 - t.info.smallLod : m.lodMin;
        m.perPixelLod = m.lodMax > m.lodMin || t.minFilter != t.magFilter || t.lodBlend ||
                        t.rgb.factor == kFactorTextureRgb || t.alpha.factor == kFactorTextureRgb ||
                        t.rgb.factor == kFactorOneMinusLodFraction || t.alpha.factor == kFactorOneMinusLodFraction ||
                        t.rgb.factor == kFactorTextureAlpha || t.alpha.factor == kFactorTextureAlpha ||
                        t.rgb.factor == kFactorOneMinusTextureAlpha || t.alpha.factor == kFactorOneMinusTextureAlpha;
        m.format = p.textureFormat(i); // P_8 through the 6666 palette reads as format 6
        m.bytes = texelBytes(t.info.format);
        m.mem = p.tmuMemory(i);
        m.memMask = p.tmuMemorySize() - 1; // a power of two
        m.palette = p.palette(i);
        m.ncc = &p.ncc(i, t.nccTable);
        for (int l = t.info.smallLod; l <= t.info.largeLod; ++l) {
            m.base[l] = p.levelAddress(i, l);
            levelSize(l, t.info.aspect, m.w[l], m.h[l]);
            m.scale[l] = std::ldexp(1.0f, l - 8);
        }
    }
    su.alphaBuffer = s.depthMode == kDepthDisable && blendUsesDstAlpha(s);
    su.blend = !(s.blendRgbSrc == kBlendOne && s.blendRgbDst == kBlendZero && s.blendAlphaSrc == kBlendOne &&
                 s.blendAlphaDst == kBlendZero);
    return true;
}

Rgba SoftwareRenderer::sampleTmu(Setup &su, int tmu, const Values &v, int x, int row, int &lodFraction, float &rawLod) {
    const State &s = *su.s;
    const TmuState &t = s.tmus[size_t(tmu)];
    const Setup::Tmu &m = su.tmu[tmu];
    lodFraction = 0;
    rawLod = 0;
    if (!m.valid)
        return {255, 255, 255, 255};
    const float S = v.v[kS0 + 3 * tmu];
    const float T = v.v[kS0 + 3 * tmu + 1];
    const float Q = v.v[kS0 + 3 * tmu + 2];
    const float inv = Q != 0 ? 1.0f / Q : 0.0f;
    const float ss = S * inv;
    const float tt = T * inv; // 0-256 across the texture at every level
    // The level of detail: log2 of how many level-256 texels a pixel
    // spans, plus the bias (grTexLodBiasValue); 0 at the largest level.
    int level = t.info.largeLod;
    bool magnify = false;
    if (m.perPixelLod) {
        float lod = -1e9f;
        if (su.derivatives) {
            const int base = kS0 + 3 * tmu;
            const float dsdx = (su.dx[base] - ss * su.dx[base + 2]) * inv;
            const float dtdx = (su.dx[base + 1] - tt * su.dx[base + 2]) * inv;
            const float dsdy = (su.dy[base] - ss * su.dy[base + 2]) * inv;
            const float dtdy = (su.dy[base + 1] - tt * su.dy[base + 2]) * inv;
            const float rho2 = std::max(dsdx * dsdx + dtdx * dtdx, dsdy * dsdy + dtdy * dtdy);
            if (rho2 > 0)
                lod = 0.5f * std::log2(rho2);
        }
        rawLod = lod;
        lod += t.lodBias;
        magnify = lod < float(m.lodMin);
        const float clamped = std::clamp(lod, float(m.lodMin), float(m.lodMax));
        float chosen = clamped;
        if (t.mipmapMode == kMipmapNearestDither) {
            // The LOD dither adds an ordered offset averaging 3/8 (Voodoo2
            // specification, textureMode's tloddither); Reluva uses the 4x4
            // matrix's threshold, scaled to 0-3/4.
            static constexpr int kBayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            chosen = std::min(clamped + float(kBayer4[(row & 3) * 4 + (x & 3)]) / 16.0f * 0.75f, float(m.lodMax));
        }
        level = std::clamp(8 - int(std::floor(chosen)), t.info.smallLod, t.info.largeLod);
        lodFraction = int((clamped - std::floor(clamped)) * 255.0f);
    } else {
        magnify = t.lodBias < float(m.lodMin); // the filters are the same either way
    }
    const bool bilinear = (magnify ? t.magFilter : t.minFilter) == kFilterBilinear;
    const int w = m.w[level];
    const int h = m.h[level];
    float u = ss * m.scale[level];
    float vv = tt * m.scale[level];
    const uint32_t base = m.base[level];
    auto fetch = [&](int ix, int iy) {
        ix = t.clampS ? std::clamp(ix, 0, w - 1) : t.mirrorS ? mirrorIndex(ix, w) : ix & (w - 1);
        iy = t.clampT ? std::clamp(iy, 0, h - 1) : t.mirrorT ? mirrorIndex(iy, h) : iy & (h - 1);
        const uint32_t at = base + uint32_t(iy * w + ix) * uint32_t(m.bytes);
        const uint32_t raw =
            m.bytes == 2 ? uint32_t(m.mem[at & m.memMask] | m.mem[(at + 1) & m.memMask] << 8) : m.mem[at & m.memMask];
        return decodeTexel(m.format, raw, m.palette, *m.ncc);
    };
    if (!bilinear)
        return unpackArgb(fetch(floorInt(u), floorInt(vv)));
    // Bilinear: the four texels round the sample, weighted by its
    // fraction in 1/256ths.
    u -= 0.5f;
    vv -= 0.5f;
    const int ix = floorInt(u);
    const int iy = floorInt(vv);
    const float fu = float(ix);
    const float fv = float(iy);
    const int wx = int((u - fu) * 256.0f);
    const int wy = int((vv - fv) * 256.0f);
    const Rgba c00 = unpackArgb(fetch(ix, iy));
    const Rgba c10 = unpackArgb(fetch(ix + 1, iy));
    const Rgba c01 = unpackArgb(fetch(ix, iy + 1));
    const Rgba c11 = unpackArgb(fetch(ix + 1, iy + 1));
    auto mix = [&](int a, int b, int c, int d) {
        return ((a * (256 - wx) + b * wx) * (256 - wy) + (c * (256 - wx) + d * wx) * wy) >> 16;
    };
    return {mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g), mix(c00.b, c10.b, c01.b, c11.b),
            mix(c00.a, c10.a, c01.a, c11.a)};
}

void SoftwareRenderer::shade(Setup &su, int x, int row, const Values &v) {
    const State &s = *su.s;
    PixelStats &stats = su.p->stats;
    ++stats.pixelsIn;
    const Rgba iterated{std::clamp(int(v.v[kR]), 0, 255), std::clamp(int(v.v[kG]), 0, 255),
                        std::clamp(int(v.v[kB]), 0, 255), std::clamp(int(v.v[kA]), 0, 255)};
    const int z = std::clamp(int(v.v[kZ]), 0, 0xffff);
    Rgba texture{255, 255, 255, 255};
    if (su.decal0) {
        int lodFraction = 0;
        float lod = 0;
        texture = sampleTmu(su, 0, v, x, row, lodFraction, lod);
    } else if (su.texture) {
        // From the last TMU used down to TMU0, each combining its own texel
        // with what the one above gives.
        Rgba other{0, 0, 0, 0};
        for (int i = su.tmusUsed - 1; i >= 0; --i) {
            const TmuState &t = s.tmus[size_t(i)];
            int lodFraction = 0;
            float lod = 0;
            const Rgba local = sampleTmu(su, i, v, x, row, lodFraction, lod);
            int detail = 0;
            if (t.rgb.factor == kFactorTextureAlpha || t.alpha.factor == kFactorTextureAlpha ||
                t.rgb.factor == kFactorOneMinusTextureAlpha || t.alpha.factor == kFactorOneMinusTextureAlpha) {
                // β = min(detailMax, max(0, (bias - LOD) << scale) / 255), the
                // LOD before grTexLodBiasValue's bias (Reference Manual,
                // grTexDetailControl).
                const float beta = std::min(
                    t.detailMax, std::max(0.0f, (float(t.detailBias) - lod) * float(1 << t.detailScale)) / 255.0f);
                detail = int(beta * 255.0f);
            }
            other = combineTexture(t.rgb, t.alpha, local, other, detail, lodFraction);
        }
        texture = other;
    }
    Rgba chromaOther;
    Rgba c;
    if (su.fast == Setup::Fast::Modulate) {
        const Rgba &l = su.combine.colorLocal == kLocalConstant ? su.constant : iterated;
        c = {scaleBy(texture.r, l.r), scaleBy(texture.g, l.g), scaleBy(texture.b, l.b), scaleBy(texture.a, l.a)};
        chromaOther = texture;
    } else if (su.fast == Setup::Fast::Local) {
        c = su.combine.colorLocal == kLocalConstant ? su.constant : iterated;
        chromaOther = su.combine.colorOther == kOtherConstant ? su.constant : iterated;
    } else {
        c = combinePixel(su.combine, iterated, texture, su.constant, z >> 8, &chromaOther);
    }
    if (s.chromakey && chromaKeyed(s, chromaOther)) {
        ++stats.chromaFail;
        return;
    }
    if (!compare(s.alphaTest, c.a, s.alphaRef)) {
        ++stats.alphaFail;
        return;
    }
    const size_t at = size_t(row) * size_t(su.width) + size_t(x);
    int depthWrite = -1;
    if (s.depthMode != kDepthDisable && su.aux) {
        const bool wMode = s.depthMode == kDepthWBuffer || s.depthMode == kDepthWCompareToBias;
        const bool toBias = s.depthMode == kDepthZCompareToBias || s.depthMode == kDepthWCompareToBias;
        const int source = wMode ? floatDepth(v.v[kW]) : z;
        const int biased = std::clamp(source + s.depthBias, 0, 0xffff);
        const int compared = toBias ? std::clamp(s.depthBias, 0, 0xffff) : biased;
        if (!compare(s.depthFunc, compared, su.aux[at])) {
            ++stats.depthFail;
            return;
        }
        depthWrite = toBias ? source : biased;
    }
    const Rgba prefog = c;
    if ((s.fogMode & 3) != kFogDisable) {
        int f = 0;
        switch (s.fogMode & 3) {
        case kFogWithIteratedAlpha:
            f = iterated.a;
            break;
        case kFogWithTable:
            f = fogTableFactor(s.fogTable.data(), floatDepth(v.v[kW]));
            break;
        default:
            f = z >> 8;
            break; // GR_FOG_WITH_ITERATED_Z: the iterated z's high bits
        }
        c = applyFog(c, su.fogColor, f, s.fogMode);
    }
    if (su.blend) {
        Rgba dst = from565(su.color[at]);
        dst.a = su.alphaBuffer && su.aux ? su.aux[at] & 0xff : 255;
        c = blendPixel(c, dst, prefog, s.blendRgbSrc, s.blendRgbDst, s.blendAlphaSrc, s.blendAlphaDst);
    }
    if (s.colorMask)
        su.color[at] = to565(c.r, c.g, c.b, x, row, s.dither);
    if (su.alphaBuffer && s.alphaMask && su.aux)
        su.aux[at] = uint16_t(c.a);
    if (depthWrite >= 0 && s.depthMask)
        su.aux[at] = uint16_t(depthWrite);
    ++stats.pixelsOut;
}

void SoftwareRenderer::triangle(Pipeline &p, const Vertex &va, const Vertex &vb, const Vertex &vc) {
    Setup su;
    if (!prepare(p, su)) {
        ++p.stats.trianglesIn;
        return;
    }
    Triangle t;
    if (!setupTriangle(*su.s, su.width, su.height, va, vb, vc, t, p.stats))
        return;
    std::copy(t.dx, t.dx + kParams, su.dx);
    std::copy(t.dy, t.dy + kParams, su.dy);
    su.derivatives = true;
    const bool flip = su.s->originLowerLeft;
    const int params = su.params;
    for (int y = t.y0; y < t.y1; ++y) {
        int xs = 0;
        int xe = 0;
        if (!t.span(y, xs, xe))
            continue;
        // The values at the first pixel's centre.
        Values rowv;
        const float px = float(double(xs) + 0.5 - t.fx0);
        const float py = float(double(y) + 0.5 - t.fy0);
        for (int k = 0; k < params; ++k)
            rowv.v[k] = t.at0.v[k] + su.dx[k] * px + su.dy[k] * py;
        const int row = flip ? su.height - 1 - y : y;
        for (int x = xs; x <= xe; ++x) {
            shade(su, x, row, rowv);
            for (int k = 0; k < params; ++k)
                rowv.v[k] += su.dx[k];
        }
    }
}

void SoftwareRenderer::line(Pipeline &p, const Vertex &a, const Vertex &b) {
    Setup su;
    if (!prepare(p, su))
        return;
    forLinePixels(*su.s, su.width, su.height, a, b, [&](int x, int y, const Values &v) {
        shade(su, x, su.s->originLowerLeft ? su.height - 1 - y : y, v);
    });
}

void SoftwareRenderer::point(Pipeline &p, const Vertex &a) {
    Setup su;
    if (!prepare(p, su))
        return;
    int x = 0;
    int y = 0;
    if (!pointPixel(*su.s, su.width, su.height, a, x, y))
        return;
    Values v;
    load(*su.s, a, v);
    shade(su, x, su.s->originLowerLeft ? su.height - 1 - y : y, v);
}

void SoftwareRenderer::fragments(Pipeline &p, const Fragments &f) {
    Setup su;
    if (!prepare(p, su))
        return;
    if (f.derivatives) {
        std::copy(f.dx, f.dx + kParams, su.dx);
        std::copy(f.dy, f.dy + kParams, su.dy);
        su.derivatives = true;
    }
    for (const Fragment &frag : f.list)
        shade(su, frag.x, su.s->originLowerLeft ? su.height - 1 - frag.y : frag.y, frag.v);
}

void SoftwareRenderer::clear(Pipeline &p, uint32_t argb, int alpha, int depth) {
    const State &s = p.state();
    uint16_t *color = p.buffer(s.renderBuffer);
    uint16_t *aux = p.buffer(kBufferAux);
    const int w = p.width();
    const int h = p.height();
    const int x0 = std::max(s.clip[0], 0);
    const int x1 = std::min(s.clip[2], w);
    const int y0 = std::max(s.clip[1], 0);
    const int y1 = std::min(s.clip[3], h);
    const Rgba c = unpackArgb(argb);
    const bool clearDepth = s.depthMode != kDepthDisable && s.depthMask && aux;
    const bool clearAlpha = !clearDepth && s.depthMode == kDepthDisable && blendUsesDstAlpha(s) && s.alphaMask && aux;
    for (int y = y0; y < y1; ++y) {
        const int row = s.originLowerLeft ? h - 1 - y : y;
        for (int x = x0; x < x1; ++x) {
            const size_t at = size_t(row) * size_t(w) + size_t(x);
            if (s.colorMask && color)
                color[at] = to565(c.r, c.g, c.b, x, row, s.dither);
            if (clearDepth)
                aux[at] = uint16_t(depth);
            else if (clearAlpha)
                aux[at] = uint16_t(alpha & 0xff);
        }
    }
    p.stats.pixelsOut += uint64_t(std::max(0, x1 - x0)) * uint64_t(std::max(0, y1 - y0));
}

} // namespace

std::unique_ptr<Renderer> makeSoftwareRenderer() {
    return std::make_unique<SoftwareRenderer>();
}

} // namespace reluva::glide
