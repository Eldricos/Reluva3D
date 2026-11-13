// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The parts of Glide's utility library that work on host data
// (docs/common/graphics/glide.md, The utility library): .3df texture files
// (gu3dfGetInfo, gu3dfLoad), the RLE16 image encoding grLfbWriteRegion
// reads (guEncodeRLE16), and the endian swaps.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::glide {

// A .3df file: TexUS's format, as the TexUS 2.01 guide (3Dfx Interactive,
// "Texus V2.01", December 1996, section 4, "3df File Format") describes
// it: ASCII lines ("3df v<major>.<minor>", "#" comments, "color format:
// <name>", "lod range: <min> <max>", "aspect ratio: <w> <h>"), then the
// levels' texels.
struct File3df {
    int format = 0; // GR_TEXFMT_*
    // log2 of each level's longer side
    int smallLodLog2 = 0;
    int largeLodLog2 = 0;
    int aspectLog2 = 0; // log2(width / height)
    // the largest level's
    int width = 0;
    int height = 0;
    uint32_t dataBytes = 0; // every level, largest first, rows packed
    size_t dataOffset = 0;  // where the texels start in the file
};

// Reads a .3df file's header; false (and why in `error`) if it isn't one
// Reluva can read.
bool parse3dfHeader(const uint8_t *file, size_t size, File3df &out, std::string *error = nullptr);
// The texels as Glide wants them in memory: 16-bit texels to the host's
// little-endian order (the file has "the leftmost byte in the color
// specification" first). False if the file is short.
bool load3dfData(const uint8_t *file, size_t size, const File3df &info, uint8_t *out);

// guEncodeRLE16: a 16-bit image (`width` by `height`, rows packed) as
// grLfbWriteRegion's GR_LFB_SRC_FMT_RLE16 reads it: runs of one colour, a
// signed 16-bit count then the 16-bit colour (Glide 2.4 Reference Manual,
// grLfbWriteRegion). Runs stop at each row's end. Writes to `dst` if
// given; returns the bytes it takes.
uint32_t encodeRle16(uint8_t *dst, const uint16_t *src, uint32_t width, uint32_t height);

// guEndianSwapWords and guEndianSwapBytes, as their names say: a 32-bit
// value's two 16-bit halves exchanged, a 16-bit value's two bytes.
inline uint32_t endianSwapWords(uint32_t v) {
    return v >> 16 | v << 16;
}
inline uint16_t endianSwapBytes(uint16_t v) {
    return uint16_t(v >> 8 | v << 8);
}

} // namespace reluva::glide
