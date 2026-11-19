// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>

namespace reluva::botf {

namespace {

constexpr int kTop = 22;
constexpr int kBottom = 20;
constexpr int kKeyWidth = 70;

std::vector<std::string> sortedNames(const Archive &archive, const char *ext) {
    std::vector<std::string> names;
    for (std::string &n : archive.list())
        if (fs::toLower(n).ends_with(ext))
            names.push_back(std::move(n));
    std::sort(names.begin(), names.end(),
              [](const std::string &x, const std::string &y) { return fs::toLower(x) < fs::toLower(y); });
    return names;
}

} // namespace

TextViewer::TextViewer(System &sys, Surface &screen, const Archive &archive)
    : _sys(sys), _screen(screen), _archive(archive), _tables(sortedNames(archive, ".tdb")),
      _fonts(sortedNames(archive, ".fnt")) {
    // The Federation's 15-point font to start with: readable, with every glyph.
    for (size_t i = 0; i < _fonts.size(); ++i)
        if (fs::toLower(_fonts[i]) == "human15.fnt")
            _font = i;
}

void TextViewer::activate() {
    load();
    show();
}

void TextViewer::load() {
    _error.clear();
    _table = TextTable{};
    _first = 0;
    std::vector<uint8_t> data;
    if (Error err = _archive.read(_tables[_current], data))
        _error = err.message;
    else if (Error err2 = _table.load(data))
        _error = "can't read: " + err2.message;
    _fontData = Font{};
    if (!_fonts.empty()) {
        if (Error err = _archive.read(_fonts[_font], data); err || _fontData.load(data))
            _fontData = Font{};
    }
}

void TextViewer::handleKey(uint32_t key) {
    if (_tables.empty())
        return;
    const size_t n = _table.entries().size();
    if (key == key::kRight || key == key::kLeft) {
        _current =
            key == key::kRight ? (_current + 1) % _tables.size() : (_current + _tables.size() - 1) % _tables.size();
        load();
    } else if (key == 'f' && !_fonts.empty()) {
        _font = (_font + 1) % _fonts.size();
        const size_t keep = _first;
        load();
        _first = keep;
    } else if (key == key::kDown)
        _first = std::min(_first + 1, n ? n - 1 : 0);
    else if (key == key::kUp)
        _first = _first ? _first - 1 : 0;
    else if (key == key::kPageDown)
        _first = std::min(_first + 10, n ? n - 1 : 0);
    else if (key == key::kPageUp)
        _first = _first >= 10 ? _first - 10 : 0;
    else if (key == key::kHome)
        _first = 0;
    else if (key == key::kEnd)
        _first = n ? n - 1 : 0;
    else
        return;
    show();
}

void TextViewer::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {16, 20, 32});
    if (_tables.empty())
        return;
    const Color dim{140, 140, 160};
    const Color text{230, 230, 230};
    const auto &entries = _table.entries();
    std::string head =
        std::to_string(_current + 1) + "/" + std::to_string(_tables.size()) + "  " + _tables[_current] + "  ";
    head += _error.empty()
                ? std::to_string(entries.size()) + " entries, longest " + std::to_string(_table.longest()) + " bytes"
                : _error;
    if (!_fonts.empty())
        head += "  font " + _fonts[_font];
    font::drawText(_screen, 8, 6, head, text);

    // Each entry: its key on the left, its text wrapped in the game font
    // (or the built-in one if the font didn't load).
    const bool gameFont = _fontData.lineHeight() > 0;
    const int lineHeight = gameFont ? _fontData.lineHeight() : font::kLineHeight;
    const int textX = 8 + kKeyWidth;
    const int textW = _screen.w - textX - 8;
    int y = kTop;
    for (size_t i = _first; i < entries.size() && y < _screen.h - kBottom; ++i) {
        char key[16];
        std::snprintf(key, sizeof key, "%08x", entries[i].key);
        font::drawText(_screen, 8, y + 2, key, dim);
        const std::string shown = displayText(entries[i].text);
        const std::vector<std::string> lines =
            gameFont ? _fontData.wrap(shown, textW) : std::vector<std::string>{shown};
        for (const std::string &line : lines) {
            if (y + lineHeight > _screen.h - kBottom)
                break;
            if (gameFont)
                _fontData.draw(_screen, textX, y, line);
            else
                font::drawText(_screen, textX, y, line, text);
            y += lineHeight;
        }
        y += 4;
    }
    font::drawText(_screen, 8, _screen.h - 14,
                   "Left/Right: table  Up/Down, PageUp/PageDown: scroll  F: font  Tab: next page", dim);
    _sys.setWindowTitle("Reluva - Birth of the Federation - text " + std::to_string(_current + 1) + "/" +
                        std::to_string(_tables.size()) + ": " + _tables[_current] + ", entry " +
                        std::to_string(entries.empty() ? 0 : _first + 1) + "/" + std::to_string(entries.size()));
}

} // namespace reluva::botf
