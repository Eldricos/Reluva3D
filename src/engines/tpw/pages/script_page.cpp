// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page for the ride and shop scripts.

#include "common/events.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {24, 28, 36};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kLabelLine = {140, 200, 255};
constexpr Color kDim = {150, 160, 175};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr int kLine = font::kLineHeight;
constexpr int kTab = 8; // characters a tab stop

// Tabs as spaces, to the next stop.
std::string expandTabs(const std::string &line) {
    std::string out;
    for (char c : line) {
        if (c != '\t') {
            out += c;
            continue;
        }
        do
            out += ' ';
        while (out.size() % kTab);
    }
    return out;
}

} // namespace

ScriptPage::ScriptPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".rse"});
    if (!_names.empty())
        selected();
}

std::string ScriptPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void ScriptPage::selected() {
    _lines.clear();
    _summary.clear();
    _error.clear();
    _first = 0;
    std::vector<uint8_t> data;
    Script script;
    if (Error err = _context.files.read(_names[_index], data)) {
        _error = err.message;
        return;
    }
    if (Error err = script.parse(data)) {
        _error = err.message;
        return;
    }
    _summary = text::format("%zu instructions, %zu variables", script.instructions().size(), script.variables().size());
    for (const std::string &line : text::split(script.disassemble(), '\n'))
        _lines.push_back(expandTabs(line));
}

void ScriptPage::handleKey(uint32_t key) {
    const size_t page = size_t((kViewerHeight - kTop) / kLine) - 1;
    const size_t last = _lines.empty() ? 0 : _lines.size() - 1;
    if (key == key::kUp && _first > 0)
        --_first;
    else if (key == key::kDown && _first < last)
        ++_first;
    else if (key == key::kPageUp)
        _first = _first > page ? _first - page : 0;
    else if (key == key::kPageDown)
        _first = std::min(last, _first + page);
    else
        Page::handleKey(key);
}

void ScriptPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  %s", _index + 1, _names.size(), current().c_str(), _summary.c_str()), kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    for (size_t i = _first; i < _lines.size(); ++i) {
        const int y = kTop + int(i - _first) * kLine;
        if (y + kLine > screen.h)
            break;
        const std::string &line = _lines[i];
        const Color c = line.empty() ? kLabel : line[0] == '.' ? kLabelLine : line[0] == ' ' ? kLabel : kDim;
        drawLabel(screen, 4, y, line, c);
    }
}

} // namespace reluva::tpw
