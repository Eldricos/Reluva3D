// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/tool_tips.h"

#include "common/text.h"
#include "engines/botf/formats/font.h"

#include <algorithm>
#include <string>
#include <vector>

namespace reluva::botf {

namespace {

constexpr int kScreenW = 800;
constexpr int kScreenH = 600;

// The tip's box, measured on a screenshot of the Federation's game menu: opened
// 4 pixels right of the pointer's hot spot and 17 below, a 1-pixel border
// in the text's colour round black; the text human15.fnt, 2 pixels in and
// 3 down, its lines 17 apart; the box 6 wider than its longest line.
constexpr int kTipDx = 4;
constexpr int kTipDy = 17;
constexpr int kTipTextDx = 2;
constexpr int kTipTextDy = 3;
constexpr int kTipLineHeight = 17;
constexpr int kTipMargin = 6;
constexpr Color kTipColor{99, 77, 181};
constexpr const char *kTipFont = "human15.fnt";

// A tip's lines, as teachtip.tdb keeps them: a space before, a line break
// after each line (some \r\n).
std::vector<std::string> tipLines(const std::string &tip) {
    std::vector<std::string> lines;
    std::string text = tip;
    if (!text.empty() && text.front() == ' ')
        text.erase(0, 1);
    for (std::string line : text::split(text, '\n')) {
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

} // namespace

void ToolTips::load(Assets &assets) {
    _assets = &assets;
    std::vector<uint8_t> data;
    if (assets.archive().read("teachtip.tdb", data) || _tips.load(data))
        _tips = TextTable();
}

void ToolTips::draw(Surface &screen, int32_t id, int pointerX, int pointerY) const {
    const TextTable::Entry *tip = _tips.find(uint32_t(id));
    const Font *font = _assets ? _assets->font(kTipFont) : nullptr;
    if (!tip || !font)
        return;
    const std::vector<std::string> lines = tipLines(tip->text);
    if (lines.empty())
        return;
    int widest = 0;
    for (const std::string &line : lines)
        widest = std::max(widest, font->measure(line));
    const int w = widest + kTipMargin;
    const int h = int(lines.size()) * kTipLineHeight;
    // Kept on the screen (guessed: the screenshot's tip fits as it is).
    const int x = std::clamp(pointerX + kTipDx, 0, std::max(0, kScreenW - w));
    const int y = std::clamp(pointerY + kTipDy, 0, std::max(0, kScreenH - h));
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col) {
            // The bottom right corner is left as it was: both edges stop
            // short of it, as lines drawn on Windows end before their last
            // pixel.
            const bool corner = row == y + h - 1 && col == x + w - 1;
            if (corner || row < 0 || col < 0 || row >= screen.h || col >= screen.w)
                continue;
            const bool edge = row == y || col == x || row == y + h - 1 || col == x + w - 1;
            const Color c = edge ? kTipColor : Color{};
            uint8_t *p = &screen.pixels[size_t(row) * size_t(screen.pitch) + size_t(col) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    for (size_t i = 0; i < lines.size(); ++i)
        font->drawInColor(screen, x + kTipTextDx, y + kTipTextDy + int(i) * kTipLineHeight, lines[i], kTipColor);
}

} // namespace reluva::botf
