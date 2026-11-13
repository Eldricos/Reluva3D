// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/texture.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>
#include <cstring>

namespace reluva::newworlds {

namespace {

constexpr int kFilterCount = 4;
constexpr const char *kFilterNames[kFilterCount] = {"all", "failed", "colour-keyed", "with alpha"};

bool endsWith(const std::string &s, const char *suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

} // namespace

TextureViewer::TextureViewer(System &sys, Surface &screen, const Archives &archives)
    : _sys(sys), _screen(screen), _archives(archives) {
    for (size_t a = 0; a < _archives.size(); ++a) {
        const auto &entries = _archives[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].tag == cattag::kTexture && !endsWith(entries[i].name, ".stb"))
                _textures.push_back({{a, i}});
    }
}

void TextureViewer::activate() {
    if (!_classified) {
        classify();
        _classified = true;
        applyFilter(0);
        return;
    }
    if (_visible.empty())
        applyFilter(_filter);
    else
        show(_current);
}

void TextureViewer::handleKey(uint32_t k) {
    if (k == 'f') {
        applyFilter((_filter + 1) % kFilterCount);
        return;
    }
    const int n = int(_visible.size());
    if (n == 0)
        return;
    switch (k) {
    case key::Right:
        show((_current + 1) % n);
        break;
    case key::Left:
        show((_current + n - 1) % n);
        break;
    case key::PageDown:
        show(std::min(_current + 10, n - 1));
        break;
    case key::PageUp:
        show(std::max(_current - 10, 0));
        break;
    case key::Home:
        show(0);
        break;
    case key::End:
        show(n - 1);
        break;
    default:
        break;
    }
}

// Decode every texture once so the filters know which is which.
void TextureViewer::classify() {
    std::vector<uint8_t> data;
    Texture tex;
    for (TextureRef &t : _textures) {
        CatArchive &cat = *_archives[t.ref.archive];
        t.ok = !cat.read(cat.entries()[t.ref.entry], data) && !decodeTexture(data, tex);
        t.colourKeyed = t.ok && tex.colourKeyed;
        t.alpha = t.ok && tex.format == 6;
    }
}

void TextureViewer::applyFilter(int filter) {
    _filter = filter;
    _visible.clear();
    for (size_t i = 0; i < _textures.size(); ++i) {
        const TextureRef &t = _textures[i];
        const bool keep =
            filter == 0 || (filter == 1 && !t.ok) || (filter == 2 && t.colourKeyed) || (filter == 3 && t.alpha);
        if (keep)
            _visible.push_back(i);
    }
    if (_visible.empty()) {
        drawCheckerboard();
        _sys.setWindowTitle(std::string("New Worlds textures [") + kFilterNames[_filter] +
                            "]: none (press F for the next filter, Tab for models)");
        return;
    }
    show(0);
}

void TextureViewer::show(int index) {
    _current = index;
    const EntryRef &ref = _textures[_visible[size_t(index)]].ref;
    CatArchive &cat = *_archives[ref.archive];
    const auto &entry = cat.entries()[ref.entry];

    drawCheckerboard();
    std::string status;
    std::vector<uint8_t> data;
    Texture tex;
    if (Error err = cat.read(entry, data)) {
        status = err.message;
    } else if (Error err2 = decodeTexture(data, tex)) {
        status = err2.message;
        fill(0, 0, _screen.w, _screen.h, 80, 0, 0); // dark red: couldn't decode
    } else {
        drawTexture(tex);
        status = "format " + std::to_string(tex.format) + ", " + std::to_string(tex.width) + "x" +
                 std::to_string(tex.height) +
                 (tex.colourKeyed ? ", colour key " + std::to_string(tex.keyIndex) : std::string());
    }
    _sys.setWindowTitle(std::string("New Worlds textures [") + kFilterNames[_filter] + "] " +
                        std::to_string(index + 1) + "/" + std::to_string(_visible.size()) + ": " + entry.name + " (" +
                        status + ")");
}

void TextureViewer::fill(int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = y0; y < y0 + h; ++y) {
        uint8_t *row = _screen.row(y) + x0 * 4;
        for (int x = 0; x < w; ++x, row += 4) {
            row[0] = r;
            row[1] = g;
            row[2] = b;
            row[3] = 255;
        }
    }
}

void TextureViewer::drawCheckerboard() {
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            row[0] = row[1] = row[2] = v;
            row[3] = 255;
        }
    }
}

// Draw centred, scaled up by the largest whole factor that fits (or down by
// halves), blended over the checkerboard using the alpha layer.
void TextureViewer::drawTexture(const Texture &tex) {
    const int sw = _screen.w;
    const int sh = _screen.h;
    int up = 1;
    int down = 1;
    while (tex.width * (up + 1) <= sw - 20 && tex.height * (up + 1) <= sh - 20)
        ++up;
    while (tex.width / down > sw || tex.height / down > sh)
        down *= 2;
    const int w = tex.width * up / down;
    const int h = tex.height * up / down;
    const int x0 = (sw - w) / 2;
    const int y0 = (sh - h) / 2;
    for (int y = 0; y < h; ++y) {
        const int sy = y * down / up;
        uint8_t *dst = _screen.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, dst += 4) {
            const uint8_t *src = &tex.rgba[(size_t(sy) * size_t(tex.width) + size_t(x * down / up)) * 4];
            // Colour key, then the alpha layer if there is one.
            const size_t i = size_t(sy) * size_t(tex.width) + size_t(x * down / up);
            const int a = src[3] && !tex.alpha.empty() ? tex.alpha[i] : src[3];
            for (int c = 0; c < 3; ++c)
                dst[c] = uint8_t((src[c] * a + dst[c] * (255 - a)) / 255);
        }
    }
}

} // namespace reluva::newworlds
