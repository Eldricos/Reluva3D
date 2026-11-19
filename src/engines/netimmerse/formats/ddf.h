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

// A LEGO Alpha Team font (.DDF): a Windows font drawn into TGA pages
// ("ARIAL_0.TGA", "ARIAL_1.TGA" beside "ARIAL.DDF"), white with the
// coverage in alpha, and where each character is on them. See
// docs/engines/netimmerse/games/legoalphateam/formats/ddf.md.
struct DdfFont {
    struct Glyph {
        int x = 0, y = 0; // its top-left corner on its page
        int above = 0;    // rows from its top down to the baseline
        int below = 0;    // rows under the baseline (negative: it ends above it)
        int left = 0;     // its left edge from the pen (negative: left of it)
        int right = 0;    // its right edge from the pen
        int advance = 0;  // how far the pen moves after it
        int page = 0;
        int width() const { return right - left; }
        int height() const { return above + below; }
    };
    std::string name; // the Windows font's ("Arial")
    int pageWidth = 0, pageHeight = 0, pages = 0;
    int first = 0, last = -1; // the characters drawn (Windows-1252)
    // Each page's characters are drawn at a size of their own (the second
    // page, 128 to 255, smaller): its rows above and below the baseline.
    // The game scales each page to the same height on screen.
    std::vector<int> pageAscent, pageDescent;
    int lineHeight = 0, descent = 0; // the first page's: the font's line
    // The space isn't in the font: the game's text window moves the pen by
    // a width of its own (UITextWin, 18/35 of the size it was made with).
    // The viewer's choice: a quarter of the line.
    int spaceAdvance = 0;
    std::vector<Glyph> glyphs; // first..last

    Error parse(const std::vector<uint8_t>& data);
    int ascent() const { return lineHeight - descent; }
    // How much a page's glyphs are scaled to come out at the font's line
    // height, as the game scales them.
    double pageScale(int page) const;
    // The character's glyph, or nullptr if it isn't drawn (the space).
    const Glyph* glyph(uint8_t c) const;
    // The page images' file names, beside `file` ("ARIAL.DDF").
    static std::string pageName(const std::string& file, int page);
    // How wide the text is, in Windows-1252.
    int measure(std::string_view text) const;
    // Draws the text (Windows-1252) with the top of its line at (x, y) on an
    // RGBA32 surface, the pages' alpha blending the colour over it; returns
    // the pen's x after it.
    int draw(Surface& surface, const std::vector<image::Image>& pageImages, int x, int y, std::string_view text,
             Color color) const;
};

} // namespace reluva::netimmerse
