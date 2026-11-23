// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::tpw {

/**
 * A font (`.bf4`, "F4FB"), as SimCoaster's `Data/Language/American/MENUBIG.bf4`:
 * a glyph for each code of the language's character set. See
 * docs/engines/tpw/games/simcoaster/formats/bf4.md.
 */
class Font {
public:
    /** A character's picture and where it goes. */
    struct Glyph {
        char32_t character = 0; // what it draws: '*' where the font hasn't the code's own
        int width = 0;
        int height = 0;
        int left = 0;    // from the pen, rightwards; may be negative
        int top = 0;     // from the line's top, downwards; may be negative
        int advance = 0; // to the next character's pen
        /**
         * Each pixel's coverage, 0 (none) to 15 (whole), row by row; empty
         * for a glyph of a storage Reluva doesn't read yet (`readable` false).
         */
        std::vector<uint8_t> coverage;
        bool readable = true;
    };

    /** Reads the font. Fails without "F4FB", or on a glyph past the end. */
    Error parse(const std::vector<uint8_t> &data);

    /** The glyph for a code of the character set (from 1), or nullptr. */
    const Glyph *glyph(uint8_t code) const { return code > 0 && code <= _glyphs.size() ? &_glyphs[code - 1] : nullptr; }
    /** Every glyph, for codes 1 on. */
    const std::vector<Glyph> &glyphs() const { return _glyphs; }
    /** The header's byte 4 (1, or 2 for two fonts) and byte 5: not worked out. */
    ///@{
    uint8_t kind() const { return _kind; }
    uint8_t size() const { return _size; }
    ///@}

private:
    std::vector<Glyph> _glyphs;
    uint8_t _kind = 0;
    uint8_t _size = 0;
};

} // namespace reluva::tpw
