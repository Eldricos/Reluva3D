// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The software reference rasterizer: Glide's triangles, lines and points
// drawn pixel by pixel through the Voodoo's pipeline, in the order the
// Voodoo2 specification draws it (its section 3 and the fbzMode, fogMode
// and alphaMode figures): clip window, texture lookup and the TMUs'
// combine, chroma-key, the colour and alpha combine, alpha test, depth
// test, fog, alpha blending, the dither to 5:6:5 and the writes.
// docs/engines/glide.md, The software rasterizer, says what follows the
// documents and what is Reluva's own choice.

#include "common/glide/pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace reluva::glide {

namespace {

// Values iterated across a primitive: linear in screen space.
enum Param {
    kR,
    kG,
    kB,
    kA,
    kZ,
    kW,
    kS0, // then T0, Q0 (the TMU's 1/w), S1, T1, Q1...
    kParams = kS0 + 3 * kMaxTmus,
};

struct Values {
    float v[kParams];
};

// floor() to an int without the library call (in range; NaN gives 0).
inline int floorInt(float f) {
    if (!(f > -2.0e9f && f < 2.0e9f))
        return 0;
    const int i = int(f);
    return i - (f < float(i));
}

// Whether a combine function reads its "other" input, and its "local".
bool usesOther(const CombineSettings& c) {
    return (c.function >= kFuncScaleOther && c.function <= kFuncScaleOtherMinusLocalAddLocalAlpha) ||
           c.factor == kFactorOtherAlpha || c.factor == kFactorOneMinusOtherAlpha;
}

bool blendUsesDstAlpha(const State& s) {
    auto dst = [](int f) { return f == kBlendDstAlpha || f == kBlendOneMinusDstAlpha; };
    return dst(s.blendRgbSrc) || dst(s.blendRgbDst) || dst(s.blendAlphaSrc) || dst(s.blendAlphaDst) ||
           s.blendRgbSrc == kBlendAlphaSaturate || s.blendAlphaSrc == kBlendAlphaSaturate;
}

class SoftwareRenderer : public Renderer {
public:
    void triangle(Pipeline& p, const Vertex& a, const Vertex& b, const Vertex& c) override;
    void line(Pipeline& p, const Vertex& a, const Vertex& b) override;
    void point(Pipeline& p, const Vertex& a) override;
    void clear(Pipeline& p, uint32_t argb, int alpha, int depth) override;

private:
    // What a primitive needs, worked out once from the state.
    struct Setup {
        Pipeline* p = nullptr;
        const State* s = nullptr;
        uint16_t* color = nullptr;
        uint16_t* aux = nullptr;
        int width = 0, height = 0;
        bool texture = false;      // the combine reads the TMUs
        int tmusUsed = 0;          // how many TMUs, from TMU0 up, take part
        bool alphaBuffer = false;  // the auxiliary buffer holds alpha
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
            const uint8_t* mem = nullptr;
            uint32_t memMask = 0;
            const uint32_t* palette = nullptr;
            const NccTable* ncc = nullptr;
            uint32_t base[9] = {};
            int w[9] = {}, h[9] = {};
            float scale[9] = {}; // texels of the level per unit of s or t
        } tmu[kMaxTmus];
    };
    bool prepare(Pipeline& p, Setup& su);
    // Values for TMU i from a vertex, as the stw hints say where to read.
    static void load(const State& s, const Vertex& v, Values& out);
    void shade(Setup& su, int x, int row, const Values& v);
    Rgba sampleTmu(Setup& su, int tmu, const Values& v, int x, int row, int& lodFraction, float& rawLod);
};

