// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "image/image.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The game's sprite font (`Sprites/font.spr` over `font.tga`): a sheet of
 * characters, 10 pixels high, each with its place and width, from the
 * space up ([formats/spr.md](docs/engines/storm3d/games/armada/formats/spr.md#fonts)).
 * Drawn a texel to a pixel, as the game's quads on whole pixels sample
 * it: where texels meet, so each pixel is the mean of four.
 */
class SpriteFont {
public:
    /** The characters' height, in pixels. */
    static constexpr int kHeight = 10;

    /**
     * Reads the font: `font.spr` over `font.tga`, or another of the same
     * layout, `title.spr` over `font256.tga`, whose sheet is twice as fine
     * and drawn twice the size.
     * @param name Its sprite's name in its table ("font", "font256"), and
     *        its texture's.
     * @return a failure if its table or sheet can't be read
     */
    Error load(const GameFiles &files, const std::string &table = "font.spr", const std::string &name = "font");
    /** How many pixels a texel of its table's 128 is: 1, or 2 for the title font. */
    int scale() const { return _scale; }
    bool loaded() const { return _sheet.w > 0; }
    /** How wide text is: each character's width, times its scale, and a pixel after it. */
    int width(std::string_view text) const;
    /**
     * Draws text from (x, y), its colour `tint` times the sheet's, over
     * what's there by the sheet's alpha; `opaque`: a texel a pixel, drawn
     * whole where its alpha is at least half, as the game's alpha test
     * does, not blended.
     */
    void draw(Surface &screen, int x, int y, std::string_view text, const std::array<uint8_t, 3> &tint,
              bool opaque = false) const;

private:
    image::Image _sheet;
    std::vector<std::array<int, 3>> _letters; // x, y and width on a 128 sheet, from the space up
    int _scale = 1;
};

} // namespace reluva::storm3d
