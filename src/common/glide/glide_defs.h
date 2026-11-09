// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The values Glide's interface gives its modes, functions and factors
// (docs/engines/glide.md). They are interface facts, read from the
// constants in 3dfx's public Glide 2.x SDK header (glide.h) and named as the
// Glide 2.4 Reference Manual names them; no 3dfx code is used. Where Glide 2
// and Glide 3 give a value the same meaning (the combine units, blending,
// comparisons, depth and fog modes, texture formats) the pipeline core uses
// it as is. Where they differ (LODs and aspect ratios) the core has its own
// version-neutral form and each front end converts.

#include <cstdint>

namespace reluva::glide {

// grColorCombine, grAlphaCombine and grTexCombine: the function.
enum CombineFunction : int {
    kFuncZero = 0x0,
    kFuncLocal = 0x1,
    kFuncLocalAlpha = 0x2,
    kFuncScaleOther = 0x3, // also BLEND_OTHER
    kFuncScaleOtherAddLocal = 0x4,
    kFuncScaleOtherAddLocalAlpha = 0x5,
    kFuncScaleOtherMinusLocal = 0x6,
    kFuncScaleOtherMinusLocalAddLocal = 0x7, // also BLEND
    kFuncScaleOtherMinusLocalAddLocalAlpha = 0x8,
    kFuncScaleMinusLocalAddLocal = 0x9, // also BLEND_LOCAL
    kFuncScaleMinusLocalAddLocalAlpha = 0x10,
};

// ... the scale factor.
enum CombineFactor : int {
    kFactorZero = 0x0,
    kFactorLocal = 0x1,
    kFactorOtherAlpha = 0x2,
    kFactorLocalAlpha = 0x3,
    kFactorTextureAlpha = 0x4, // also DETAIL_FACTOR in the texture combine unit
    kFactorTextureRgb = 0x5,   // also LOD_FRACTION in the texture combine unit
    kFactorOne = 0x8,
    kFactorOneMinusLocal = 0x9,
    kFactorOneMinusOtherAlpha = 0xa,
    kFactorOneMinusLocalAlpha = 0xb,
    kFactorOneMinusTextureAlpha = 0xc, // also ONE_MINUS_DETAIL_FACTOR
    kFactorOneMinusLodFraction = 0xd,
};

// grColorCombine/grAlphaCombine: the local and other inputs.
enum CombineLocal : int { kLocalIterated = 0x0, kLocalConstant = 0x1, kLocalDepth = 0x2 };
enum CombineOther : int { kOtherIterated = 0x0, kOtherTexture = 0x1, kOtherConstant = 0x2 };

// grAlphaBlendFunction.
enum BlendFactor : int {
    kBlendZero = 0x0,
    kBlendSrcAlpha = 0x1,
    kBlendSrcColor = 0x2, // DST_COLOR as a source factor
    kBlendDstAlpha = 0x3,
    kBlendOne = 0x4,
    kBlendOneMinusSrcAlpha = 0x5,
    kBlendOneMinusSrcColor = 0x6, // ONE_MINUS_DST_COLOR as a source factor
    kBlendOneMinusDstAlpha = 0x7,
    kBlendAlphaSaturate = 0xf, // PREFOG_COLOR as a destination factor
};

// Comparisons (grAlphaTestFunction, grDepthBufferFunction).
enum CmpFunc : int { kCmpNever, kCmpLess, kCmpEqual, kCmpLequal, kCmpGreater, kCmpNotEqual, kCmpGequal, kCmpAlways };

enum DepthMode : int {
    kDepthDisable = 0x0,
    kDepthZBuffer = 0x1,
    kDepthWBuffer = 0x2,
    kDepthZCompareToBias = 0x3,
    kDepthWCompareToBias = 0x4,
};

enum DitherMode : int { kDitherDisable = 0x0, kDither2x2 = 0x1, kDither4x4 = 0x2 };

// grFogMode: one of the first four, optionally with kFogMult2 or kFogAdd2.
enum FogMode : int {
    kFogDisable = 0x0,
    kFogWithIteratedAlpha = 0x1,
    kFogWithTable = 0x2,
    kFogWithIteratedZ = 0x3,
    kFogMult2 = 0x100,
    kFogAdd2 = 0x200,
};

enum CullMode : int { kCullDisable = 0x0, kCullNegative = 0x1, kCullPositive = 0x2 };

// Packed colours as an application passes them (grSstWinOpen's cformat).
enum ColorFormat : int { kColorArgb = 0x0, kColorAbgr = 0x1, kColorRgba = 0x2, kColorBgra = 0x3 };

// Buffers (grRenderBuffer, grLfbLock...).
enum BufferId : int {
    kBufferFront = 0x0,
    kBufferBack = 0x1,
    kBufferAux = 0x2,
    kBufferDepth = 0x3,
    kBufferAlpha = 0x4,
    kBufferTriple = 0x5,
};

// Linear frame buffer write modes (grLfbLock), also grLfbWriteRegion's
// source formats (GR_LFB_SRC_FMT_*, the same numbers).
enum LfbWriteMode : int {
    kLfb565 = 0x0,
    kLfb555 = 0x1,
    kLfb1555 = 0x2,
    kLfb888 = 0x4,
    kLfb8888 = 0x5,
    kLfb565Depth = 0xc,
    kLfb555Depth = 0xd,
    kLfb1555Depth = 0xe,
    kLfbZA16 = 0xf,
    kLfbAny = 0xff,
    kLfbRle16 = 0x80, // grLfbWriteRegion only
};

// Texture formats (GR_TEXFMT_*; the same numbers in Glide 2 and 3).
enum TextureFormat : int {
    kTexRgb332 = 0x0,
    kTexYiq422 = 0x1,
    kTexAlpha8 = 0x2,
    kTexIntensity8 = 0x3,
    kTexAlphaIntensity44 = 0x4,
    kTexP8 = 0x5,
    kTexArgb8332 = 0x8,
    kTexAyiq8422 = 0x9,
    kTexRgb565 = 0xa,
    kTexArgb1555 = 0xb,
    kTexArgb4444 = 0xc,
    kTexAlphaIntensity88 = 0xd,
    kTexAp88 = 0xe,
};

enum TextureClamp : int { kClampWrap = 0x0, kClampClamp = 0x1 };
enum TextureFilter : int { kFilterPoint = 0x0, kFilterBilinear = 0x1 };
enum MipmapMode : int { kMipmapDisable = 0x0, kMipmapNearest = 0x1, kMipmapNearestDither = 0x2 };

// grTexDownloadMipMap's evenOdd: which levels are in this TMU. Even levels
// are those whose Glide 2 constant is even (GR_LOD_256, GR_LOD_64...), that
// is whose log2 size is even.
enum EvenOdd : int { kLevelsEven = 0x1, kLevelsOdd = 0x2, kLevelsBoth = 0x3 };

// grTexDownloadTable's table type.
enum TableType : int { kTableNcc0 = 0x0, kTableNcc1 = 0x1, kTablePalette = 0x2 };

// grHints' types and GR_HINT_STWHINT's bits.
enum HintType : int { kHintStw = 0, kHintFifoCheck = 1, kHintFpuPrecision = 2, kHintAllowMipmapDither = 3 };
enum StwHint : uint32_t {
    kStwWDiffFbi = 1u << 0,
    kStwWDiffTmu0 = 1u << 1,
    kStwStDiffTmu0 = 1u << 2,
    kStwWDiffTmu1 = 1u << 3,
    kStwStDiffTmu1 = 1u << 4,
    kStwWDiffTmu2 = 1u << 5,
    kStwStDiffTmu2 = 1u << 6,
};

} // namespace reluva::glide
