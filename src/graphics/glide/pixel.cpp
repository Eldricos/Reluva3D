// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/glide/pixel.h"

#include "graphics/glide/glide_defs.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace reluva::glide {

int combineChannel(const CombineSettings &s, int local, int other, int localAlpha, int factor) {
    int v = 0;
    switch (s.function) {
    case kFuncZero:
        v = 0;
        break;
    case kFuncLocal:
        v = local;
        break;
    case kFuncLocalAlpha:
        v = localAlpha;
        break;
    case kFuncScaleOther:
        v = scaleBy(other, factor);
        break;
    case kFuncScaleOtherAddLocal:
        v = scaleBy(other, factor) + local;
        break;
    case kFuncScaleOtherAddLocalAlpha:
        v = scaleBy(other, factor) + localAlpha;
        break;
    case kFuncScaleOtherMinusLocal:
        v = scaleBy(other - local, factor);
        break;
    case kFuncScaleOtherMinusLocalAddLocal:
        v = scaleBy(other - local, factor) + local;
        break;
    case kFuncScaleOtherMinusLocalAddLocalAlpha:
        v = scaleBy(other - local, factor) + localAlpha;
        break;
    case kFuncScaleMinusLocalAddLocal:
        v = scaleBy(-local, factor) + local;
        break;
    case kFuncScaleMinusLocalAddLocalAlpha:
        v = scaleBy(-local, factor) + localAlpha;
        break;
    default:
        v = 0;
        break;
    }
    v = std::clamp(v, 0, 255);
    return s.invert ? 255 - v : v;
}

int combineFactor(int factor, int local, int otherAlpha, int localAlpha, int textureAlpha, int textureChannel) {
    switch (factor) {
    case kFactorZero:
        return 0;
    case kFactorLocal:
        return local;
    case kFactorOtherAlpha:
        return otherAlpha;
    case kFactorLocalAlpha:
        return localAlpha;
    case kFactorTextureAlpha:
        return textureAlpha;
    case kFactorTextureRgb:
        return textureChannel;
    case kFactorOne:
        return 255;
    case kFactorOneMinusLocal:
        return 255 - local;
    case kFactorOneMinusOtherAlpha:
        return 255 - otherAlpha;
    case kFactorOneMinusLocalAlpha:
        return 255 - localAlpha;
    case kFactorOneMinusTextureAlpha:
        return 255 - textureAlpha;
    case kFactorOneMinusLodFraction:
        return 255 - textureChannel;
    default:
        return 0;
    }
}

int textureCombineFactor(int factor, int local, int otherAlpha, int localAlpha, int detail, int lodFraction) {
    switch (factor) {
    case kFactorTextureAlpha:
        return detail; // DETAIL_FACTOR
    case kFactorTextureRgb:
        return lodFraction; // LOD_FRACTION
    case kFactorOneMinusTextureAlpha:
        return 255 - detail;
    case kFactorOneMinusLodFraction:
        return 255 - lodFraction;
    default:
        return combineFactor(factor, local, otherAlpha, localAlpha, 0, 0);
    }
}

Rgba combinePixel(const PixelCombine &p, const Rgba &iterated, const Rgba &texture, const Rgba &constant, int depth8,
                  Rgba *chromaOther) {
    // The inputs: local and other colour, local and other alpha.
    Rgba cl = p.colorLocal == kLocalConstant ? constant : iterated;
    if (p.alphaControlsLighting) // the texel's top alpha bit picks constant over iterated
        cl = (texture.a & 0x80) ? constant : iterated;
    const Rgba co = p.colorOther == kOtherTexture ? texture : p.colorOther == kOtherConstant ? constant : iterated;
    const int al = p.alphaLocal == kLocalConstant ? constant.a : p.alphaLocal == kLocalDepth ? depth8 : iterated.a;
    const int ao = p.alphaOther == kOtherTexture ? texture.a : p.alphaOther == kOtherConstant ? constant.a : iterated.a;
    if (chromaOther)
        *chromaOther = co;
    Rgba out;
    out.r = combineChannel(p.color, cl.r, co.r, al, combineFactor(p.color.factor, cl.r, ao, al, texture.a, texture.r));
    out.g = combineChannel(p.color, cl.g, co.g, al, combineFactor(p.color.factor, cl.g, ao, al, texture.a, texture.g));
    out.b = combineChannel(p.color, cl.b, co.b, al, combineFactor(p.color.factor, cl.b, ao, al, texture.a, texture.b));
    out.a = combineChannel(p.alpha, al, ao, al, combineFactor(p.alpha.factor, al, ao, al, texture.a, texture.a));
    return out;
}