bool SoftwareRenderer::prepare(Pipeline& p, Setup& su) {
    su.p = &p;
    su.s = &p.state();
    const State& s = p.state();
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
            const TmuState& t = s.tmus[size_t(su.tmusUsed - 1)];
            if (!usesOther(t.rgb) && !usesOther(t.alpha))
                break;
            ++su.tmusUsed;
        }
    }
    if (su.tmusUsed == 1) {
        const TmuState& t0 = s.tmus[0];
        su.decal0 = t0.rgb.function == kFuncLocal && t0.alpha.function == kFuncLocal && !t0.rgb.invert &&
                    !t0.alpha.invert;
    }
    {
        const CombineUnit &c = s.color, &a = s.alpha;
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
        const TmuState& t = s.tmus[size_t(i)];
        Setup::Tmu& m = su.tmu[i];
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
        m.format = t.info.format;
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

void SoftwareRenderer::load(const State& s, const Vertex& v, Values& out) {
    out.v[kR] = v.r;
    out.v[kG] = v.g;
    out.v[kB] = v.b;
    out.v[kA] = v.a;
    out.v[kZ] = v.ooz;
    out.v[kW] = v.oow;
    // s and t: TMU0's from tmuvtx[0], each later TMU the same unless the
    // hints say its own differ; 1/w: the vertex's oow unless a hint names
    // the TMU's own (Glide 2.4 Reference Manual, grHints).
    float sow = v.tmu[0].sow, tow = v.tmu[0].tow, q = v.oow;
    if (s.stwHints & kStwWDiffTmu0)
        q = v.tmu[0].oow;
    for (int i = 0; i < kMaxTmus; ++i) {
        if (i > 0 && (s.stwHints & (i == 1 ? kStwStDiffTmu1 : kStwStDiffTmu2))) {
            sow = v.tmu[size_t(i)].sow;
            tow = v.tmu[size_t(i)].tow;
        }
        if (i > 0 && (s.stwHints & (i == 1 ? kStwWDiffTmu1 : kStwWDiffTmu2)))
            q = v.tmu[size_t(i)].oow;
        out.v[kS0 + 3 * i] = sow;
        out.v[kS0 + 3 * i + 1] = tow;
        out.v[kS0 + 3 * i + 2] = q;
    }
}

Rgba SoftwareRenderer::sampleTmu(Setup& su, int tmu, const Values& v, int x, int row, int& lodFraction,
                                  float& rawLod) {
    const State& s = *su.s;
    const TmuState& t = s.tmus[size_t(tmu)];
    const Setup::Tmu& m = su.tmu[tmu];
    lodFraction = 0;
    rawLod = 0;
    if (!m.valid)
        return {255, 255, 255, 255};
    const float S = v.v[kS0 + 3 * tmu], T = v.v[kS0 + 3 * tmu + 1], Q = v.v[kS0 + 3 * tmu + 2];
    const float inv = Q != 0 ? 1.0f / Q : 0.0f;
    const float ss = S * inv, tt = T * inv; // 0-256 across the texture at every level
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
    const int w = m.w[level], h = m.h[level];
    float u = ss * m.scale[level], vv = tt * m.scale[level];
    const uint32_t base = m.base[level];
    auto fetch = [&](int ix, int iy) {
        ix = t.clampS ? std::clamp(ix, 0, w - 1) : ix & (w - 1);
        iy = t.clampT ? std::clamp(iy, 0, h - 1) : iy & (h - 1);
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
    const int ix = floorInt(u), iy = floorInt(vv);
    const float fu = float(ix), fv = float(iy);
    const int wx = int((u - fu) * 256.0f), wy = int((vv - fv) * 256.0f);
    const Rgba c00 = unpackArgb(fetch(ix, iy)), c10 = unpackArgb(fetch(ix + 1, iy));
    const Rgba c01 = unpackArgb(fetch(ix, iy + 1)), c11 = unpackArgb(fetch(ix + 1, iy + 1));
    auto mix = [&](int a, int b, int c, int d) {
        return ((a * (256 - wx) + b * wx) * (256 - wy) + (c * (256 - wx) + d * wx) * wy) >> 16;
    };
    return {mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g), mix(c00.b, c10.b, c01.b, c11.b),
            mix(c00.a, c10.a, c01.a, c11.a)};
}

void SoftwareRenderer::shade(Setup& su, int x, int row, const Values& v) {
    const State& s = *su.s;
    PixelStats& stats = su.p->stats;
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
            const TmuState& t = s.tmus[size_t(i)];
            int lodFraction = 0;
            float lod = 0;
            const Rgba local = sampleTmu(su, i, v, x, row, lodFraction, lod);
            int detail = 0;
            if (t.rgb.factor == kFactorTextureAlpha || t.alpha.factor == kFactorTextureAlpha ||
                t.rgb.factor == kFactorOneMinusTextureAlpha || t.alpha.factor == kFactorOneMinusTextureAlpha) {
                // β = min(detailMax, max(0, (bias - LOD) << scale) / 255), the
                // LOD before grTexLodBiasValue's bias (Reference Manual,
                // grTexDetailControl).
                const float beta = std::min(t.detailMax, std::max(0.0f, (float(t.detailBias) - lod) *
                                                                            float(1 << t.detailScale)) / 255.0f);
                detail = int(beta * 255.0f);
            }
            other = combineTexture(t.rgb, t.alpha, local, other, detail, lodFraction);
        }
        texture = other;
    }
    Rgba chromaOther;
    Rgba c;
    if (su.fast == Setup::Fast::Modulate) {
        const Rgba& l = su.combine.colorLocal == kLocalConstant ? su.constant : iterated;
        c = {scaleBy(texture.r, l.r), scaleBy(texture.g, l.g), scaleBy(texture.b, l.b), scaleBy(texture.a, l.a)};
        chromaOther = texture;
    } else if (su.fast == Setup::Fast::Local) {
        c = su.combine.colorLocal == kLocalConstant ? su.constant : iterated;
        chromaOther = su.combine.colorOther == kOtherConstant ? su.constant : iterated;
    } else {
        c = combinePixel(su.combine, iterated, texture, su.constant, z >> 8, &chromaOther);
    }
    if (s.chromakey && (uint32_t(chromaOther.r) << 16 | uint32_t(chromaOther.g) << 8 | uint32_t(chromaOther.b)) ==
                           (s.chromakeyColor & 0xffffff)) {
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
        case kFogWithIteratedAlpha: f = iterated.a; break;
        case kFogWithTable: f = fogTableFactor(s.fogTable.data(), floatDepth(v.v[kW])); break;
        default: f = z >> 8; break; // GR_FOG_WITH_ITERATED_Z: the iterated z's high bits
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

void SoftwareRenderer::triangle(Pipeline& p, const Vertex& va, const Vertex& vb, const Vertex& vc) {
    ++p.stats.trianglesIn;
    Setup su;
    if (!prepare(p, su))
        return;
    const State& s = *su.s;
    // The vertices snapped to 1/16 pixel (the hardware takes x and y as
    // 12.4 fixed point).
    auto snap = [](float f) { return std::isfinite(f) ? int64_t(std::llround(double(f) * 16.0)) : int64_t(0); };
    const Vertex* v[3] = {&va, &vb, &vc};
    int64_t X[3], Y[3];
    for (int i = 0; i < 3; ++i) {
        X[i] = snap(v[i]->x);
        Y[i] = snap(v[i]->y);
    }
    int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0)
        return;
    // The sign of the area, as grCullMode reads it: positive for a triangle
    // turning clockwise on the screen with the origin at the top left.
    if ((s.cull == kCullNegative && area < 0) || (s.cull == kCullPositive && area > 0))
        return;
    if (area < 0) {
        std::swap(v[1], v[2]);
        std::swap(X[1], X[2]);
        std::swap(Y[1], Y[2]);
        area = -area;
    }
    ++p.stats.trianglesOut;
    // The pixels: those whose centre is inside all three edges, or on a
    // left edge or an edge along the lower y (Glide 2.4 Reference Manual,
    // grDrawTriangle), within the clip window and the screen.
    const int64_t minX = std::min({X[0], X[1], X[2]}), maxX = std::max({X[0], X[1], X[2]});
    const int64_t minY = std::min({Y[0], Y[1], Y[2]}), maxY = std::max({Y[0], Y[1], Y[2]});
    const int x0 = std::max<int64_t>({(minX - 8 + 15) >> 4, s.clip[0], 0});
    const int x1 = int(std::min<int64_t>({((maxX - 8) >> 4) + 1, s.clip[2], su.width}));
    const int y0 = std::max<int64_t>({(minY - 8 + 15) >> 4, s.clip[1], 0});
    const int y1 = int(std::min<int64_t>({((maxY - 8) >> 4) + 1, s.clip[3], su.height}));
    if (x0 >= x1 || y0 >= y1)
        return;
    // Edge i runs from vertex i to vertex i+1; with the area positive the
    // inside is where each edge function is positive.
    struct Edge {
        int64_t a, b, c; // E(X, Y) = a * X + b * Y + c, X and Y in 1/16 pixel
        bool inclusive;
    } e[3];
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const int64_t dx = X[j] - X[i], dy = Y[j] - Y[i];
        e[i].a = -dy;
        e[i].b = dx;
        e[i].c = dy * X[i] - dx * Y[i];
        e[i].inclusive = dy < 0 || (dy == 0 && dx > 0); // a left edge, or the edge along the lower y
    }
    // The plane of each value over the snapped positions.
    Values at[3];
    for (int i = 0; i < 3; ++i)
        load(s, *v[i], at[i]);
    const double fx0 = double(X[0]) / 16, fy0 = double(Y[0]) / 16;
    const double ex1 = double(X[1] - X[0]) / 16, ey1 = double(Y[1] - Y[0]) / 16;
    const double ex2 = double(X[2] - X[0]) / 16, ey2 = double(Y[2] - Y[0]) / 16;
    const double det = ex1 * ey2 - ex2 * ey1;
    for (int k = 0; k < kParams; ++k) {
        const double d1 = double(at[1].v[k]) - at[0].v[k], d2 = double(at[2].v[k]) - at[0].v[k];
        su.dx[k] = float((d1 * ey2 - d2 * ey1) / det);
        su.dy[k] = float((d2 * ex1 - d1 * ex2) / det);
    }
    su.derivatives = true;
    const bool flip = s.originLowerLeft;
    // Integer division rounding down and up, for any signs.
    auto floorDiv = [](int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); };
    auto ceilDiv = [&](int64_t a, int64_t b) { return -floorDiv(-a, b); };
    const int params = su.params;
    for (int y = y0; y < y1; ++y) {
        const int64_t Yc = int64_t(y) * 16 + 8;
        // The row's span: for each edge, the pixels k from x0 where
        // E0 + 16 a k >= t (t 0 on an inclusive edge, else 1).
        int64_t kmin = 0, kmax = int64_t(x1 - x0) - 1;
        for (int i = 0; i < 3 && kmin <= kmax; ++i) {
            const int64_t E0 = e[i].a * (int64_t(x0) * 16 + 8) + e[i].b * Yc + e[i].c;
            const int64_t t = e[i].inclusive ? 0 : 1, step = e[i].a * 16;
            if (step > 0)
                kmin = std::max(kmin, ceilDiv(t - E0, step));
            else if (step < 0)
                kmax = std::min(kmax, floorDiv(E0 - t, -step));
            else if (E0 < t)
                kmax = -1;
        }
        if (kmin > kmax)
            continue;
        const int xs = x0 + int(kmin), xe = x0 + int(kmax);
        // The values at the first pixel's centre.
        Values rowv;
        const float px = float(double(xs) + 0.5 - fx0), py = float(double(y) + 0.5 - fy0);
        for (int k = 0; k < params; ++k)
            rowv.v[k] = at[0].v[k] + su.dx[k] * px + su.dy[k] * py;
        const int row = flip ? su.height - 1 - y : y;
        for (int x = xs; x <= xe; ++x) {
            shade(su, x, row, rowv);
            for (int k = 0; k < params; ++k)
                rowv.v[k] += su.dx[k];
        }
    }
}

