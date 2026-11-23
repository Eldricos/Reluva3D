// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "common/system.h"
#include "image/image.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::stawayteam {

/**
 * One of the game's fonts (`Cache/Fonts/<name>.seq16`, a `CFont`): a
 * picture for each character, drawn one after another with nothing
 * between them (docs/engines/stawayteam/systems/menus.md).
 */
class Font {
public:
    /** Reads the font `name` ("Placar Condensed") from DATA.dat. */
    Error load(const Archive &data, const std::string &name);

    /** Whether it has been loaded. */
    bool loaded() const { return !_glyphs.empty(); }

    /** How wide `text` is drawn, in pixels. */
    int width(std::string_view text) const;
    /** A line's height: the font's sequence's bounds. */
    int height() const { return _height; }
    /** The row under its characters' baseline, from their top: where a shortcut's line goes. */
    int baseline() const { return _baseline; }

    /**
     * Draws `text`, its first character's top left at (x, y).
     * @param opacity 0-255: how much it shows over what's there.
     * @param brightness 0-1: its colours scaled, as the toolkit dims what it draws.
     */
    void draw(Surface &screen, int x, int y, std::string_view text, int opacity = 255, float brightness = 1.0f) const;

    /** How text is lined up across: on its left, its middle or its right. */
    enum class Align { kLeft, kCenter, kRight };
    /**
     * Draws `text` broken into lines no wider than `maxWidth` (0: no
     * limit), at word boundaries, each lined up on `x` as `align` says, the
     * lines the font's height and `spacing` apart (0x4b16f0); the first
     * line's top at `y`.
     */
    void drawWrapped(Surface &screen, int x, int y, int maxWidth, std::string_view text, Align align = Align::kCenter,
                     int spacing = 0) const;
    /**
     * `text` broken into lines as drawWrapped breaks it, and at its line
     * breaks; the first `narrowLines` of them `indent` narrower.
     */
    std::vector<std::string> wrap(std::string_view text, int maxWidth, int indent = 0, int narrowLines = 0) const;

private:
    // The picture drawn for character `c`, or nullptr for none.
    const image::Image *glyph(unsigned char c) const;

    std::vector<image::Image> _glyphs; // from First ASCII on
    int _first = 0;
    int _last = 0;
    int _height = 0;
    int _baseline = 0;
};

} // namespace reluva::stawayteam
