// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Drawing shared by the 2D viewer pages: their colours, text with the
// built-in font, and the title and keys lines.

#include "common/font.h"
#include "common/types.h"

#include <algorithm>
#include <string>

namespace reluva::nfs::page {

constexpr int kMargin = 16;
constexpr Color kBackground{16, 20, 32};
constexpr Color kTitleColor{255, 200, 64};
constexpr Color kLabelColor{150, 160, 180};
constexpr Color kTextColor{230, 230, 230};
constexpr Color kWarnColor{255, 120, 100};

inline void fill(Surface& s, int x0, int y0, int w, int h, Color c) {
    for (int y = std::max(y0, 0); y < std::min(y0 + h, s.h); ++y) {
        uint8_t* p = s.row(y) + std::max(x0, 0) * 4;
        for (int x = std::max(x0, 0); x < std::min(x0 + w, s.w); ++x, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

inline void clear(Surface& s) { fill(s, 0, 0, s.w, s.h, kBackground); }

// A line of text with the built-in font, cut to fit the screen.
inline void text(Surface& s, int x, int y, const std::string& utf8, Color c) {
    const size_t room = static_cast<size_t>(std::max(0, s.w - x - kMargin) / font::kAdvance);
    font::drawText(s, x, y, font::length(utf8) > room ? utf8.substr(0, room) : utf8, c);
}

// A page's title line and, at the bottom, its keys.
inline void frame(Surface& s, const std::string& title, const std::string& keys) {
    clear(s);
    text(s, kMargin, 8, title, kTitleColor);
    text(s, kMargin, s.h - font::kLineHeight - 4, keys + "   Tab: next page   Esc: launcher", kLabelColor);
}

} // namespace reluva::nfs::page
