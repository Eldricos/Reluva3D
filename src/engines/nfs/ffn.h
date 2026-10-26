// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::nfs {

// A bitmap font (.ffn, "FNTF"), as docs/formats/nfs/ffn.md describes:
// glyphs for Windows-1252 characters cut from one 4-bit coverage atlas.
struct Font {
    struct Glyph {
        int width = 0, height = 0;
        int x = 0, y = 0;            // in the atlas
        int advance = 0;             // pen movement after drawing
        int offsetX = 0, offsetY = 0; // from the pen and the line's top
    };
    int lineHeight = 0;
    int flags = 0; // not understood yet
    std::array<Glyph, 256> glyphs{}; // by character code; width 0: none
    int atlasWidth = 0, atlasHeight = 0;
    std::vector<uint8_t> coverage; // atlas pixels, 0 (clear) to 255 (solid)

    bool has(uint8_t code) const { return glyphs[code].width > 0; }
    // The width of Windows-1252 text drawn in this font.
    int measure(std::string_view text) const;
    // Draws Windows-1252 text with its top-left corner at (x, y) into an
    // RGBA32 surface, blending by coverage, clipped to it; returns the x
    // after the last character.
    int draw(Surface& surface, int x, int y, std::string_view text, Color color) const;
};

Error parseFont(const std::vector<uint8_t>& data, Font& out);

} // namespace reluva::nfs
