// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/draw.h"

#include <algorithm>
#include <cstring>

namespace reluva::draw {

void create(Surface& s, int width, int height, uint8_t color) {
    s.create(width, height, PixelFormat::CLUT8);
    if (color)
        fill(s, color);
}

void fill(Surface& s, uint8_t color) { std::fill(s.pixels.begin(), s.pixels.end(), color); }

void fillRect(Surface& s, int x, int y, int w, int h, uint8_t color) {
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    if (x1 <= x0)
        return;
    for (int yy = y0; yy < y1; yy++)
        std::memset(at(s, x0, yy), color, size_t(x1 - x0));
}

void frameRect(Surface& s, int x, int y, int w, int h, uint8_t color) {
    fillRect(s, x, y, w, 1, color);
    fillRect(s, x, y + h - 1, w, 1, color);
    fillRect(s, x, y, 1, h, color);
    fillRect(s, x + w - 1, y, 1, h, color);
}

void blit(Surface& dst, const Surface& src, int x, int y, int transparent) {
    const int sx0 = std::max(0, -x), sy0 = std::max(0, -y);
    const int sx1 = std::min(src.w, dst.w - x), sy1 = std::min(src.h, dst.h - y);
    if (sx0 >= sx1 || sy0 >= sy1)
        return;
    for (int sy = sy0; sy < sy1; sy++) {
        const uint8_t* s = at(src, sx0, sy);
        uint8_t* d = at(dst, x + sx0, y + sy);
        if (transparent < 0) {
            std::memcpy(d, s, size_t(sx1 - sx0));
        } else {
            for (int i = 0; i < sx1 - sx0; i++)
                if (s[i] != transparent)
                    d[i] = s[i];
        }
    }
}

} // namespace reluva::draw
