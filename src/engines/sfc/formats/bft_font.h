// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::sfc {

/**
 * An anti-aliased bitmap font, an `.bft` in `Assets/aafonts/`
 * (docs/engines/sfc/games/sfc2/formats/bft.md): a glyph for each
 * character from 0 up, each an 8-bit coverage bitmap.
 */
class BftFont {
public:
    /** One character's picture, and where it goes. */
    struct Glyph {
        int x = 0;       // from the pen to the bitmap's left
        int y = 0;       // from the line's top to the bitmap's top
        int advance = 0; // how far the pen moves
        int width = 0;
        int height = 0;
        std::vector<uint8_t> coverage; // width x height, 0 (none) to 255 (all), rows top down
    };

    /** Reads a whole file; anything after the last glyph is an error. */
    Error read(const std::vector<uint8_t> &data);

    /** The distance from one line's top to the next's. */
    int lineHeight() const { return _lineHeight; }
    /** The header's second number: half the line height in every retail font, its use unknown. */
    int half() const { return _half; }
    /** The glyphs, by character code (Windows-1252). */
    const std::vector<Glyph> &glyphs() const { return _glyphs; }

    /** The width the text takes: the sum of its characters' advances. */
    int textWidth(std::string_view text) const;
    /**
     * Draws text (one byte a character) into an RGBA32 surface from the
     * line's top left at (x, y), blending `color` by each pixel's coverage;
     * clipped to the surface. Returns the pen's x after it.
     */
    int draw(Surface &surface, int x, int y, std::string_view text, Color color) const;

private:
    int _lineHeight = 0;
    int _half = 0;
    std::vector<Glyph> _glyphs;
};

} // namespace reluva::sfc
