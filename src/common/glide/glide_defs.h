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

// --- Glide 3 (glide3x.dll) ------------------------------------------------------
// Values Glide 3 adds or changes, from the headers of 3dfx's Glide 3.x SDK
// for Linux (glide.h, as packaged in Slackware 7.1's
// contrib/3dfx/sdk3x.tgz; only the headers were read), named as the Glide
// 3.0 Reference Manual names them. LODs and aspect ratios are already log2
// (GR_LOD_LOG2_256 is 8, GR_ASPECT_LOG2_8x1 3), the core's own form.
namespace g3 {

// grVertexLayout's parameters.
enum Param : uint32_t {
    kParamXy = 0x01,
    kParamZ = 0x02,
    kParamW = 0x03,
    kParamQ = 0x04,
    kParamFogExt = 0x05,
    kParamA = 0x10,
    kParamRgb = 0x20,
    kParamPargb = 0x30,
    kParamSt0 = 0x40, // ST1, ST2 follow
    kParamQ0 = 0x50,  // Q1, Q2 follow
};
enum ParamMode : uint32_t { kParamDisable = 0, kParamEnable = 1 };
// grDrawVertexArray's modes.
enum Primitive : uint32_t {
    kPoints = 0,
    kLineStrip = 1,
    kLines = 2,
    kPolygon = 3,
    kTriangleStrip = 4,
    kTriangleFan = 5,
    kTriangles = 6,
    kTriangleStripContinue = 7,
    kTriangleFanContinue = 8,
};
// grEnable/grDisable.
enum EnableMode : uint32_t {
    kAaOrdered = 0x01,
    kAllowMipmapDither = 0x02,
    kPassthru = 0x03,
    kShamelessPlug = 0x04,
    kVideoSmoothing = 0x05,
};
enum CoordinateSpace : uint32_t { kWindowCoords = 0x00, kClipCoords = 0x01 };
// grFogMode: the table indexed by q (Glide 2's GR_FOG_WITH_TABLE), the
// iterated z as before, the iterated alpha moved to an extension number,
// and the fog coordinate extension.
enum FogMode : int {
    kFogWithTableOnFogCoordExt = 0x1,
    kFogWithTableOnQ = 0x2,
    kFogWithIteratedZ = 0x3,
    kFogWithIteratedAlphaExt = 0x4,
};
// grTexMultibaseAddress's ranges, renumbered from Glide 2's.
enum TexBase : int { kTexBase32To1 = 0x0, kTexBase64 = 0x1, kTexBase128 = 0x2, kTexBase256 = 0x3 };
// grGet's queries.
enum Query : uint32_t {
    kBitsDepth = 0x01,
    kBitsRgba = 0x02,
    kFifoFullness = 0x03,
    kFogTableEntries = 0x04,
    kGammaTableEntries = 0x05,
    kGlideStateSize = 0x06,
    kGlideVertexLayoutSize = 0x07,
    kIsBusy = 0x08,
    kLfbPixelPipe = 0x09,
    kMaxTextureSize = 0x0a,
    kMaxTextureAspectRatio = 0x0b,
    kMemoryFb = 0x0c,
    kMemoryTmu = 0x0d,
    kMemoryUma = 0x0e,
    kNumBoards = 0x0f,
    kNonPowerOfTwoTextures = 0x10,
    kNumFb = 0x11,
    kNumSwapHistoryBuffer = 0x12,
    kNumTmu = 0x13,
    kPendingBufferSwaps = 0x14,
    kRevisionFb = 0x15,
    kRevisionTmu = 0x16,
    kStatsLines = 0x17,
    kStatsPixelsAfuncFail = 0x18,
    kStatsPixelsChromaFail = 0x19,
    kStatsPixelsDepthfuncFail = 0x1a,
    kStatsPixelsIn = 0x1b,
    kStatsPixelsOut = 0x1c,
    kStatsPixels = 0x1d,
    kStatsPoints = 0x1e,
    kStatsTrianglesIn = 0x1f,
    kStatsTrianglesOut = 0x20,
    kStatsTriangles = 0x21,
    kSwapHistory = 0x22,
    kSupportsPassthru = 0x23,
    kTextureAlign = 0x24,
    kVideoPosition = 0x25,
    kViewport = 0x26,
    kWdepthMinMax = 0x27,
    kZdepthMinMax = 0x28,
    kVertexParameter = 0x29,
    kBitsGamma = 0x2a,
};
// grGetString's.
enum StringQuery : uint32_t {
    kExtension = 0xa0,
    kHardware = 0xa1,
    kRenderer = 0xa2,
    kVendor = 0xa3,
    kVersion = 0xa4,
};
constexpr uint32_t kQueryAny = 0xffffffffu; // GR_QUERY_ANY

} // namespace g3

} // namespace reluva::glide