Rgba combineTexture(const CombineSettings &rgb, const CombineSettings &alpha, const Rgba &local, const Rgba &other,
                    int detail, int lodFraction) {
    Rgba out;
    const int la = local.a;
    const int oa = other.a;
    out.r = combineChannel(rgb, local.r, other.r, la,
                           textureCombineFactor(rgb.factor, local.r, oa, la, detail, lodFraction));
    out.g = combineChannel(rgb, local.g, other.g, la,
                           textureCombineFactor(rgb.factor, local.g, oa, la, detail, lodFraction));
    out.b = combineChannel(rgb, local.b, other.b, la,
                           textureCombineFactor(rgb.factor, local.b, oa, la, detail, lodFraction));
    out.a = combineChannel(alpha, la, oa, la, textureCombineFactor(alpha.factor, la, oa, la, detail, lodFraction));
    return out;
}

namespace {

// A blending factor's value (0-255) for one channel.
int blendFactor(int f, int channelSrc, int channelDst, int srcAlpha, int dstAlpha, bool dstSide, int prefog) {
    switch (f) {
    case kBlendZero:
        return 0;
    case kBlendSrcAlpha:
        return srcAlpha;
    case kBlendSrcColor:
        return dstSide ? channelSrc : channelDst; // DST_COLOR on the source side
    case kBlendDstAlpha:
        return dstAlpha;
    case kBlendOne:
        return 255;
    case kBlendOneMinusSrcAlpha:
        return 255 - srcAlpha;
    case kBlendOneMinusSrcColor:
        return 255 - (dstSide ? channelSrc : channelDst);
    case kBlendOneMinusDstAlpha:
        return 255 - dstAlpha;
    case kBlendAlphaSaturate:
        return dstSide ? prefog : std::min(srcAlpha, 255 - dstAlpha);
    default:
        return 0;
    }
}

int blendChannel(int s, int d, int sf, int df) {
    return std::min(255, scaleBy(s, sf) + scaleBy(d, df));
}

} // namespace

Rgba blendPixel(const Rgba &src, const Rgba &dst, const Rgba &prefog, int rgbSrc, int rgbDst, int alphaSrc,
                int alphaDst) {
    Rgba out;
    const int sa = src.a;
    const int da = dst.a;
    out.r = blendChannel(src.r, dst.r, blendFactor(rgbSrc, src.r, dst.r, sa, da, false, prefog.r),
                         blendFactor(rgbDst, src.r, dst.r, sa, da, true, prefog.r));
    out.g = blendChannel(src.g, dst.g, blendFactor(rgbSrc, src.g, dst.g, sa, da, false, prefog.g),
                         blendFactor(rgbDst, src.g, dst.g, sa, da, true, prefog.g));
    out.b = blendChannel(src.b, dst.b, blendFactor(rgbSrc, src.b, dst.b, sa, da, false, prefog.b),
                         blendFactor(rgbDst, src.b, dst.b, sa, da, true, prefog.b));
    out.a = blendChannel(src.a, dst.a, blendFactor(alphaSrc, sa, da, sa, da, false, prefog.a),
                         blendFactor(alphaDst, sa, da, sa, da, true, prefog.a));
    return out;
}

// The dithered 5- and 6-bit value of each 8-bit level at each matrix
// position: (v * (2^bits - 1) * 2n + (2k + 1) * 255) / (255 * 2n),
// tabulated once.
DitherTables::DitherTables() {
    static constexpr int kBayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    static constexpr int kBayer2[4] = {0, 2, 3, 1};
    for (int m = 0; m < 2; ++m) {
        const int n = m == 0 ? 16 : 4;
        for (int pos = 0; pos < 16; ++pos) {
            const int k = m == 0 ? kBayer4[pos] : kBayer2[((pos >> 2) & 1) * 2 + (pos & 1)];
            const int t = (2 * k + 1) * 255;
            const int scale = 255 * 2 * n;
            for (int v = 0; v < 256; ++v) {
                five[m][pos][v] = uint8_t(std::min(31, (v * 31 * 2 * n + t) / scale));
                six[m][pos][v] = uint8_t(std::min(63, (v * 63 * 2 * n + t) / scale));
            }
        }
    }
}

const DitherTables kDitherTables;

} // namespace reluva::glide
