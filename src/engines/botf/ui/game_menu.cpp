// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/game_menu.h"

#include <algorithm>
#include <string>
#include <vector>

namespace reluva::botf {

namespace {

constexpr int kScreenW = 800;
constexpr int kScreenH = 600;
constexpr char kLetters[] = "chfkr"; // the five empires' files, in race.rst's order

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
    _tips.load(_assets);
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
        _tips.draw(screen, _pointed, _pointerX, _pointerY);
}

} // namespace reluva::botf
