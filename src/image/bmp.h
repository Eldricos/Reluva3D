// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/palette.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::image {

// A Windows bitmap as it is stored: palette indices with the palette, for
// 1-, 4- and 8-bit ones (RLE8 compressed too), which games draw with their
// own palettes; others (16-, 24- and 32-bit) as RGBA32. image::decode
// (image.h) gives every BMP as RGBA instead.
struct Bitmap {
    Surface surface;       // CLUT8 or RGBA32, rows top to bottom
    Palette palette;       // for CLUT8: the file's colours, the rest black
    int paletteColors = 0; // how many the file holds (0 for RGBA32)
    bool indexed() const { return surface.format == PixelFormat::CLUT8; }
};

// A .bmp file (its BITMAPFILEHEADER first).
Error decodeBmp(const uint8_t *data, size_t size, Bitmap &out);
inline Error decodeBmp(const std::vector<uint8_t> &data, Bitmap &out) {
    return decodeBmp(data.data(), data.size(), out);
}
// A bitmap without the file header, from its BITMAPINFOHEADER on, with the
// pixels straight after the palette (as resources in executables keep it).
Error decodeDib(const uint8_t *data, size_t size, Bitmap &out);

} // namespace reluva::image
