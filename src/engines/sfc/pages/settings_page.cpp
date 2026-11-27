// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Settings page: the settings files' sections and keys.

#include "common/fs.h"
#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <string>

namespace reluva::sfc {

namespace {

constexpr int kTop = 2 * font::kLineHeight + 8;

size_t linesShown() {
    return size_t((kScreenH - kTop - font::kLineHeight) / font::kLineHeight);
}

} // namespace

SettingsPage::SettingsPage(ViewerContext &ctx) : Page(ctx) {
    for (const char *ext : {".gf", ".mct"})
        for (const auto &p : filesWithExtension(ctx.root, ext))
            _files.push_back(p);
}

void SettingsPage::activate() {
    load();
    show();
}

void SettingsPage::load() {
    _lines.clear();
    _error.clear();
    _first = 0;
    std::vector<uint8_t> data;
    SettingsFile file;
    if (!readFile(_ctx.root / _files[_current], data)) {
        _error = "Can't read it";
        return;
    }
    if (Error err = file.read(data)) {
        _error = err.message;
        return;
    }
    for (const auto &section : file.sections) {
        if (section.name.empty() && section.entries.empty())
            continue;
        _lines.emplace_back(section.name.empty() ? "(before any section)" : "[" + section.name + "]", true);
        for (const auto &e : section.entries)
            _lines.emplace_back("  " + e.key + " = " + (e.quoted ? "\"" + e.value + "\"" : e.value), false);
    }
}

void SettingsPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _files.empty())
        return;
    const size_t n = _files.size();
    const size_t page = linesShown();
    const size_t last = _lines.empty() ? 0 : _lines.size() - 1;
    switch (ev.key) {
    case key::kLeft:
        _current = (_current + n - 1) % n;
        load();
        break;
    case key::kRight:
        _current = (_current + 1) % n;
        load();
        break;
    case key::kUp:
        _first = _first ? _first - 1 : 0;
        break;
    case key::kDown:
        _first = std::min(last, _first + 1);
        break;
    case key::kPageUp:
        _first = _first > page ? _first - page : 0;
        break;
    case key::kPageDown:
        _first = std::min(last, _first + page);
        break;
    case key::kHome:
        _first = 0;
        break;
    case key::kEnd:
        _first = last;
        break;
    default:
        return;
    }
    show();
}

void SettingsPage::show() {
    clear({20, 22, 28});
    if (_files.empty())
        return;
    const auto &rel = _files[_current];
    size_t sections = 0;
    for (const auto &line : _lines)
        sections += line.second ? 1 : 0;
    text(4, 2,
         rel.generic_string() + "  (" + std::to_string(_current + 1) + " of " + std::to_string(_files.size()) +
             "): " + std::to_string(sections) + " sections, " + std::to_string(_lines.size() - sections) + " keys",
         {150, 200, 255});
    text(4, 2 + font::kLineHeight, "Left/Right: files  Up/Down, PgUp/PgDn, Home/End: lines", {170, 170, 170});
    setTitle(rel.filename().string());
    if (!_error.empty()) {
        text(4, kTop, _error, {255, 120, 120});
        return;
    }
    int y = kTop;
    for (size_t i = _first; i < _lines.size() && i < _first + linesShown(); ++i, y += font::kLineHeight)
        text(4, y, _lines[i].first, _lines[i].second ? Color{150, 220, 150} : Color{230, 230, 230});
}

} // namespace reluva::sfc
