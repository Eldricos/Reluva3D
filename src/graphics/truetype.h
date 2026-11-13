// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::ttf {

// A TrueType font (a game's own .ttf), drawn anti-aliased into RGBA32
// surfaces. Uses stb_truetype (third_party/stb). Glyphs are rendered once
// per size and kept.
class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    Error load(std::vector<uint8_t> data);
    bool loaded() const { return !data_.empty(); }

    // Sizes are in pixels from the top of the tallest letters to the bottom
    // of the lowest (the font's ascent minus its descent).
    int lineHeight(float pixels) const;
    // The size (as above) of a font of `points`, as Windows makes it at 96
    // pixels an inch: an em of points x 96 / 72 pixels.
    float sizeForPoints(float points) const;
    int ascent(float pixels) const;
    int width(std::string_view utf8, float pixels);

    // Draws UTF-8 text with the top of its line at (x, y), clipped to the
    // surface, blended by its coverage times `alpha` (the surface's alpha
    // raised as it is, so it can be an overlay); returns the x after it.
    int draw(Surface& dst, int x, int y, std::string_view utf8, float pixels, Color color, uint8_t alpha = 255);

    // The text broken into lines no wider than `maxWidth`, at spaces where
    // it can be (a word too long for a line is left whole), and at line
    // ends.
    std::vector<std::string> wrap(std::string_view utf8, float pixels, int maxWidth);

private:
    struct Glyph {
        int width = 0, height = 0, xoff = 0, yoff = 0;
        float advance = 0;
        std::vector<uint8_t> coverage;
    };
    const Glyph& glyph(uint32_t codepoint, float pixels);
    float scale(float pixels) const;

    std::vector<uint8_t> data_;
    struct Info;
    std::unique_ptr<Info> info_;
    int ascent_ = 0, descent_ = 0, lineGap_ = 0;
    std::map<std::pair<uint32_t, int>, Glyph> glyphs_; // (code point, size in 1/16 pixel)
};

} // namespace reluva::ttf
