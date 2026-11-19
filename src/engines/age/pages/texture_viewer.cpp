// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Textures page.

#include "engines/age/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>

namespace reluva::age {

namespace {

bool endsWith(const std::string &s, const char *ext) {
    const std::string e = ext;
    return s.size() > e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

} // namespace

TextureViewer::TextureViewer(ScreenContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if ((key.rfind("texture/", 0) == 0 && (endsWith(key, ".tex") || endsWith(key, ".tga"))) ||
            (key.rfind("jpg/", 0) == 0 && endsWith(key, ".jpg")))
            _names.push_back(n);
    }
}

void TextureViewer::activate() {
    if (!_names.empty())
        show(_index);
}

void TextureViewer::show(int index) {
    _index = index;
    const std::string &name = _names[size_t(index)];
    _tex = Tex{};
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _ctx.files.read(name, data))
        _error = err.message;
    else if (endsWith(normalizeArchiveName(name), ".tex"))
        _error = decodeTex(data, _tex).message;
    else if (Error e = image::decode(data, _tex.image))
        _error = e.message;
    else
        _tex.levels = 1;
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");
    draw();
}

void TextureViewer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _names.empty())
        return;
    const int n = static_cast<int>(_names.size());
    int i = _index;
    if (ev.key == key::kLeft)
        i = (i + n - 1) % n;
    else if (ev.key == key::kRight)
        i = (i + 1) % n;
    else if (ev.key == key::kPageUp)
        i = std::max(0, i - 10);
    else if (ev.key == key::kPageDown)
        i = std::min(n - 1, i + 10);
    else if (ev.key == key::kHome)
        i = 0;
    else if (ev.key == key::kEnd)
        i = n - 1;
    else if (ev.key == key::kUp || ev.key == key::kDown) {
        // The first texture of the next (or previous) kind.
        const int step = ev.key == key::kDown ? 1 : n - 1;
        const std::string kind = kindOf(_names[size_t(i)]);
        do
            i = (i + step) % n;
        while (i != _index && kindOf(_names[size_t(i)]) == kind);
        if (ev.key == key::kUp)
            while (i > 0 && kindOf(_names[size_t(i - 1)]) == kindOf(_names[size_t(i)]))
                --i;
    } else if (ev.key == '+' || ev.key == '=') {
        _zoom = std::min(_zoom + 1, 8);
    } else if (ev.key == '-') {
        _zoom = std::max(_zoom - 1, 0);
    } else if (ev.key == 'a') {
        _alpha = !_alpha;
    } else {
        return;
    }
    if (i != _index)
        show(i);
    else
        draw();
}

void TextureViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    const std::string &name = _names[size_t(_index)];
    text(4, 4, name + "  (" + std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + ")", {150, 200, 255});
    if (!_error.empty()) {
        text(4, 4 + lh, _error, {255, 120, 120});
        return;
    }
    const image::Image &img = _tex.image;
    char info[160];
    // Not a .tex: a TGA or JPEG, as its name says.
    const std::string format =
        _tex.format ? formatName(_tex.format) : fs::toLower(name.substr(name.rfind('.') + 1)) + " file";
    std::snprintf(info, sizeof info, "%d x %d, %s, %u level%s, flags 0x%x%s", img.w, img.h, format.c_str(), _tex.levels,
                  _tex.levels == 1 ? "" : "s", _tex.flags, _alpha ? "" : "; alpha ignored (A)");
    text(4, 4 + lh, info, {180, 180, 180});

    // Fit the space below the text (zoom 0), or that many times the size.
    const int top = 4 + 3 * lh, areaW = _ctx.screen.w - 8, areaH = _ctx.screen.h - top - 4;
    int w = img.w, h = img.h;
    if (_zoom > 0) {
        w *= _zoom;
        h *= _zoom;
    } else {
        const float s = std::min({float(areaW) / float(w), float(areaH) / float(h), 4.0f});
        w = std::max(1, int(float(w) * s));
        h = std::max(1, int(float(h) * s));
    }
    const int x0 = 4 + std::max(0, (areaW - w) / 2), y0 = top + std::max(0, (areaH - h) / 2);
    Surface &s = _ctx.screen;
    for (int y = 0; y < h && y0 + y < s.h; ++y) {
        uint8_t *row = s.row(y0 + y);
        const int sy = int(int64_t(y) * img.h / h);
        for (int x = 0; x < w && x0 + x < s.w; ++x) {
            const uint8_t *p = img.pixel(int(int64_t(x) * img.w / w), sy);
            const int a = _alpha ? p[3] : 255;
            const uint8_t check = ((x / 8 + y / 8) & 1) ? 100 : 60; // shows through where transparent
            uint8_t *d = row + (x0 + x) * 4;
            for (int c = 0; c < 3; ++c)
                d[c] = uint8_t((p[c] * a + check * (255 - a)) / 255);
            d[3] = 255;
        }
    }
}

} // namespace reluva::age
