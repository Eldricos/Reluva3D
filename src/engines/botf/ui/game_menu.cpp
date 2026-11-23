// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/game_menu.h"

#include "common/text.h"

#include <algorithm>
#include <string>
#include <vector>

namespace reluva::botf {

namespace {

constexpr int kScreenW = 800;
constexpr int kScreenH = 600;
constexpr char kLetters[] = "chfkr"; // the five empires' files, in race.rst's order

// The tip's box, measured on a screenshot of the Federation's menu: opened
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

std::string role(const Widget &w, const char *which) {
    for (const auto &[r, file] : w.pictures)
        if (r == which)
            return file;
    return {};
}

} // namespace

Error GameMenu::load(int empire) {
    const std::string file = std::string(1, kLetters[std::clamp(empire, 0, 4)]) + "marker.wdf";
    std::vector<uint8_t> data;
    if (Error err = _assets.archive().read(file, data))
        return err;
    if (Error err = _layout.load(data))
        return Error::fail(file + ": " + err.message);
    for (const auto id : {Choice::kGalaxy, Choice::kSystem, Choice::kDiplomacy, Choice::kIntelligence,
                          Choice::kResearch, Choice::kMainMenu}) {
        const auto &c = _layout.root().children;
        if (std::none_of(c.begin(), c.end(), [&](const Widget &w) { return w.id == int32_t(id); }))
            return Error::fail(file + " has no button " + std::to_string(int(id)));
    }
    // Without the tips the menu works, untipped.
    std::vector<uint8_t> tips;
    if (_assets.archive().read("teachtip.tdb", tips) || _tips.load(tips))
        _tips = TextTable();
    _open = false;
    return Error::success();
}

void GameMenu::open(int x, int y) {
    const Widget &root = _layout.root();
    _x = std::clamp(x - root.w / 2, 0, std::max(0, kScreenW - root.w));
    _y = std::clamp(y - root.h / 2, 0, std::max(0, kScreenH - root.h));
    _pointerX = x;
    _pointerY = y;
    _pointed = buttonAt(x, y);
    _open = true;
}

int32_t GameMenu::buttonAt(int x, int y) const {
    for (const Widget &w : _layout.root().children) {
        const int left = _x + w.x;
        const int top = _y + w.y;
        if (w.type == WidgetType::kButton && x >= left && y >= top && x < left + w.w && y < top + w.h)
            return w.id;
    }
    return 0;
}

GameMenu::Choice GameMenu::handle(const Event &event) {
    if (!_open)
        return Choice::kNone;
    if (event.type == EventType::kKeyDown && event.key == key::kEscape) {
        _open = false;
        return Choice::kNone;
    }
    if (event.type != EventType::kMouseMove && event.type != EventType::kMouseDown)
        return Choice::kNone;
    _pointerX = event.mouseX;
    _pointerY = event.mouseY;
    _pointed = buttonAt(event.mouseX, event.mouseY);
    if (event.type != EventType::kMouseDown)
        return Choice::kNone;
    if (event.button == MouseButton::kRight) {
        open(event.mouseX, event.mouseY);
        return Choice::kNone;
    }
    if (event.button != MouseButton::kLeft)
        return Choice::kNone;
    _open = false;
    return Choice(_pointed);
}

void GameMenu::draw(Surface &screen) const {
    if (!_open)
        return;
    const Widget &root = _layout.root();
    if (const image::Image *back = _assets.picture(root.background))
        blitClipped(screen, *back, _x, _y, _x, _y, root.w, root.h);
    for (const Widget &w : root.children) {
        const std::string file = role(w, w.id == _pointed ? "selected" : "normal");
        if (const image::Image *img = file.empty() ? nullptr : _assets.picture(file))
            blitClipped(screen, *img, _x + w.x, _y + w.y, _x + w.x, _y + w.y, w.w, w.h);
    }
    if (_toolTips && _pointed)
        drawToolTip(screen);
}

void GameMenu::drawToolTip(Surface &screen) const {
    const TextTable::Entry *tip = _tips.find(uint32_t(_pointed));
    const Font *font = _assets.font(kTipFont);
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
    const int x = std::clamp(_pointerX + kTipDx, 0, std::max(0, kScreenW - w));
    const int y = std::clamp(_pointerY + kTipDy, 0, std::max(0, kScreenH - h));
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
