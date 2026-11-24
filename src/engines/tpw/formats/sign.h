// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * How an attraction's sign is lettered (`.sgn`, beside its model:
 * `rides/robot/robot.sgn`): its name in two lines, each in a TrueType
 * font of the game's (`fonts.wad`) at a size and place, filled with a
 * picture of its colours. See
 * docs/engines/tpw/games/simcoaster/formats/sgn.md.
 */
class SignStyle {
public:
    /** A line's lettering. */
    struct Line {
        int top = 0;       // on a canvas kCanvasHeight high, twice as wide
        int height = 0;    // the letters' em, in its pixels
        int width = 0;     // the font description's: 0 for the font's own
        int weight = 400;  // 400 regular, 700 bold
        int stretch = 100; // how wide, percent
        std::string face;  // its font's name, which may not be fontName()'s
        image::Image fill; // stretched over its letters: 128 × 16, a ramp of colours top to bottom
    };
    /** The canvas the lines are laid out on: this high, twice as wide. */
    static constexpr int kCanvasHeight = 256;

    /** Reads the file; fails if it's cut short or isn't a sign's (101). */
    Error parse(const std::vector<uint8_t> &data);

    /** The font's file in `fonts.wad`: "VICTP___.TTF". */
    const std::string &fontFile() const { return _fontFile; }
    /** The font's name: "Victorian Plain". */
    const std::string &fontName() const { return _fontName; }
    const std::array<Line, 2> &lines() const { return _lines; }

private:
    std::string _fontFile;
    std::string _fontName;
    std::array<Line, 2> _lines{};
};

} // namespace reluva::tpw
