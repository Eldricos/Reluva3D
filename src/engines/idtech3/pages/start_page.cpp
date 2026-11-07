// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/start_page.h"

#include "common/font.h"

#include <algorithm>

namespace reluva::idtech3 {

namespace {

constexpr int kChoiceTop = 200, kChoiceHeight = 56, kChoiceLeft = 70, kChoiceWidth = 500;

} // namespace

StartPage::StartPage(ViewerContext& ctx, std::vector<Choice> choices) : Page(ctx), choices_(std::move(choices)) {
    // The first level's picture behind the choices.
    for (const char* ext : {".tga", ".jpg"}) {
        std::vector<uint8_t> data;
        if (!ctx.game.firstMap.empty() && !ctx.files.read("levelshots/" + ctx.game.firstMap + ext, data) &&
            !image::decode(data, background_))
            break;
        background_ = {};
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
    return i < int(choices_.size()) && (y - kChoiceTop) % kChoiceHeight < kChoiceHeight - 8 ? i : -1;
}

void StartPage::choose(size_t i) {
    if (i < choices_.size())
        ctx_.requestPage = choices_[i].page;
}

void StartPage::handleEvent(const Event& ev) {
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown) {
        const int i = choiceAt(ev.mouseX, ev.mouseY);
        if (i >= 0 && size_t(i) != selected_) {
            selected_ = size_t(i);
            draw();
        }
        if (i >= 0 && ev.type == EventType::MouseDown && ev.button == MouseButton::Left)
            choose(size_t(i));
        return;
    }
    if (ev.type != EventType::KeyDown || choices_.empty())
        return;
    if (ev.key == key::Up)
        selected_ = (selected_ + choices_.size() - 1) % choices_.size();
    else if (ev.key == key::Down || ev.key == key::Tab)
        selected_ = (selected_ + 1) % choices_.size();
    else if (ev.key == key::Return || ev.key == key::KpEnter || ev.key == ' ')
        choose(selected_);
    else if (ev.key >= '1' && ev.key < '1' + choices_.size())
        choose(ev.key - '1');
    else
        return;
    draw();
}

void StartPage::draw() {
    Surface& s = ctx_.screen;
    // The picture, scaled to the screen and darkened; else a plain dark blue.
    for (int y = 0; y < s.h; ++y) {
        uint8_t* row = s.row(y);
        for (int x = 0; x < s.w; ++x) {
            uint8_t* p = row + x * 4;
            if (background_.w > 0) {
                const uint8_t* q = background_.pixel(x * background_.w / s.w, y * background_.h / s.h);
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
    const std::string& title = ctx_.game.name;
    const int titleWidth = int(title.size()) * font::kAdvance * titleScale;
    const int titleX = std::max(8, (s.w - titleWidth) / 2);
    font::drawText(s, titleX, 90, title, {255, 200, 90}, titleWidth > s.w - 16 ? 2 : titleScale);
    for (size_t i = 0; i < choices_.size(); ++i) {
        const int y = kChoiceTop + int(i) * kChoiceHeight;
        const bool on = i == selected_;
        fill(kChoiceLeft, y, kChoiceWidth, kChoiceHeight - 8, on ? 60 : 20, on ? 80 : 26, on ? 140 : 50);
        if (on) // a bar down its left
            fill(kChoiceLeft, y, 4, kChoiceHeight - 8, 255, 200, 90);
        font::drawText(s, kChoiceLeft + 16, y + 6, std::to_string(i + 1) + "  " + choices_[i].label,
                       on ? Color{255, 255, 255} : Color{200, 210, 230}, 2);
        text(kChoiceLeft + 16 + 3 * font::kAdvance * 2, y + 6 + 2 * font::kLineHeight + 1, choices_[i].detail,
             on ? Color{200, 220, 255} : Color{140, 150, 170});
    }
    const std::string hint = "Up/Down or the mouse to choose, Enter to start, Esc for the launcher";
    text((s.w - int(hint.size()) * font::kAdvance) / 2, s.h - 2 * font::kLineHeight, hint, {150, 160, 180});
}

} // namespace reluva::idtech3
