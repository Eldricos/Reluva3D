// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::nfs {

/**
 * A bitmap font (.ffn, "FNTF"), as docs/engines/nfs/games/nfs3/formats/ffn.md
 * describes: glyphs for Windows-1252 characters cut from one 4-bit coverage
 * atlas.
 */
struct Font {
    /** One character's picture in the atlas, and where it is drawn. */
    struct Glyph {
        int width = 0;
        int height = 0;
        // in the atlas
        int x = 0;
        int y = 0;
        int advance = 0; // pen movement after drawing
        // from the pen and the line's top
        int offsetX = 0;
        int offsetY = 0;
    };
    int lineHeight = 0;
    int lineGap = 0; // added to lineHeight between lines
    // added to the pen where drawing starts
    int penX = 0;
    int penY = 0;
    int flags = 0;                   // not understood yet
    std::array<Glyph, 256> glyphs{}; // by character code; width 0: none
    int atlasWidth = 0;
    int atlasHeight = 0;
    std::vector<uint8_t> coverage; // atlas pixels, 0 (clear) to 255 (solid)

    /** Whether the font has a glyph for that character. */
    bool has(uint8_t code) const { return glyphs[code].width > 0; }
    /** How far a new line moves down, as the game does it. */
    int lineStep() const { return lineHeight + lineGap; }
    /** The width of Windows-1252 text drawn in this font: its advances added up. */
    int measure(std::string_view text) const;
    /**
     * Draws Windows-1252 text with its top-left corner at (x, y), moved by
     * the pen offset, into an RGBA32 surface, blending by coverage, clipped
     * to it. The surface's alpha is left as it was.
     * @return The x after the last character, without the pen offset.
     */
    int draw(Surface &surface, int x, int y, std::string_view text, Color color) const;
};

/** Reads a font; fails if its atlas isn't 4-bit or a glyph lies outside it. */
Error parseFont(const std::vector<uint8_t> &data, Font &out);

} // namespace reluva::nfs
