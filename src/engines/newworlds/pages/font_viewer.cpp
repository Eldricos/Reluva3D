// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/font.h"
#include "engines/newworlds/pages/viewers.h"

#include <algorithm>

namespace reluva::newworlds {

namespace {

constexpr Color kBackground{22, 26, 38};
constexpr Color kLabelColor{110, 118, 140};
constexpr Color kTextColor{235, 238, 245};
constexpr Color kBoxColor{90, 60, 60};
constexpr int kMargin = 6;

const char *const kSamples[] = {
    "STAR TREK: NEW WORLDS",
    "The quick brown fox jumps over the lazy dog.",
    "Stardate 8987.8. Priority One Communication received.",
    "0123456789 Done: 100% Caf\xc3\xa9 \xc2\xa9 \xc2\xbd",
};

} // namespace

FontViewer::FontViewer(System &sys, Surface &screen, const Archives &archives)
    : _sys(sys), _screen(screen), _fonts(loadGameFonts(archives)) {
}

void FontViewer::activate() {
    draw();
    updateTitle();
}

void FontViewer::handleKey(uint32_t k) {
    const int n = int(_fonts.size());
    if (n == 0)
        return;
    switch (k) {
    case key::kRight:
        _current = (_current + 1) % n;
        break;
    case key::kLeft:
        _current = (_current + n - 1) % n;
        break;
    case '+':
    case '=':
        _scale = std::min(_scale + 1, 4);
        break;
    case '-':
        _scale = std::max(_scale - 1, 1);
        break;
    case 'b':
        _boxes = !_boxes;
        break;
    default:
        return;
    }
    draw();
    updateTitle();
}

void FontViewer::draw() {
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4)
            row[0] = kBackground.r, row[1] = kBackground.g, row[2] = kBackground.b, row[3] = 255;
    }
    if (_fonts.empty())
        return;
    const GameFont &f = _fonts[size_t(_current)];
    const int line = f.lineHeight * _scale + 2;
    int y = kMargin;
    // Every glyph, in code order, wrapped at the screen's edge.
    font::drawText(_screen, kMargin, y,
                   "Characters " + std::to_string(f.first) + "-" + std::to_string(f.first + f.glyphs.size() - 1) + ":",
                   kLabelColor);
    y += font::kLineHeight + 2;
    int x = kMargin;
    for (size_t i = 0; i < f.glyphs.size(); ++i) {
        const GameFont::Glyph &g = f.glyphs[i];
        if (g.height == 0)
            continue;
        if (x + g.width * _scale > _screen.w - kMargin)
            x = kMargin, y += line;
        if (_boxes)
            for (int by = 0; by < g.height * _scale; ++by)
                for (int bx = 0; bx < g.width * _scale; ++bx)
                    if (y + by < _screen.h && x + bx < _screen.w) {
                        uint8_t *p = _screen.row(y + by) + (x + bx) * 4;
                        if ((bx + by) % 2 == 0 &&
                            (bx == 0 || by == 0 || bx == g.width * _scale - 1 || by == g.height * _scale - 1))
                            p[0] = kBoxColor.r, p[1] = kBoxColor.g, p[2] = kBoxColor.b;
                    }
        // Latin-1 codes are their own UTF-8 code points.
        const uint32_t cp = f.first + uint32_t(i);
        std::string ch;
        if (cp < 0x80)
            ch = char(cp);
        else
            ch = {char(0xc0 | (cp >> 6)), char(0x80 | (cp & 0x3f))};
        x = f.drawText(_screen, x, y, ch, kTextColor, _scale);
    }
    y += line + 8;
    font::drawText(_screen, kMargin, y, "Sample:", kLabelColor);
    y += font::kLineHeight + 2;
    for (const char *sample : kSamples) {
        f.drawText(_screen, kMargin, y, sample, kTextColor, _scale);
        y += line;
    }
}

void FontViewer::updateTitle() {
    if (_fonts.empty())
        return;
    const GameFont &f = _fonts[size_t(_current)];
    size_t drawn = 0;
    for (const auto &g : f.glyphs)
        drawn += g.height > 0;
    _sys.setWindowTitle("New Worlds fonts " + std::to_string(_current + 1) + "/" + std::to_string(_fonts.size()) +
                        ": " + f.name + " in " + f.archive + " (" + std::to_string(drawn) + " characters, " +
                        std::to_string(f.lineHeight) + " pixels high, image " + std::to_string(f.imageWidth) + "x" +
                        std::to_string(f.imageHeight) + ")");
}

} // namespace reluva::newworlds
