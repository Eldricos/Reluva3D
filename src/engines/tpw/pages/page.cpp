// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/events.h"
#include "common/fs.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

std::vector<std::string> ViewerContext::namesEndingIn(std::initializer_list<const char *> extensions) const {
    std::vector<std::string> out;
    for (const std::string &name : files.list()) {
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos)
            continue;
        const std::string ext = fs::toLower(name.substr(dot));
        for (const char *e : extensions)
            if (ext == e) {
                out.push_back(name);
                break;
            }
    }
    return out;
}

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(s.w, x + w);
    const int y1 = std::min(s.h, y + h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + x0 * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

int drawLabel(Surface &s, int x, int y, const std::string &utf8, Color c) {
    return font::drawText(s, x, y, utf8, c);
}

void drawPicture(Surface &s, const image::Image &picture, int x, int y, int zoom) {
    for (int py = 0; py < picture.h * zoom; ++py) {
        const int sy = y + py;
        if (sy < 0 || sy >= s.h)
            continue;
        for (int px = 0; px < picture.w * zoom; ++px) {
            const int sx = x + px;
            if (sx < 0 || sx >= s.w)
                continue;
            const uint8_t *src = picture.pixel(px / zoom, py / zoom);
            // A clear pixel shows a grey check, 8 screen pixels a square.
            const uint8_t check = ((sx / 8 + sy / 8) % 2) ? 96 : 64;
            uint8_t *dst = s.row(sy) + sx * 4;
            for (int i = 0; i < 3; ++i)
                dst[i] = uint8_t((src[i] * src[3] + check * (255 - src[3])) / 255);
            dst[3] = 255;
        }
    }
}

void Page::handleKey(uint32_t key) {
    const long at = long(_index);
    if (key == key::kLeft)
        select(at - 1);
    else if (key == key::kRight)
        select(at + 1);
    else if (key == key::kPageUp)
        select(at - 10);
    else if (key == key::kPageDown)
        select(at + 10);
    else if (key == key::kHome)
        select(0);
    else if (key == key::kEnd)
        select(long(count()) - 1);
}

void Page::select(long index) {
    if (count() == 0)
        return;
    const size_t next = size_t(std::clamp(index, 0L, long(count()) - 1));
    if (next == _index)
        return;
    _index = next;
    selected();
}

} // namespace reluva::tpw
