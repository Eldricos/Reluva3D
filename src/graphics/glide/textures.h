// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Glide's textures in a TMU's memory (docs/common/graphics/glide.md, Textures):
// their formats, how each texel widens to 32-bit ARGB, the size and
// place of each mipmap level. Version-neutral: a level is named by the
// log2 of its longer side (8 for 256, 0 for 1), the aspect ratio by
// log2(width / height) (3 for 8x1, -3 for 1x8).

#include <cstdint>

namespace reluva::glide {

struct TextureInfo {
    int smallLod = 0; // the smallest level in the mipmap
    int largeLod = 8; // the largest
    int aspect = 0;
    int format = 0;   // TextureFormat
};

// The Voodoo2's NCC table (narrow channel compression, the YIQ formats):
// sixteen Y values and four signed (r, g, b) offsets each for I and Q.
struct NccTable {
    uint8_t y[16] = {};
    int16_t i[4][3] = {};
    int16_t q[4][3] = {};
};

// 1 for the 8-bit formats, 2 for the 16-bit ones.
int texelBytes(int format);
// Whether Reluva can read the format (the reserved numbers aren't).
bool knownFormat(int format);

// A level's width and height in texels.
void levelSize(int lod, int aspect, int& width, int& height);
// Whether `lod` is among the levels `evenOdd` names.
bool levelIncluded(int lod, int evenOdd);
// A level's size in texture memory, in bytes. The Voodoo2 specification's
// table (section 10, Texture Memory Access) gives 16-bit levels in 8-byte
// units, a level's sides counted as at least 2 texels, and 8-bit levels
// half as much (so a level may start in the middle of a unit).
uint32_t levelBytes(int lod, int aspect, int format);
// Where level `lod` starts, in bytes from the texture's start address (the
// address of its largest level that `evenOdd` includes).
uint32_t levelOffset(const TextureInfo& info, int evenOdd, int lod);
// The memory the texture takes, rounded up to 8 bytes (grTexCalcMemRequired
// for kLevelsBoth, grTexTextureMemRequired otherwise).
uint32_t textureBytes(const TextureInfo& info, int evenOdd);

// One texel as stored (8 or 16 bits) widened to 0xAARRGGBB as the TMU
// widens it (Voodoo2 specification, textureMode's format table): fields
// repeated to fill 8 bits, palette and NCC lookups. `palette` holds 256
// 0x00RRGGBB entries.
uint32_t decodeTexel(int format, uint32_t raw, const uint32_t* palette, const NccTable& ncc);

} // namespace reluva::glide
