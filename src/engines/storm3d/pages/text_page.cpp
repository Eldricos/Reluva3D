// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/text_page.h"

#include "common/archive.h"
#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"

#include <algorithm>
#include <filesystem>

namespace reluva::storm3d {

namespace {

constexpr Color kBackground = {16, 20, 28};
constexpr Color kText = {210, 210, 200};
constexpr Color kComment = {120, 160, 120};
constexpr int kTabWidth = 4;

// The extensions of the game's text files; .set and the .ldl files are
// left out (binary, and encoded).
const char *const kExtensions[] = {".odf", ".txt", ".t",   ".tt",  ".h",   ".cfg", ".aip",
                                   ".spr", ".map", ".mdf", ".pth", ".ini", ".std", ".des"};

std::string extension(const std::string &name) {
    return text::toLower(std::filesystem::path(name).extension().string());
}

} // namespace

bool TextPage::isText(const std::string &name) {
    const std::string ext = extension(name);
    return std::any_of(std::begin(kExtensions), std::end(kExtensions), [&](const char *e) { return ext == e; });
}

TextPage::TextPage(System &sys, Surface &screen, const GameFiles &files) : _sys(sys), _screen(screen), _files(files) {
    for (const std::string &name : files.list())
        if (isText(name))
            _names.push_back(name);
    // By kind, then by name, so F can step from one kind to the next.
    std::stable_sort(_names.begin(), _names.end(),
                     [](const std::string &a, const std::string &b) { return extension(a) < extension(b); });
}

void TextPage::handleKey(uint32_t key) {
    const int screenLines = (_screen.h - font::kLineHeight - 12) / font::kLineHeight;
    // Scrolled no further than the last screenful.
    const size_t lastTop = _lines.size() > size_t(screenLines) ? _lines.size() - size_t(screenLines) : 0;
    const size_t before = _current;
    switch (key) {
    case key::kLeft:
        _current = stepIndex(_current, -1, _names.size());
        break;
    case key::kRight:
        _current = stepIndex(_current, 1, _names.size());
        break;
    case 'f': {
        const std::string kind = extension(_names[_current]);
        size_t i = _current;
        while (i < _names.size() && extension(_names[i]) == kind)
            ++i;
        _current = i < _names.size() ? i : 0;
        break;
    }
    case key::kUp:
        _top = stepIndex(_top, -1, _lines.size());
        break;
    case key::kDown:
        _top = std::min(lastTop, _top + 1);
        break;
    case key::kPageUp:
        _top = stepIndex(_top, -screenLines, _lines.size());
        break;
    case key::kPageDown:
        _top = std::min(lastTop, _top + size_t(screenLines));
        break;
    case key::kHome:
        _top = 0;
        break;
    case key::kEnd:
        _top = lastTop;
        break;
    default:
        return;
    }
    if (_current != before) {
        _loaded = false;
        _top = 0;
    }
    show();
}

void TextPage::load() {
    _lines.clear();
    _loaded = true;
    std::vector<uint8_t> data;
    if (Error err = _files.read(_names[_current], data)) {
        _lines.push_back(err.message);
        return;
    }
    const std::string utf8 = text::windows1252ToUtf8(std::string(data.begin(), data.end()));
    for (std::string line : text::split(utf8, '\n')) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string expanded;
        for (const char c : line) {
            if (c == '\t')
                expanded.append(size_t(kTabWidth - int(font::length(expanded)) % kTabWidth), ' ');
            else
                expanded += c;
        }
        _lines.push_back(std::move(expanded));
    }
}

void TextPage::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_names.empty())
        return;
    if (!_loaded)
        load();
    const int screenLines = (_screen.h - font::kLineHeight - 12) / font::kLineHeight;
    for (int r = 0; r < screenLines && _top + size_t(r) < _lines.size(); ++r) {
        const std::string &line = _lines[_top + size_t(r)];
        const std::string trimmed = text::trim(line);
        const bool comment = trimmed.rfind("//", 0) == 0 || trimmed.rfind('#', 0) == 0 || trimmed.rfind(';', 0) == 0;
        font::drawText(_screen, 4, 4 + r * font::kLineHeight, line, comment ? kComment : kText);
    }
    const std::string status =
        text::format("%zu/%zu  %s  lines %zu-%zu of %zu", _current + 1, _names.size(), _names[_current].c_str(),
                     _lines.empty() ? 0 : _top + 1, std::min(_lines.size(), _top + size_t(screenLines)), _lines.size());
    drawStatus(_screen, status);
    _sys.setWindowTitle("Reluva viewer - Text - " + status);
}

} // namespace reluva::storm3d