void SoftwareRenderer::line(Pipeline& p, const Vertex& a, const Vertex& b) {
    Setup su;
    if (!prepare(p, su))
        return;
    const State& s = *su.s;
    Values va, vb;
    load(s, a, va);
    load(s, b, vb);
    // One pixel a step along the longer axis, at each pixel centre the
    // line crosses; the values linear between the ends.
    const float dx = b.x - a.x, dy = b.y - a.y;
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    const bool xMajor = std::fabs(dx) >= std::fabs(dy);
    const float from = xMajor ? std::min(a.x, b.x) : std::min(a.y, b.y);
    const float to = xMajor ? std::max(a.x, b.x) : std::max(a.y, b.y);
    const int first = int(std::ceil(from - 0.5f)), last = int(std::ceil(to - 0.5f)) - 1;
    if (last - first > 4096)
        return;
    const float span = xMajor ? dx : dy;
    for (int i = first; i <= last; ++i) {
        const float c = float(i) + 0.5f;
        const float t = span != 0 ? (c - (xMajor ? a.x : a.y)) / span : 0.0f;
        const float other = xMajor ? a.y + dy * t : a.x + dx * t;
        const int x = xMajor ? i : int(std::floor(other)), y = xMajor ? int(std::floor(other)) : i;
        if (x < std::max(s.clip[0], 0) || x >= std::min(s.clip[2], su.width) || y < std::max(s.clip[1], 0) ||
            y >= std::min(s.clip[3], su.height))
            continue;
        Values v;
        for (int k = 0; k < kParams; ++k)
            v.v[k] = va.v[k] + (vb.v[k] - va.v[k]) * t;
        shade(su, x, s.originLowerLeft ? su.height - 1 - y : y, v);
    }
}

