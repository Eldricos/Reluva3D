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

class Hinter;

/**
 * A TrueType font (a game's own .ttf), drawn anti-aliased into RGBA32
 * surfaces. Uses stb_truetype (third_party/stb) for the outlines and their
 * coverage; a font with hinting programs is fitted to the pixel grid by
 * them first, at a whole number of pixels to the em, as Windows draws it
 * (truetype_hinting.h). Glyphs are rendered once per size and kept.
 *
 * Sizes (`pixels`) are in pixels from the top of the tallest letters to the
 * bottom of the lowest (the font's ascent minus its descent).
 */
class Font {
public:
    /** No font until load(). */
    Font();
    ~Font();
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

    /** Takes the font file (the first font of a collection); fails if it isn't TrueType. */
    Error load(std::vector<uint8_t> data);
    /** Whether a font was loaded. */
    bool loaded() const { return !_data.empty(); }

    /** From one line to the next, the font's line gap included: whole pixels, rounded up. */
    int lineHeight(float pixels) const;
    /**
     * The size of a font of `points`, as Windows makes it at 96 pixels an
     * inch: an em of points x 96 / 72 pixels. `points` itself before a font
     * is loaded.
     */
    float sizeForPoints(float points) const;
    /** From the top of a line to its baseline, in whole pixels. */
    int ascent(float pixels) const;
    /** How far the text advances, in whole pixels rounded up. */
    int width(std::string_view utf8, float pixels);

    /**
     * Draws UTF-8 text with the top of its line at (x, y), clipped to the
     * surface, blended by its coverage times `alpha` (the surface's alpha
     * raised as it is, so it can be an overlay). Nothing on a surface
     * that isn't RGBA32.
     * @return The x after it.
     */
    int draw(Surface &dst, int x, int y, std::string_view utf8, float pixels, Color color, uint8_t alpha = 255);

    /**
     * The text broken into lines no wider than `maxWidth`, at spaces where
     * it can be (a word too long for a line is left whole), and at line
     * ends.
     */
    std::vector<std::string> wrap(std::string_view utf8, float pixels, int maxWidth);

private:
    struct Glyph {
        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        float advance = 0;
        std::vector<uint8_t> coverage;
    };
    const Glyph &glyph(uint32_t codepoint, float pixels);
    // The glyph hinted, if the hinter can: false to draw it as scaled.
    bool hintedGlyph(uint32_t codepoint, float pixels, Glyph &out);
    float scale(float pixels) const;

    std::vector<uint8_t> _data;
    struct Info;
    std::unique_ptr<Info> _info;
    std::unique_ptr<Hinter> _hinter; // null: the font isn't hinted
    int _ascent = 0;
    int _descent = 0;
    int _lineGap = 0;
    std::map<std::pair<uint32_t, int>, Glyph> _glyphs; // (code point, size in 1/16 pixel)
};

} // namespace reluva::ttf
