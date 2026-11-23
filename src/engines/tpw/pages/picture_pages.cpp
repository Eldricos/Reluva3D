// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages for the game's pictures and fonts.

#include "common/events.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "engines/tpw/game_files.h"
#include "graphics/font.h"
#include "image/fsh.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {24, 28, 36};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kDim = {150, 160, 175};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24; // below the status line

} // namespace

PicturePage::PicturePage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".fsh", ".tga", ".png", ".jpg"});
    if (!_names.empty())
        selected();
}

std::string PicturePage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void PicturePage::selected() {
    _picture = {};
    _kind.clear();
    _error.clear();
    std::vector<uint8_t> data;
    const std::string &name = _names[_index];
    if (Error err = _context.files.read(name, data)) {
        _error = err.message;
        return;
    }
    if (fs::toLower(name.substr(name.size() - 4)) != ".fsh") {
        _kind = fs::toLower(name.substr(name.size() - 3));
        if (Error err = image::decode(data, _picture))
            _error = err.message;
        return;
    }
    image::ShapeBank bank;
    if (Error err = image::parseShapeBank(std::move(data), bank, kShapeOptions)) {
        _error = err.message;
        return;
    }
    if (bank.shapes.empty()) {
        _error = "no shapes in the bank";
        return;
    }
    const image::Shape &shape = bank.shapes[0];
    _kind = std::string("fsh, ") + image::shapeTypeName(shape.type) + (shape.compressed ? ", compressed" : "") +
            (shape.longName.empty() ? "" : ", \"" + shape.longName + "\"");
    if (Error err = image::decodeShape(shape, _picture))
        _error = err.message;
}

void PicturePage::handleKey(uint32_t key) {
    if (key == 'z')
        _fit = !_fit;
    else
        Page::handleKey(key);
}

void PicturePage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s", _index + 1, _names.size(), current().c_str()) +
                  (_picture.w ? text::format("  %dx%d, %s", _picture.w, _picture.h, _kind.c_str()) : ""),
              kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    if (_picture.w == 0 || _picture.h == 0)
        return;
    // Fitted: the largest whole zoom that fits, at least 1.
    const int room = screen.h - kTop;
    const int zoom = _fit ? std::max(1, std::min(screen.w / _picture.w, room / _picture.h)) : 1;
    drawPicture(screen, _picture, (screen.w - _picture.w * zoom) / 2, kTop + (room - _picture.h * zoom) / 2, zoom);
    if (zoom > 1)
        drawLabel(screen, 4, screen.h - font::kLineHeight - 2, text::format("x%d (Z: its own size)", zoom), kDim);
}

FontPage::FontPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".bf4"});
    if (!_names.empty())
        selected();
}

std::string FontPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void FontPage::selected() {
    _font = {};
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _context.files.read(_names[_index], data))
        _error = err.message;
    else if (Error parseErr = _font.parse(data))
        _error = parseErr.message;
}

void FontPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    size_t unread = 0;
    for (const Font::Glyph &g : _font.glyphs())
        unread += !g.readable;
    drawLabel(
        screen, 4, 4,
        text::format("%zu/%zu  %s  %zu glyphs", _index + 1, _names.size(), current().c_str(), _font.glyphs().size()) +
            (unread ? text::format(", %zu of a storage not read", unread) : ""),
        kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    // How tall the font draws: its deepest glyph.
    int height = 1;
    for (const Font::Glyph &g : _font.glyphs())
        height = std::max(height, g.top + g.height);
    int y = kTop + 4;
    for (const char *sample : {"SimCoaster: Land of Invention, Polar Zone, Arabian Nights",
                               "The quick brown fox jumps over the lazy dog 0123456789",
                               "\xc2\xa3$\xe2\x84\xa2 \xc3\xa9\xc3\xa8\xc3\xbc\xc3\xb1 (!?) - 50%"}) {
        if (y + height > screen.h / 2)
            break;
        drawGameText(screen, _font, _context.characters, 8, y, sample, kLabel);
        y += height + 4;
    }
    // Every glyph in a grid, with its code in the character set: cells as
    // wide as the widest glyph, as many as fit.
    int widest = 1;
    for (const Font::Glyph &g : _font.glyphs())
        widest = std::max(widest, g.left + g.width);
    const int label = 4 * font::kAdvance;
    const int cellW = label + widest + 4;
    const int cellH = std::max(height, font::kLineHeight) + 2;
    const int columns = std::max(1, (screen.w - 8) / cellW);
    y = std::max(y + 8, screen.h / 2);
    for (size_t i = 0; i < _font.glyphs().size(); ++i) {
        const int cx = 8 + int(i % size_t(columns)) * cellW;
        const int cy = y + int(i / size_t(columns)) * cellH;
        if (cy + cellH > screen.h)
            break;
        const Font::Glyph &g = _font.glyphs()[i];
        drawLabel(screen, cx, cy, text::format("%zu", i + 1), kDim);
        for (int py = 0; py < g.height && !g.coverage.empty(); ++py)
            for (int px = 0; px < g.width; ++px) {
                const int v = g.coverage[size_t(py * g.width + px)] * 17;
                if (v)
                    fillRect(screen, cx + label + g.left + px, cy + g.top + py, 1, 1,
                             Color{uint8_t(v), uint8_t(v), uint8_t(v)});
            }
    }
}

} // namespace reluva::tpw
