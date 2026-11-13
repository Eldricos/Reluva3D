// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/start_page.h"

#include "graphics/font.h"

#include <algorithm>

namespace reluva::idtech3 {

namespace {

constexpr int kChoiceTop = 200;
constexpr int kChoiceHeight = 56;
constexpr int kChoiceLeft = 70;
constexpr int kChoiceWidth = 500;

} // namespace

StartPage::StartPage(ViewerContext &ctx, std::vector<Choice> choices) : Page(ctx), _choices(std::move(choices)) {
    // The first level's picture behind the choices.
    for (const char *ext : {".tga", ".jpg"}) {
        std::vector<uint8_t> data;
        if (!ctx.game.firstMap.empty() && !ctx.files.read("levelshots/" + ctx.game.firstMap + ext, data) &&
            !image::decode(data, _background))
            break;
        _background = {};
    }
}

void StartPage::activate() {
    setTitle("choose what to play");
    draw();
}

int StartPage::choiceAt(int x, int y) const {
    if (x < kChoiceLeft || x >= kChoiceLeft + kChoiceWidth || y < kChoiceTop)
        return -1;
    const int i = (y - kChoiceTop) / kChoiceHeight;
    return i < int(_choices.size()) && (y - kChoiceTop) % kChoiceHeight < kChoiceHeight - 8 ? i : -1;
}

void StartPage::choose(size_t i) {
    if (i < _choices.size())
        _ctx.requestPage = _choices[i].page;
}

void StartPage::handleEvent(const Event &ev) {
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown) {
        const int i = choiceAt(ev.mouseX, ev.mouseY);
        if (i >= 0 && size_t(i) != _selected) {
            _selected = size_t(i);
            draw();
        }
        if (i >= 0 && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft)
            choose(size_t(i));
        return;
    }
    if (ev.type != EventType::kKeyDown || _choices.empty())
        return;
    if (ev.key == key::kUp)
        _selected = (_selected + _choices.size() - 1) % _choices.size();
    else if (ev.key == key::kDown || ev.key == key::kTab)
        _selected = (_selected + 1) % _choices.size();
    else if (ev.key == key::kReturn || ev.key == key::kKpEnter || ev.key == ' ')
        choose(_selected);
    else if (ev.key >= '1' && ev.key < '1' + _choices.size())
        choose(ev.key - '1');
    else
        return;
    draw();
}

void StartPage::draw() {
    Surface &s = _ctx.screen;
    // The picture, scaled to the screen and darkened; else a plain dark blue.
    for (int y = 0; y < s.h; ++y) {
        uint8_t *row = s.row(y);
        for (int x = 0; x < s.w; ++x) {
            uint8_t *p = row + x * 4;
            if (_background.w > 0) {
                const uint8_t *q = _background.pixel(x * _background.w / s.w, y * _background.h / s.h);
                for (int c = 0; c < 3; ++c)
                    p[c] = uint8_t(q[c] * 2 / 7);
            } else {
                p[0] = 12;
                p[1] = 16;
                p[2] = 34;
            }
            p[3] = 255;
        }
    }
    const int titleScale = 3;
    const std::string &title = _ctx.game.name;
    const int titleWidth = int(title.size()) * font::kAdvance * titleScale;
    const int titleX = std::max(8, (s.w - titleWidth) / 2);
    font::drawText(s, titleX, 90, title, {255, 200, 90}, titleWidth > s.w - 16 ? 2 : titleScale);
    for (size_t i = 0; i < _choices.size(); ++i) {
        const int y = kChoiceTop + int(i) * kChoiceHeight;
        const bool on = i == _selected;
        fill(kChoiceLeft, y, kChoiceWidth, kChoiceHeight - 8, on ? 60 : 20, on ? 80 : 26, on ? 140 : 50);
        if (on) // a bar down its left
            fill(kChoiceLeft, y, 4, kChoiceHeight - 8, 255, 200, 90);
        font::drawText(s, kChoiceLeft + 16, y + 6, std::to_string(i + 1) + "  " + _choices[i].label,
                       on ? Color{255, 255, 255} : Color{200, 210, 230}, 2);
        text(kChoiceLeft + 16 + 3 * font::kAdvance * 2, y + 6 + 2 * font::kLineHeight + 1, _choices[i].detail,
             on ? Color{200, 220, 255} : Color{140, 150, 170});
    }
    const std::string hint = "Up/Down or the mouse to choose, Enter to start, Esc for the launcher";
    text((s.w - int(hint.size()) * font::kAdvance) / 2, s.h - 2 * font::kLineHeight, hint, {150, 160, 180});
}

} // namespace reluva::idtech3
