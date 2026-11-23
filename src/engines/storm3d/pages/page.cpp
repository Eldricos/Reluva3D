// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/page.h"

#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
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

void drawStatus(Surface &s, std::string_view text) {
    const int top = s.h - font::kLineHeight - 4;
    fillRect(s, 0, top, s.w, s.h - top, {16, 16, 24});
    font::drawText(s, 4, top + 2, text, {220, 220, 220});
}

void drawFitted(Surface &screen, const image::Image &img, bool alpha, int areaHeight) {
    if (img.w <= 0 || img.h <= 0)
        return;
    const double fit = std::min(double(screen.w) / img.w, double(areaHeight) / img.h);
    const double scale = fit >= 1 ? std::min(4.0, std::floor(fit)) : fit;
    const int w = std::max(1, int(img.w * scale));
    const int h = std::max(1, int(img.h * scale));
    const int x0 = (screen.w - w) / 2;
    const int y0 = (areaHeight - h) / 2;
    for (int y = 0; y < h; ++y) {
        uint8_t *out = screen.row(y0 + y) + size_t(x0) * 4;
        const int sy = std::min(img.h - 1, int(y / scale));
        for (int x = 0; x < w; ++x, out += 4) {
            const uint8_t *in = img.pixel(std::min(img.w - 1, int(x / scale)), sy);
            if (!alpha) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
            } else {
                const uint8_t check = ((x / 8 + y / 8) % 2) ? 96 : 160;
                for (int c = 0; c < 3; ++c)
                    out[c] = uint8_t((in[c] * in[3] + check * (255 - in[3])) / 255);
            }
            out[3] = 255;
        }
    }
}

size_t stepIndex(size_t index, int step, size_t count) {
    if (count == 0)
        return 0;
    const int64_t moved = int64_t(index) + step;
    return size_t(std::clamp<int64_t>(moved, 0, int64_t(count) - 1));
}

} // namespace reluva::storm3d