void SoftwareRenderer::point(Pipeline& p, const Vertex& a) {
    Setup su;
    if (!prepare(p, su))
        return;
    const State& s = *su.s;
    if (!std::isfinite(a.x) || !std::isfinite(a.y))
        return;
    const int x = int(std::floor(a.x)), y = int(std::floor(a.y));
    if (x < std::max(s.clip[0], 0) || x >= std::min(s.clip[2], su.width) || y < std::max(s.clip[1], 0) ||
        y >= std::min(s.clip[3], su.height))
        return;
    Values v;
    load(s, a, v);
    shade(su, x, s.originLowerLeft ? su.height - 1 - y : y, v);
}

void SoftwareRenderer::clear(Pipeline& p, uint32_t argb, int alpha, int depth) {
    const State& s = p.state();
    uint16_t* color = p.buffer(s.renderBuffer);
    uint16_t* aux = p.buffer(kBufferAux);
    const int w = p.width(), h = p.height();
    const int x0 = std::max(s.clip[0], 0), x1 = std::min(s.clip[2], w);
    const int y0 = std::max(s.clip[1], 0), y1 = std::min(s.clip[3], h);
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

std::unique_ptr<Renderer> makeSoftwareRenderer() { return std::make_unique<SoftwareRenderer>(); }

} // namespace reluva::glide
