// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/draw.h"

#include "graphics/font.h"

#include <algorithm>

namespace reluva::stawayteam {

void fill(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void clearPage(Surface &s) {
    fill(s, 0, 0, s.w, s.h - kStatusHeight, {40, 40, 48});
}

void drawStatus(Surface &s, const std::string &first, const std::string &second) {
    fill(s, 0, s.h - kStatusHeight, s.w, kStatusHeight, {0, 0, 0});
    font::drawText(s, 4, s.h - 24, first, {230, 230, 230});
    font::drawText(s, 4, s.h - 12, second, {180, 180, 190});
}

void drawBackdrop(Surface &s, int x, int y, int w, int h, int backdrop) {
    if (backdrop == 1) {
        for (int ty = 0; ty < h; ty += 8)
            for (int tx = 0; tx < w; tx += 8) {
                const uint8_t v = ((tx / 8 + ty / 8) % 2) ? 96 : 160;
                fill(s, x + tx, y + ty, std::min(8, w - tx), std::min(8, h - ty), {v, v, v});
            }
    } else if (backdrop == 2) {
        fill(s, x, y, w, h, {255, 0, 255});
    }
}

image::Image enlarged(const image::Image &img, int zoom) {
    if (zoom <= 1)
        return img;
    image::Image out;
    out.w = img.w * zoom;
    out.h = img.h * zoom;
    out.rgba.resize(size_t(out.w) * size_t(out.h) * 4);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            std::copy_n(img.pixel(x / zoom, y / zoom), 4, out.pixel(x, y));
    return out;
}

std::string folderOf(const std::string &name) {
    const size_t slash = name.rfind('/');
    return slash == std::string::npos ? "" : name.substr(0, slash);
}

} // namespace reluva::stawayteam
