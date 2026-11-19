// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Voodoo's per-pixel units as plain functions (docs/common/graphics/glide.md,
// The pixel pipeline): the colour, alpha and texture combine units, the
// fog unit, the depth value, alpha blending and the dither to 5:6:5. The
// software rasterizer calls them for each pixel; they are kept apart so
// that tests and a later GPU backend can check against the same rules.

#include <bit>
#include <cstdint>

namespace reluva::glide {

struct Rgba {
    // 0-255
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

inline Rgba unpackArgb(uint32_t v) {
    return {int(v >> 16 & 255), int(v >> 8 & 255), int(v & 255), int(v >> 24)};
}
inline uint32_t packArgb(const Rgba &c) {
    return uint32_t(c.a) << 24 | uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | uint32_t(c.b);
}

// A value (-255..255) scaled by a factor (0..255): (v * (f + 1)) >> 8, the
// combine units' multiply (Voodoo2 specification, fbzColorPath and
// textureMode figures: the factor plus one, the low bits truncated, not
// rounded). Factor 255 gives v, 0 gives 0 for |v| < 256.
inline int scaleBy(int v, int factor) {
    return (v * (factor + 1)) >> 8;
}

// One unit's settings (grColorCombine, grAlphaCombine, grTexCombine).
struct CombineSettings {
    int function = 0; // CombineFunction
    int factor = 0;   // CombineFactor
    bool invert = false;
};

// The combine arithmetic for one channel: `local`, `other`, the unit's
// local alpha (for the *_ADD_LOCAL_ALPHA functions) and the factor's value
// (0-255), already chosen. The result clamped to 0-255, then inverted if
// asked.
int combineChannel(const CombineSettings &s, int local, int other, int localAlpha, int factor);

// The factor's value (0-255) for one channel of the colour or alpha
// combine unit (grColorCombine/grAlphaCombine's table): `local`, `otherAlpha`,
// `localAlpha` and the texture's colour channel and alpha. Unknown factors
// give 0.
int combineFactor(int factor, int local, int otherAlpha, int localAlpha, int textureAlpha, int textureChannel);

// The texture combine unit's factor: as above, with the detail factor and
// the LOD fraction for TEXTURE_ALPHA and TEXTURE_RGB.
int textureCombineFactor(int factor, int local, int otherAlpha, int localAlpha, int detail, int lodFraction);

// grColorCombine and grAlphaCombine together: the pixel's colour and alpha
// from the iterated colour, the texture's output, the constant colour and
// the iterated depth's high 8 bits (GR_COMBINE_LOCAL_DEPTH). `chromaOther`
// gets the colour combine's "other" colour, which chroma-keying compares.
struct PixelCombine {
    CombineSettings color;
    CombineSettings alpha;
    // CombineLocal/CombineOther
    int colorLocal = 0;
    int colorOther = 0;
    int alphaLocal = 0;
    int alphaOther = 0;
    bool alphaControlsLighting = false; // grAlphaControlsITRGBLighting
};
Rgba combinePixel(const PixelCombine &p, const Rgba &iterated, const Rgba &texture, const Rgba &constant, int depth8,
                  Rgba *chromaOther);

// One TMU's combine (grTexCombine): its own filtered texel (`local`) with
// what the TMU upstream of it gives (`other`).
Rgba combineTexture(const CombineSettings &rgb, const CombineSettings &alpha, const Rgba &local, const Rgba &other,
                    int detail, int lodFraction);

// The Voodoo's 16-bit floating-point depth from a 1/w (Voodoo2
// specification, fbzMode and its depth figure): the 1/w as 16.32 fixed
// point, a 4-bit exponent counting the leading zeros of its bits 31-16, the
// 12 bits after the leading one as the mantissa, inverted, so that larger w
// gives larger values. 1/w of 1 or more gives 0, below 2^-16 0xffff.
inline uint16_t floatDepth(double oow) {
    if (!(oow < 1.0))
        return 0;                             // integer bits set (or not a number): the nearest
    const double scaled = oow * 4294967296.0; // the fraction's 32 bits
    if (!(scaled >= 65536.0))
        return 0xffff; // below 2^-16: the farthest
    const uint32_t f = uint32_t(scaled);
    const int exponent = std::countl_zero(f);              // at most 15 here
    const uint32_t mantissa = (f << (exponent + 1)) >> 20; // the 12 bits after the leading one
    return uint16_t(exponent << 12 | (0xfff - mantissa));
}

// The fog table's blend factor for a pixel's floating-point w (as
// floatDepth gives it): the entry its top 6 bits name, moved towards the
// next by its next 8 bits (Voodoo2 specification, fogMode's figure; the
// last entry stays).
inline int fogTableFactor(const uint8_t table[64], uint16_t wfloat) {
    const int i = wfloat >> 10;
    const int frac = wfloat >> 2 & 0xff;
    const int here = table[i];
    const int next = i < 63 ? table[i + 1] : here;
    return here + (((next - here) * frac) >> 8);
}

// The fog unit: `c` moved towards `fogColor` by f (0-255) as grFogMode's
// mode says (kFogMult2, kFogAdd2 or the plain mix). Alpha untouched.
inline Rgba applyFog(const Rgba &c, const Rgba &fogColor, int f, int mode) {
    auto mix = [&](int in, int fog) {
        const int v = (mode & 0x100)   ? scaleBy(fog, f)
                      : (mode & 0x200) ? in - scaleBy(in, f)
                                       : in + scaleBy(fog - in, f);
        return v < 0 ? 0 : v > 255 ? 255 : v;
    };
    return {mix(c.r, fogColor.r), mix(c.g, fogColor.g), mix(c.b, fogColor.b), c.a};
}

// grAlphaBlendFunction's equation for one pixel: `src` (after fog) over
// `dst` (read back from the buffer), `prefog` the colour before fog (for
// GR_BLEND_PREFOG_COLOR as the destination factor).
Rgba blendPixel(const Rgba &src, const Rgba &dst, const Rgba &prefog, int rgbSrc, int rgbDst, int alphaSrc,
                int alphaDst);

// a `func` b (CmpFunc).
inline bool compare(int func, int a, int b) {
    switch (func) {
    case 0:
        return false; // never
    case 1:
        return a < b; // less
    case 2:
        return a == b; // equal
    case 3:
        return a <= b; // less or equal
    case 4:
        return a > b; // greater
    case 5:
        return a != b; // not equal
    case 6:
        return a >= b; // greater or equal
    default:
        return true; // always
    }
}

// 8-bit RGB to 5:6:5, truncated or ordered-dithered (DitherMode) at the
// pixel (x, y): each channel scaled to its bits, plus the matrix's
// threshold at the pixel, (2k + 1) / (2n) of a step for the n-entry matrix,
// then truncated. The matrices are the ordered (Bayer) ones Glide names;
// the Voodoo's own rounding of them isn't documented
// (docs/engines/nfs/games/nfs3/systems/thrash.md, Dithering). Tabulated once.
struct DitherTables {
    DitherTables();
    // [4x4, 2x2][(y & 3) * 4 + (x & 3)][level]
    uint8_t five[2][16][256];
    uint8_t six[2][16][256];
};
extern const DitherTables kDitherTables;

inline uint16_t to565(int r, int g, int b, int x, int y, int ditherMode) {
    if (ditherMode == 2 || ditherMode == 1) { // GR_DITHER_4x4, GR_DITHER_2x2
        const int m = ditherMode == 2 ? 0 : 1;
        const int pos = (y & 3) * 4 + (x & 3);
        const DitherTables &d = kDitherTables;
        return uint16_t(d.five[m][pos][r] << 11 | d.six[m][pos][g] << 5 | d.five[m][pos][b]);
    }
    return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

// 5:6:5 widened to 8 bits a channel by repeating the top bits, as the
// Voodoo widens 16-bit colours.
inline Rgba from565(uint16_t v) {
    const int r = v >> 11 & 31;
    const int g = v >> 5 & 63;
    const int b = v & 31;
    return {r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255};
}

} // namespace reluva::glide
