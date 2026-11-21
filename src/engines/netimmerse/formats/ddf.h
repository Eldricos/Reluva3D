// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "image/image.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::netimmerse {

/**
 * A LEGO Alpha Team font (.DDF): a Windows font drawn into TGA pages
 * ("ARIAL_0.TGA", "ARIAL_1.TGA" beside "ARIAL.DDF"), white with the
 * coverage in alpha, and where each character is on them. See
 * docs/engines/netimmerse/games/legoalphateam/formats/ddf.md.
 */
struct DdfFont {
    /** Where a character is on its page and how it sits on the line, in its page's pixels. */
    struct Glyph {
        // its top-left corner on its page
        int x = 0;
        int y = 0;
        int above = 0;   // rows from its top down to the baseline
        int below = 0;   // rows under the baseline (negative: it ends above it)
        int left = 0;    // its left edge from the pen (negative: left of it)
        int right = 0;   // its right edge from the pen
        int advance = 0; // how far the pen moves after it
        int page = 0;
        /** Its size on its page. */
        ///@{
        int width() const { return right - left; }
        int height() const { return above + below; }
        ///@}
    };
    std::string name; // the Windows font's ("Arial")
    int pageWidth = 0;
    int pageHeight = 0;
    int pages = 0;
    // the characters drawn (Windows-1252)
    int first = 0;
    int last = -1;
    // Each page's characters are drawn at a size of their own (the second
    // page, 128 to 255, smaller): its rows above and below the baseline.
    // The game scales each page to the same height on screen.
    std::vector<int> pageAscent;
    std::vector<int> pageDescent;
    // the first page's: the font's line
    int lineHeight = 0;
    int descent = 0;
    // The space isn't in the font: the game's text window moves the pen by
    // a width of its own (UITextWin, 18/35 of the size it was made with).
    // The viewer's choice: a quarter of the line.
    int spaceAdvance = 0;
    std::vector<Glyph> glyphs; // first..last

    /** Reads a .DDF file, replacing what the font held; fails if it's cut short or out of range. */
    Error parse(const std::vector<uint8_t> &data);
    /** The first page's rows above the baseline. */
    int ascent() const { return lineHeight - descent; }
    /**
     * How much a page's glyphs are scaled to come out at the font's line
     * height, as the game scales them; 1 for a page the font doesn't have.
     */
    double pageScale(int page) const;
    /** The character's glyph, or nullptr if it isn't drawn (the space). */
    const Glyph *glyph(uint8_t c) const;
    /** A page image's file name, beside `file`: "ARIAL.DDF"'s page 1 is "ARIAL_1.TGA". */
    static std::string pageName(const std::string &file, int page);
    /** How wide the text (Windows-1252) is, in pixels. */
    int measure(std::string_view text) const;
    /**
     * Draws the text (Windows-1252) with the top of its line at (x, y), the
     * pages' alpha blending the colour over the surface. Only an RGBA32
     * surface is drawn on; on another the pen only moves.
     * @param pageImages The pages, by number; glyphs on a page not in it are left out.
     * @return The pen's x after the text.
     */
    int draw(Surface &surface, const std::vector<image::Image> &pageImages, int x, int y, std::string_view text,
             Color color) const;
};

} // namespace reluva::netimmerse
