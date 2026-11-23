// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/page.h"

#include "graphics/font.h"

#include <algorithm>

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

size_t stepIndex(size_t index, int step, size_t count) {
    if (count == 0)
        return 0;
    const int64_t moved = int64_t(index) + step;
    return size_t(std::clamp<int64_t>(moved, 0, int64_t(count) - 1));
}

} // namespace reluva::storm3d
