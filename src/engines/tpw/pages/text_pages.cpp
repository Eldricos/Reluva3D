// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages for the game's text: its string tables and its
// settings files.

#include "common/events.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {24, 28, 36};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kDim = {150, 160, 175};
constexpr Color kPicked = {60, 80, 120};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr int kLine = font::kLineHeight;
// The font text is drawn in when the game's is asked for: its panels'.
constexpr const char *kGameFont = "GAME12.bf4";

// Text cut into lines of at most `width` characters, at spaces where it can
// be, and at its own line breaks.
std::vector<std::string> wrap(const std::string &utf8, size_t width) {
    std::vector<std::string> lines;
    for (const std::string &para : text::split(utf8, '\n')) {
        std::string line;
        for (const std::string &word : text::split(para, ' ')) {
            if (!line.empty() && font::length(line) + 1 + font::length(word) > width) {
                lines.push_back(line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
        }
        lines.push_back(line);
    }
    return lines;
}

} // namespace

TextPage::TextPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".str"});
    std::vector<uint8_t> data;
    if (!_context.files.read("Language/" + _context.language + "/" + kGameFont, data))
        (void)_gameFont.parse(data);
    if (!_names.empty())
        selected();
}

std::string TextPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void TextPage::selected() {
    _table = {};
    _error.clear();
    _line = 0;
    std::vector<uint8_t> data;
    if (Error err = _context.files.read(_names[_index], data))
        _error = err.message;
    else if (Error parseErr = _table.parse(data, _context.characters))
        _error = parseErr.message;
}

void TextPage::handleKey(uint32_t key) {
    if (key == key::kUp && _line > 0)
        --_line;
    else if (key == key::kDown && _line + 1 < _table.size())
        ++_line;
    else if (key == 'f')
        _useGameFont = !_useGameFont && !_gameFont.glyphs().empty();
    else
        Page::handleKey(key);
}

void TextPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  table %u, %zu strings", _index + 1, _names.size(), current().c_str(),
                           _table.id(), _table.size()),
              kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    if (_table.size() == 0) {
        drawLabel(screen, 4, kTop + 4, "(no strings)", kDim);
        return;
    }
    // The list above, the picked string whole below it.
    const int listBottom = screen.h * 3 / 5;
    const size_t rows = size_t((listBottom - kTop) / kLine);
    const size_t first =
        _line >= rows / 2 ? std::min(_line - rows / 2, _table.size() > rows ? _table.size() - rows : 0) : 0;
    for (size_t i = first; i < _table.size() && i < first + rows; ++i) {
        const int y = kTop + int(i - first) * kLine;
        if (i == _line)
            fillRect(screen, 0, y - 1, screen.w, kLine, kPicked);
        std::string line = _table.text(i);
        std::replace(line.begin(), line.end(), '\n', ' ');
        drawLabel(screen, 4, y, text::format("%4zu", i), kDim);
        drawLabel(screen, 36, y, line, kLabel);
    }
    fillRect(screen, 0, listBottom + 2, screen.w, 1, kDim);
    int y = listBottom + 8;
    if (_useGameFont) {
        for (const std::string &line : wrap(_table.text(_line), 70)) {
            drawGameText(screen, _gameFont, _context.characters, 8, y, line, kLabel);
            y += 18;
        }
        drawLabel(screen, 4, screen.h - kLine - 2, std::string("in ") + kGameFont + " (F: the built-in font)", kDim);
        return;
    }
    for (const std::string &line : wrap(_table.text(_line), size_t((screen.w - 16) / font::kAdvance))) {
        drawLabel(screen, 8, y, line, kLabel);
        y += kLine;
    }
    drawLabel(screen, 4, screen.h - kLine - 2, std::string("F: in the game's font, ") + kGameFont, kDim);
}

SettingsPage::SettingsPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".sam"});
    if (!_names.empty())
        selected();
}

std::string SettingsPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void SettingsPage::selected() {
    _lines.clear();
    _error.clear();
    _first = 0;
    std::vector<uint8_t> data;
    SamFile sam;
    if (Error err = _context.files.read(_names[_index], data)) {
        _error = err.message;
        return;
    }
    if (Error err = sam.parse(data)) {
        _error = err.message;
        return;
    }
    for (const SamFile::Entry &e : sam.entries()) {
        std::string line = text::format("%5d  %s", e.line, e.name.c_str());
        for (const SamFile::Value &v : e.values)
            line += v.isText ? "  \"" + v.text + "\"" : "  " + text::format("%g", v.number);
        _lines.push_back(line);
        for (const std::string &row : e.grid)
            _lines.push_back("         | " + row);
    }
}

void SettingsPage::handleKey(uint32_t key) {
    if (key == key::kUp && _first > 0)
        --_first;
    else if (key == key::kDown && _first + 1 < _lines.size())
        ++_first;
    else
        Page::handleKey(key);
}

void SettingsPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  %zu lines", _index + 1, _names.size(), current().c_str(), _lines.size()),
              kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    for (size_t i = _first; i < _lines.size(); ++i) {
        const int y = kTop + int(i - _first) * kLine;
        if (y + kLine > screen.h)
            break;
        drawLabel(screen, 4, y, _lines[i], _lines[i].find('|') != std::string::npos ? kDim : kLabel);
    }
}

} // namespace reluva::tpw
