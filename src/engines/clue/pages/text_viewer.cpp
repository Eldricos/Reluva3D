// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/pages/viewers.h"

#include "graphics/font.h"
#include "common/text.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
constexpr int kMargin = 8;
constexpr int kColumns = (kScreenW - 2 * kMargin) / font::kAdvance;
constexpr int kRows = (kScreenH - kInfoH - 2 * kMargin) / font::kLineHeight;

// Splits UTF-8 text into lines of at most kColumns characters, at spaces
// where it can.
void wrap(const std::string &line, std::vector<std::string> &out) {
    std::string rest = line;
    while (font::length(rest) > size_t(kColumns)) {
        // The byte offset of character kColumns, and the last space before it.
        size_t i = 0;
        size_t cut = 0;
        size_t space = std::string::npos;
        for (int c = 0; c < kColumns && i < rest.size(); ++c) {
            if (rest[i] == ' ')
                space = i;
            text::nextUtf8(rest, i);
            cut = i;
        }
        if (space != std::string::npos && space > 0)
            cut = space;
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut < rest.size() && rest[cut] == ' ' ? cut + 1 : cut);
    }
    out.push_back(rest);
}

} // namespace

TextViewer::TextViewer(System &sys, Surface &screen, const GameData &data) : _sys(sys), _screen(screen), _data(data) {
    _files = _data.withExtension(".txt");
}

void TextViewer::activate() {
    load();
    draw();
}

void TextViewer::load() {
    _lines.clear();
    _top = 0;
    if (_files.empty())
        return;
    std::ifstream in(_files[size_t(_current)], std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string utf8 = text::windows1252ToUtf8(raw);
    size_t pos = 0;
    while (pos <= utf8.size()) {
        size_t end = utf8.find('\n', pos);
        if (end == std::string::npos)
            end = utf8.size();
        std::string line = utf8.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        wrap(line, _lines);
        pos = end + 1;
    }
}

void TextViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const int maxTop = std::max(0, int(_lines.size()) - kRows);
    switch (key) {
    case key::Right:
        _current = (_current + 1) % int(_files.size());
        load();
        break;
    case key::Left:
        _current = (_current + int(_files.size()) - 1) % int(_files.size());
        load();
        break;
    case key::Down:
        _top = std::min(maxTop, _top + 1);
        break;
    case key::Up:
        _top = std::max(0, _top - 1);
        break;
    case key::PageDown:
        _top = std::min(maxTop, _top + kRows);
        break;
    case key::PageUp:
        _top = std::max(0, _top - kRows);
        break;
    case key::Home:
        _top = 0;
        break;
    case key::End:
        _top = maxTop;
        break;
    default:
        return;
    }
    draw();
}

void TextViewer::draw() {
    fillRect(_screen, 0, 0, kScreenW, kScreenH, {250, 246, 232});
    if (_files.empty()) {
        drawInfo(_screen, "No text files found");
        return;
    }
    for (int r = 0; r < kRows && _top + r < int(_lines.size()); ++r)
        font::drawText(_screen, kMargin, kMargin + r * font::kLineHeight, _lines[size_t(_top + r)], {32, 24, 16});
    const std::string name = _files[size_t(_current)].filename().string();
    drawInfo(_screen, name + "   file " + std::to_string(_current + 1) + "/" + std::to_string(_files.size()),
             "lines " + std::to_string(_top + 1) + "-" + std::to_string(std::min(int(_lines.size()), _top + kRows)) +
                 " of " + std::to_string(_lines.size()));
    _sys.setWindowTitle(std::string(gameName(_data.language())) + " text: " + name);
}

} // namespace reluva::clue
