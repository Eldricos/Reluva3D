// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"
#include "common/stream.h"

#include <string>
#include <vector>

namespace reluva::stharbinger {

/**
 * The game's bitmap fonts (`art\short.fnt` for the conversation menu,
 * `art\han.fnt`). Format: u16 count, then for each character from 0x20:
 * u8 height, u8 width (the advance), u8 length, then `length` bytes of
 * 1-bit rows, most significant bit first, each row starting on a byte.
 * A character the font lacks is drawn as the space.
 */
class Font {
public:
    /** Reads a .fnt; false, leaving the font empty, if it's cut short or has no or over 224 characters. */
    bool load(SeekableReadStream &stream);
    /** Whether no font has been loaded. */
    bool empty() const { return _glyphs.empty(); }

    /** The height of the space character. */
    int getLineHeight() const { return _glyphs.empty() ? 0 : _glyphs[0].height; }
    /** How wide `text` is, in pixels. */
    int getStringWidth(const std::string &text) const;

    /** Draws `text` with a drop shadow one pixel right and down, as DEEP.EXE does. */
    void drawString(Surface &dst, int x, int y, const std::string &text, uint8_t color, uint8_t shadow) const;

private:
    struct Glyph {
        int height = 0;
        int width = 0;
        int pitch = 0;
        std::vector<uint8_t> bits;
    };

    const Glyph *getGlyph(char c) const;
    void drawGlyph(Surface &dst, int x, int y, const Glyph &g, uint8_t color, bool shadow) const;

    std::vector<Glyph> _glyphs;
};

} // namespace reluva::stharbinger
