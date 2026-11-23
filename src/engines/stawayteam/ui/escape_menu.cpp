// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/escape_menu.h"

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/main_menu.h"

#include <array>
#include <optional>
#include <utility>

namespace reluva::stawayteam {

namespace {

// Its buttons, a column 69 in from its left, the first 26 down, each the
// next 8 under the last; RESUME 27 above its bottom under its own height
// (0x5bab25, 0x5babe4, 0x5bae8a).
constexpr int kButtonX = 0x45;
constexpr int kFirstY = 0x1a;
constexpr int kGap = 8;
constexpr int kResumeAbove = 0x1b;
constexpr std::array<const char *, 6> kLabels = {"&CONFIGURE",       "&SAVE GAME", "&LOAD GAME",
                                                 "RESTART &MISSION", "E&ND GAME",  "&RESUME"};
enum Button { kConfigure, kSave, kLoad, kRestart, kEnd, kResume };
const char *const kEndQuestion = "Are you sure you want to quit the game and exit to the main menu?"; // 0x5bb190
const char *const kRestartQuestion = "Are you sure you want to restart this game?";                   // 0x5bb1ea

} // namespace

EscapeWindow::EscapeWindow(const Archive &data, const TextObject &rules, int screenW, int screenH)
    : _data(data), _rules(rules), _background(screenPicture(data, "Escape Menu/Escape Menu Background")),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    (void)_font.load(data, "Placar Condensed");
    const image::Image picture = screenPicture(data, "Escape Menu/Button Background");
    int y = kFirstY;
    for (size_t i = 0; i < kLabels.size(); ++i) {
        PictureButton button(picture, kLabels[i], &_font);
        if (i == kResume) {
            button.setPosition(kButtonX, _background.h - button.height() - kResumeAbove);
        } else {
            button.setPosition(kButtonX, y);
            y += button.height() + kGap;
        }
        button.setSound(gameSound(data, "UI/Button Action.wav"));
        _buttons.push_back(std::move(button));
    }
}

EscapeWindow::~EscapeWindow() = default;

EscapeWindow::Choice EscapeWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, mixer);
        if (!answer)
            return Choice::kNone;
        _confirm.reset();
        return *answer ? std::exchange(_asking, Choice::kNone) : Choice::kNone;
    }
    // RESUME, made last, is the window's default and cancel button
    // (0x5bae25).
    if ((isEnter(ev) || isEscape(ev)) && _buttons[kResume].press(mixer))
        return Choice::kResume;
    for (size_t i = 0; i < _buttons.size(); ++i) {
        if (!_buttons[i].handleEvent(ev, _x, _y, mixer))
            continue;
        switch (i) {
        case kConfigure:
            return Choice::kConfigure;
        case kSave:
            return Choice::kSaveGame;
        case kLoad:
            return Choice::kLoadGame;
        case kRestart:
            _asking = Choice::kRestart;
            _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kRestartQuestion, 640, 480);
            return Choice::kNone;
        case kEnd:
            _asking = Choice::kEndGame;
            _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kEndQuestion, 640, 480);
            return Choice::kNone;
        case kResume:
            return Choice::kResume;
        default:
            return Choice::kNone;
        }
    }
    return Choice::kNone;
}

void EscapeWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    for (PictureButton &b : _buttons)
        b.draw(screen, _x, _y, now);
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
