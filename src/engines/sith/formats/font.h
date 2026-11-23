// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/bitmap.h"

#include <cstdint>
#include <map>
#include <vector>

namespace reluva::sith {

/**
 * A .sft font: where each character's glyph is in a strip of glyphs side
 * by side, a .bm picture (docs/engines/sith/games/jk/formats/sft.md).
 */
struct Font {
    /** A glyph's place in the strip. */
    struct Glyph {
        int32_t x = 0;
        int32_t width = 0;
    };

    /** Pixels added after each glyph (0x4347d0). */
    int32_t gap = 0;
    /** How far white space moves the text, in pixels (0x43470a). */
    int32_t spaceWidth = 0;
    /** The character drawn for one the font lacks (0x434734). */
    uint16_t fallback = 0;
    /** Each character's glyph, by its code (Windows-1252 in the game's fonts). */
    std::map<uint16_t, Glyph> glyphs;
    /** The strip, in its first cel. */
    Bitmap strip;

    /** A character's glyph, or nullptr if the font hasn't one. */
    const Glyph *glyph(uint16_t code) const {
        auto it = glyphs.find(code);
        return it == glyphs.end() ? nullptr : &it->second;
    }
};

/** Reads a .sft file; fails if it's not one, or its glyphs fall outside the strip. */
Error parseFont(const std::vector<uint8_t> &data, Font &out);

} // namespace reluva::sith
