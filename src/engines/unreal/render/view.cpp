// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/render/view.h"

#include "graphics/font.h"

namespace reluva::unreal {


void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    fill(0, 0, ctx_.screen.w, ctx_.screen.h, r, g, b);
}

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface &s = ctx_.screen;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string &s, Color c) {
    font::drawText(ctx_.screen, x, y, s, c);
}

void Page::setTitle(const std::string &detail) {
    ctx_.sys.setWindowTitle(std::string("Reluva - ") + ctx_.game.name + " - " + name() + ": " + detail);
}

} // namespace reluva::unreal
