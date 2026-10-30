// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::netimmerse {

// A LEGO Alpha Team font (.DDF): a Windows font drawn into TGA pages
// ("ARIAL_0.TGA", "ARIAL_1.TGA" beside "ARIAL.DDF"), white with the
// coverage in alpha, and where each character is on them. See
// docs/engines/netimmerse/formats/ddf.md.
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
    int lineHeight = 0, descent = 0;
    int emHeight = 0;     // probably the height less the internal leading
    int spaceAdvance = 0; // the space isn't drawn, but moves the pen this far
    std::vector<Glyph> glyphs; // first..last

    Error parse(const std::vector<uint8_t>& data);
    int ascent() const { return lineHeight - descent; }
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
