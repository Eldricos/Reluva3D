// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/colormap.h"
#include "image/image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace reluva::sith {

/**
 * A .bm picture: one or more cels of the same depth, 8-bit (with or
 * without a palette of its own) or 16-bit
 * (docs/engines/sith/games/jk/formats/bm.md). Fonts hold one too.
 */
struct Bitmap {
    /** One picture of the file, its pixels row by row (1 or 2 bytes each). */
    struct Cel {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> pixels;
    };

    /** 0 or 1 for an 8-bit picture drawn with another's palette, 2 for one with its own. */
    uint32_t paletteMode = 0;
    /** Where the picture goes: set on the menus' and the HUD's pieces, 0 on the rest. */
    int32_t x = 0;
    int32_t y = 0;
    /** The colour (an index, or a 16-bit value) drawn as transparent. */
    uint32_t transparentColor = 0;
    /** Header field 8: 1 in every 16-bit picture, 0 in the 8-bit ones. */
    uint32_t field8 = 0;
    int bitsPerPixel = 8;
    /** A 16-bit pixel's red, green and blue: how many bits each, and where. */
    ///@{
    std::array<uint32_t, 3> channelBits{};
    std::array<uint32_t, 3> channelShifts{};
    ///@}
    std::vector<Cel> cels;
    /** The picture's own palette (paletteMode 2). */
    std::optional<Palette> palette;
};

/** What a .bm file's fixed header takes, before its cels. */
constexpr size_t kBitmapHeaderSize = 128;

/**
 * Reads a .bm picture from the start of `data`.
 * @param used Set to the bytes it took, when not nullptr (a font's picture
 *        is the end of its file).
 */
Error parseBitmap(const uint8_t *data, size_t size, Bitmap &out, size_t *used = nullptr);

/** The same, for a whole file. */
inline Error parseBitmap(const std::vector<uint8_t> &data, Bitmap &out) {
    return parseBitmap(data.data(), data.size(), out);
}

/**
 * A cel as RGBA: 8-bit through the picture's own palette or else
 * `palette`, 16-bit through its channels.
 * @param transparent Whether the transparent colour comes out with alpha 0.
 */
image::Image bitmapToImage(const Bitmap &bm, const Bitmap::Cel &cel, const Palette &palette, bool transparent);

} // namespace reluva::sith
