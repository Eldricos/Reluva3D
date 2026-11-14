// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3::raven {

// Raven's fonts (Jedi Outcast's fonts/<name>.fontdat, each over the image
// fonts/<name>.tga; see docs/engines/idtech3/formats/fonts.md): where each
// of 256 characters is in the image and how it's placed on a line.
struct FontGlyph {
    int width = 0; // in the image, in pixels
    int height = 0;
    int advance = 0;  // to the next character
    int offset = 0;   // from the pen to its left edge
    int baseline = 0; // from its top to the line it stands on
    float s = 0;
    float t = 0;
    float s2 = 0;
    float t2 = 0;
};

struct Font {
    std::string name;
    std::array<FontGlyph, 256> glyphs{};
    int pointSize = 0; // a line's height
    int height = 0;
    int ascender = 0;
    int descender = 0;
    // Sizes rounded to whole pixels as they're scaled, as the original
    // does for its "ergo" fonts only.
    bool roundSizes = false;

    // A character's, or a full stop's for one it hasn't (no width).
    const FontGlyph &glyph(uint8_t c) const;
    // The widest line's width, `scale` times the font's own (colour codes
    // count as letters, as the original measures them).
    int textWidth(std::string_view text, float scale) const;

    // Where each letter of `text` goes, as the original draws it from
    // (x, y), its top left: a picture of the image's (s, t)-(s2, t2) at
    // (x, y), w by h; `colour` the ^ code before it (-1: none yet). Stops
    // at the first letter or space that would pass `maxWidth` (-1: none),
    // as the single-player engine does, or after `maxChars` letters and
    // spaces (-1: none), as the multiplayer engine does.
    struct Piece {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        float s = 0;
        float t = 0;
        float s2 = 0;
        float t2 = 0;
        int colour = -1;
    };
    std::vector<Piece> layout(std::string_view text, int x, int y, float scale, int maxWidth = -1,
                              int maxChars = -1) const;
};

// A .fontdat file: 256 glyphs of 28 bytes, then the sizes.
Error parseFontdat(const std::vector<uint8_t> &data, const std::string &name, Font &out);

} // namespace reluva::idtech3::raven
