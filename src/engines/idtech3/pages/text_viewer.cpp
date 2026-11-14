// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/text_viewer.h"

#include "graphics/font.h"

#include <algorithm>

namespace reluva::idtech3 {

namespace {

// Quake III's colour codes, ^0 to ^7.
constexpr uint8_t kCodeColours[8][3] = {{0, 0, 0},   {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                        {0, 0, 255}, {0, 255, 255}, {255, 0, 255}, {255, 255, 255}};
constexpr const char *kLanguages[3] = {"English", "French", "German"};

} // namespace

TextViewer::TextViewer(PageContext &ctx) : Page(ctx) {
    _strings.load(ctx.files, &_warnings);
    std::vector<uint8_t> data;
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("fonts/", 0) != 0 || key.size() < 8 || key.compare(key.size() - 8, 8, ".fontdat") != 0)
            continue;
        LoadedFont f;
        const std::string base = key.substr(0, key.size() - 8);
        std::vector<uint8_t> picture;
        if (ctx.files.read(n, data) || raven::parseFontdat(data, base.substr(6), f.font))
            continue;
        const std::string image = findImage(ctx.files, base);
        if (image.empty() || ctx.files.read(image, picture) || image::decode(picture, f.image))
            continue;
        _fonts.push_back(std::move(f));
    }
}

void TextViewer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _strings.packages().empty())
        return;
    const int packages = int(_strings.packages().size());
    const int entries = int(_strings.packages()[size_t(_package)].entries.size());
    if (ev.key == key::kLeft || ev.key == key::kRight) {
        _package = (_package + (ev.key == key::kLeft ? packages - 1 : 1)) % packages;
        _first = 0;
    } else if (ev.key == key::kUp || ev.key == key::kDown) {
        _first = std::clamp(_first + (ev.key == key::kUp ? -1 : 1), 0, std::max(0, entries - 1));
    } else if (ev.key == key::kPageUp || ev.key == key::kPageDown) {
        _first = std::clamp(_first + (ev.key == key::kPageUp ? -10 : 10), 0, std::max(0, entries - 1));
    } else if (ev.key == 'l') {
        _language = raven::StringTable::Language((int(_language) + 1) % 3);
    } else if (ev.key == 'f' && !_fonts.empty()) {
        _font = (_font + 1) % int(_fonts.size());
    } else {
        return;
    }
    show();
}

int TextViewer::drawText(int x, int y, const std::string &text, int maxWidth) {
    Surface &s = _ctx.screen;
    if (_fonts.empty()) {
        this->text(x, y, text);
        return y + font::kLineHeight;
    }
    const LoadedFont &f = _fonts[size_t(_font)];
    // Broken at spaces into lines that fit, to read it all.
    std::string wrapped;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = std::min(text.find_first_of(" \n", at), text.size());
        const std::string word = text.substr(at, end - at);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.font.textWidth(tried, 1) > maxWidth) {
            wrapped += line + "\n";
            line = word;
        } else {
            line = tried;
        }
        if (end < text.size() && text[end] == '\n') {
            wrapped += line + "\n";
            line.clear();
        }
        at = end + 1;
    }
    wrapped += line;
    int bottom = y;
    // Each letter's part of the font's image, stretched to its place, its
    // white tinted by its colour code's colour, over what's there.
    for (const raven::Font::Piece &p : f.font.layout(wrapped, x, y, 1)) {
        const uint8_t *tint = p.colour < 0 ? kCodeColours[7] : kCodeColours[p.colour];
        const int w = int(p.w);
        const int h = int(p.h);
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                const int dx = int(p.x) + px;
                const int dy = int(p.y) + py;
                if (dx < 0 || dy < 0 || dx >= s.w || dy >= s.h)
                    continue;
                const int sx = std::clamp(int((p.s + (p.s2 - p.s) * (float(px) + 0.5f) / float(w)) * float(f.image.w)),
                                          0, f.image.w - 1);
                const int sy = std::clamp(int((p.t + (p.t2 - p.t) * (float(py) + 0.5f) / float(h)) * float(f.image.h)),
                                          0, f.image.h - 1);
                const uint8_t *src = f.image.pixel(sx, sy);
                uint8_t *dst = s.row(dy) + dx * 4;
                const int a = src[3];
                for (int c = 0; c < 3; ++c)
                    dst[c] = uint8_t((src[c] * tint[c] / 255 * a + dst[c] * (255 - a)) / 255);
            }
        bottom = std::max(bottom, int(p.y) + h);
    }
    return std::max(bottom, y + f.font.pointSize);
}

void TextViewer::show() {
    Surface &s = _ctx.screen;
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    if (_strings.packages().empty()) {
        text(4, 4, "No string packages (strip/*.sp)");
        return;
    }
    const raven::StringPackage &p = _strings.packages()[size_t(_package)];
    setTitle("strip/" + p.reference + " (" + std::to_string(_package + 1) + "/" +
             std::to_string(_strings.packages().size()) + ")");
    const std::string fontName = _fonts.empty() ? std::string("none") : _fonts[size_t(_font)].font.name;
    text(4, 4,
         p.reference + " (" + std::to_string(p.id) + "): " + p.description + ", " + std::to_string(p.entries.size()) +
             " entries");
    text(4, 4 + lh,
         std::string(kLanguages[int(_language)]) + " (L: language), font " + fontName +
             " (F), Left/Right: package, Up/Down/PageUp/PageDown: scroll" +
             (_warnings.empty() ? std::string() : ", " + std::to_string(_warnings.size()) + " unreadable"),
         {150, 200, 255});
    int y = 4 + 3 * lh;
    for (size_t i = size_t(_first); i < p.entries.size() && y < s.h - lh; ++i) {
        const raven::StringPackage::Entry &e = p.entries[i];
        const std::string *t = _strings.find(p.reference + "_" + e.reference, _language);
        text(4, y, p.reference + "_" + e.reference, {200, 200, 120});
        y = drawText(12, y + lh, t ? *t : std::string(), s.w - 24) + lh / 2;
    }
}

} // namespace reluva::idtech3
