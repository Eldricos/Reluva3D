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

int drawGameText(Surface &s, const Font &font, const CharacterSet &characters, int x, int y, const std::string &utf8,
                 Color c) {
    // Each UTF-8 character to the set's code for it, as a game string holds it.
    std::map<char32_t, uint8_t> codes;
    for (size_t code = 1; code <= characters.size(); ++code)
        codes.emplace(characters.unicode(uint8_t(code)), uint8_t(code));
    for (size_t i = 0; i < utf8.size();) {
        char32_t ch = uint8_t(utf8[i]);
        int extra = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : ch >= 0xc0 ? 1 : 0;
        if (extra)
            ch &= 0x3f >> extra;
        ++i;
        for (; extra > 0 && i < utf8.size(); --extra, ++i)
            ch = ch << 6 | (uint8_t(utf8[i]) & 0x3f);
        const auto it = codes.find(ch);
        const Font::Glyph *g = it == codes.end() ? nullptr : font.glyph(it->second);
        if (!g)
            continue;
        for (int py = 0; py < g->height && !g->coverage.empty(); ++py)
            for (int px = 0; px < g->width; ++px) {
                const int sx = x + g->left + px;
                const int sy = y + g->top + py;
                const int cover = g->coverage[size_t(py * g->width + px)];
                if (cover == 0 || sx < 0 || sy < 0 || sx >= s.w || sy >= s.h)
                    continue;
                uint8_t *dst = s.row(sy) + sx * 4;
                dst[0] = uint8_t((c.r * cover + dst[0] * (15 - cover)) / 15);
                dst[1] = uint8_t((c.g * cover + dst[1] * (15 - cover)) / 15);
                dst[2] = uint8_t((c.b * cover + dst[2] * (15 - cover)) / 15);
            }
        x += g->advance;
    }
    return x;
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
