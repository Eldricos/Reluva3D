// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "image/image.h"
#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::botf {

/**
 * A .fnt font from stbof.res (docs/engines/botf/formats/fnt.md): a 260-byte header
 * (the line height, 120 advance widths) and a 16-bit TGA sheet holding the
 * 120 glyphs in framed cells, in rows of 10, 26, 26, 32 and 26.
 */
class Font {
public:
    /** How many glyphs the sheet holds. */
    static constexpr int kGlyphs = 120;
    /** The character of each glyph, in sheet order. */
    static const std::array<char32_t, kGlyphs> kCharacters;

    /** A glyph's place in the sheet and its width on the line. */
    struct Glyph {
        // the cell's inside, in the sheet
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        int advance = 0; // from the header
    };

    /** Reads the header and finds each glyph's framed cell in the sheet; fails if a row hasn't its count of cells. */
    Error load(const std::vector<uint8_t> &data);

    /** The sheet's row pitch, in pixels. */
    int lineHeight() const { return _lineHeight; }
    /** A cell's height inside its frame. */
    int glyphHeight() const { return _lineHeight - 1; }
    /** How far a space moves the pen. */
    int spaceAdvance() const { return _spaceAdvance; }
    /** The glyph sheet, as RGBA32. */
    const image::Image &sheet() const { return _sheet; }
    /** Every glyph, in kCharacters' order. */
    const std::array<Glyph, kGlyphs> &glyphs() const { return _glyphs; }
    /**
     * The glyph for a character, or nullptr (space has none: it advances
     * spaceAdvance()).
     */
    const Glyph *glyph(char32_t c) const;

    /**
     * The width of UTF-8 text in pixels; characters without a glyph count as
     * spaces.
     */
    int measure(std::string_view utf8) const;
    /**
     * Draws UTF-8 text with its top-left corner at (x, y) into an RGBA32
     * surface, clipped to it: each glyph's pixels as they are, its
     * background left out; with `tint`, multiplied by it. Returns the x after
     * the text.
     */
    int draw(Surface &surface, int x, int y, std::string_view utf8, std::optional<Color> tint = std::nullopt,
             int scale = 1) const;
    /**
     * Draws the text in a colour, as the game's screens do: the glyphs are
     * shades of grey, and the brightest shade in the sheet comes out as
     * `color`, the others in proportion.
     */
    int drawInColor(Surface &surface, int x, int y, std::string_view utf8, Color color) const;
    /**
     * Splits UTF-8 text into lines at most `width` pixels wide, breaking at
     * spaces and newlines; a word wider than a line gets a line of its own.
     */
    std::vector<std::string> wrap(std::string_view utf8, int width) const;

private:
    image::Image _sheet;
    std::array<Glyph, kGlyphs> _glyphs{};
    int _lineHeight = 0;
    int _spaceAdvance = 0;
    uint32_t _background = 0; // the sheet's background colour, RGBA as stored
    int _brightest = 255;     // the brightest channel of any glyph pixel

    int drawImpl(Surface &surface, int x, int y, std::string_view utf8, std::optional<Color> color, bool shaded,
                 int scale) const;
};

} // namespace reluva::botf
