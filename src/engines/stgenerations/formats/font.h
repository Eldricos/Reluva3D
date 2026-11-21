// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <vector>

#include "engines/stgenerations/canvas.h"
#include "graphics/draw.h"
#include "common/stream.h"
#include "common/text.h"

namespace reluva::stgenerations {

// A font's layout, worked out from ACTION.FNT and CREDITS.FNT:
//
//   char[4]  "FONT"
//   uint32   size of the rest of the file
//   uint16   number of glyphs
//   uint8    transparent colour index
//   uint8    line height
//   uint32   unknown, 0 in both fonts seen
//   glyphs, each:
//     uint16 character code
//     uint16 size of the rest of the glyph (= width * height + 2)
//     uint8  width
//     uint8  height (glyphs are top aligned; may be less than the line height)
//     uint8  pixels[height][width], palette indices

/**
 * A bitmap font in the game's .FNT format (e.g. COMMON.DAT:FONTS/ACTION.FNT).
 * A lower-case letter the font hasn't is drawn as its capital.
 */
class Font {
public:
    /** Reads a font; false (with a warning) if it isn't one, is damaged or has no glyphs. */
    bool load(SeekableReadStream &stream);

    /** Whether it has any glyphs. */
    bool isLoaded() const { return !_glyphs.empty(); }
    /** The line height, in pixels. */
    int getHeight() const { return _height; }
    /** The palette index of a glyph's see-through pixels. */
    uint8_t getTransparentColor() const { return _transparent; }
    /** How many glyphs, and whether one is for code `c` (without the capital's stand-in). */
    ///@{
    int getGlyphCount() const { return (int)_glyphs.size(); }
    bool hasGlyph(uint16_t c) const { return _glyphs.count(c) != 0; }
    ///@}

    /** Width in pixels; a character without a glyph is 0 wide. */
    ///@{
    int getCharWidth(uint16_t c) const;
    int getStringWidth(const std::string &str) const;
    ///@}

    /**
     * Draws text with its top left corner at (x, y) in the glyphs' own
     * colours. The glyphs have dark outline pixels, so drawing them all in
     * one colour makes text unreadable; instead, remap (if given) translates
     * each glyph colour index into the destination palette.
     */
    ///@{
    void drawChar(Canvas &dst, uint16_t c, int x, int y, const uint8_t *remap = nullptr) const;
    void drawString(Canvas &dst, const std::string &str, int x, int y, const uint8_t *remap = nullptr) const;
    ///@}

    /**
     * Draws text on a true-colour (ARGB8888) surface, each glyph colour
     * looked up in palette (0xAARRGGBB).
     */
    void drawStringRgb(Canvas &dst, const std::string &str, int x, int y, const uint32_t *palette,
                       const Rect *clip = nullptr) const;

    /**
     * Splits text into lines no wider than maxWidth, breaking at spaces. A
     * word wider than that is a line of its own.
     */
    std::vector<std::string> wordWrap(const std::string &str, int maxWidth) const;

private:
    struct Glyph {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels;
    };

    const Glyph *findGlyph(uint16_t c) const;

    std::map<uint16_t, Glyph> _glyphs;
    uint8_t _transparent = 0;
    int _height = 0;
};

} // namespace reluva::stgenerations
