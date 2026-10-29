// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include "common/font.h"
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
void wrap(const std::string& line, std::vector<std::string>& out) {
    std::string rest = line;
    while (font::length(rest) > size_t(kColumns)) {
        // The byte offset of character kColumns, and the last space before it.
        size_t i = 0, cut = 0, space = std::string::npos;
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

TextViewer::TextViewer(System& sys, Surface& screen, const GameData& data)
    : sys_(sys), screen_(screen), data_(data) {
    files_ = data_.withExtension(".txt");
}

void TextViewer::activate() {
    load();
    draw();
}

void TextViewer::load() {
    lines_.clear();
    top_ = 0;
    if (files_.empty())
        return;
    std::ifstream in(files_[size_t(current_)], std::ios::binary);
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
        wrap(line, lines_);
        pos = end + 1;
    }
}

void TextViewer::handleKey(uint32_t key) {
    if (files_.empty())
        return;
    const int maxTop = std::max(0, int(lines_.size()) - kRows);
    switch (key) {
    case key::Right:
        current_ = (current_ + 1) % int(files_.size());
        load();
        break;
    case key::Left:
        current_ = (current_ + int(files_.size()) - 1) % int(files_.size());
        load();
        break;
    case key::Down: top_ = std::min(maxTop, top_ + 1); break;
    case key::Up: top_ = std::max(0, top_ - 1); break;
    case key::PageDown: top_ = std::min(maxTop, top_ + kRows); break;
    case key::PageUp: top_ = std::max(0, top_ - kRows); break;
    case key::Home: top_ = 0; break;
    case key::End: top_ = maxTop; break;
    default: return;
    }
    draw();
}

void TextViewer::draw() {
    fillRect(screen_, 0, 0, kScreenW, kScreenH, {250, 246, 232});
    if (files_.empty()) {
        drawInfo(screen_, "No text files found");
        return;
    }
    for (int r = 0; r < kRows && top_ + r < int(lines_.size()); ++r)
        font::drawText(screen_, kMargin, kMargin + r * font::kLineHeight, lines_[size_t(top_ + r)], {32, 24, 16});
    const std::string name = files_[size_t(current_)].filename().string();
    drawInfo(screen_, name + "   file " + std::to_string(current_ + 1) + "/" + std::to_string(files_.size()),
             "lines " + std::to_string(top_ + 1) + "-" +
                 std::to_string(std::min(int(lines_.size()), top_ + kRows)) + " of " +
                 std::to_string(lines_.size()));
    sys_.setWindowTitle("Clue text: " + name);
}

} // namespace reluva::clue
