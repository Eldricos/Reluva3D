// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/difficulty.h"

#include "engines/stawayteam/ui/button.h"

#include <algorithm>
#include <array>

namespace reluva::stawayteam {

namespace {

// The levels' names, in the program's table (0x63be78).
constexpr std::array<const char *, 2> kLevels = {"Easy", "Normal"};
const char *const kHeading = "Select a difficulty level"; // 0x63be90
// The heading's top (0x48c271).
constexpr int kHeadingY = 0x18;
// START GAME 20 above the window's bottom, CANCEL 4 above it (0x48bf76,
// 0x48bfc6); the toggles 4 apart, the last 16 above the window's middle
// (0x48c0c1).
constexpr int kStartBottomGap = 0x14;
constexpr int kButtonGap = 4;
constexpr int kToggleGap = 4;
constexpr int kTogglesAboveMiddle = 0x10;

} // namespace

DifficultyWindow::DifficultyWindow(const Archive &data, int screenW, int screenH, Difficulty current, bool fromSettings)
    : _background(screenPicture(data, "Difficulty Menu/Background")),
      // 0x48bf0b
      _start(screenPicture(data, "Difficulty Menu/Button Background"), fromSettings ? "&CONTINUE" : "&START GAME",
             &_buttonFont),
      _cancel(screenPicture(data, "Difficulty Menu/Button Background"), "CA&NCEL", &_buttonFont), _current(current),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    // The program names both fonts (0x48be90, 0x48bf88).
    (void)_font.load(data, "Placar Blue");
    (void)_buttonFont.load(data, "Placar Condensed");
    const int w = _background.w;
    const int h = _background.h;
    // Both buttons in the middle across (layout 6, 0x40aea0).
    _start.setPosition((w - _start.width()) / 2, h - _start.height() - kStartBottomGap);
    _cancel.setPosition((w - _cancel.width()) / 2, _start.y() - _cancel.height() - kButtonGap);
    const auto sound = gameSound(data, "UI/Button Action.wav");
    _switch = gameSound(data, "UI/Button Switch.wav");
    _start.setSound(sound);
    _cancel.setSound(sound);

    int widest = 0;
    for (size_t i = 0; i < kLevels.size(); ++i) {
        Toggle toggle(screenPicture(data, "Difficulty Menu/Toggle Default"),
                      screenPicture(data, "Difficulty Menu/Toggle Press"), kLevels[i], &_font);
        const int middle = (h - toggle.height()) / 2;
        toggle.setPosition(0, (toggle.height() + kToggleGap) * (int(i) - 1) + middle - kTogglesAboveMiddle);
        widest = std::max(widest, toggle.width());
        _toggles.push_back(std::move(toggle));
    }
    // Lined up on the left, the widest in the middle (0x48c107).
    const size_t checked = std::min(size_t(current) - 1, _toggles.size() - 1);
    for (size_t i = 0; i < _toggles.size(); ++i) {
        _toggles[i].setPosition((w - widest) / 2, _toggles[i].y());
        _toggles[i].setChecked(i == checked);
    }
}

std::optional<std::optional<Difficulty>> DifficultyWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    // START GAME is the window's default button, CANCEL its cancel
    // button (0x48c1ad, 0x48c1c0).
    // START GAME has the focus to start with (0x48c1b3); the keys move it,
    // a level's toggle taking Left and Right itself (0x4e4890), and Enter
    // presses the one with it.
    if (isEscape(ev) && _cancel.press(mixer))
        return std::optional<Difficulty>();
    const int count = 2 + int(_toggles.size());
    _focus = moveFocus(ev, _focus, count, [](int) { return true; }, _focus < 2);
    const bool enter = isEnter(ev);
    if (enter && _focus == 0 && _cancel.press(mixer))
        return std::optional<Difficulty>();
    // A level's toggle focused, Left and Right check the one before or
    // after it.
    int keyed = -1;
    if (_focus >= 2 && ev.type == EventType::kKeyDown && (ev.key == key::kLeft || ev.key == key::kRight)) {
        const int n = int(_toggles.size());
        _focus = 2 + ((_focus - 2 + (ev.key == key::kRight ? 1 : n - 1)) % n);
        keyed = _focus - 2;
    } else if (_focus >= 2 && enter) {
        keyed = _focus - 2;
    }
    // One level checked at a time (0x48c36a); Button Switch as it changes
    // to another (0x40786f).
    for (size_t i = 0; i < _toggles.size(); ++i)
        if (int(i) == keyed || _toggles[i].handleEvent(ev, _x, _y)) {
            bool changed = false;
            for (size_t j = 0; j < _toggles.size(); ++j) {
                changed = changed || (_toggles[j].checked() != (j == i));
                _toggles[j].setChecked(j == i);
            }
            if (changed && mixer && _switch)
                mixer->play(_switch, audio::SoundType::kEffects);
        }
    if ((enter && _focus == 1 && _start.press(mixer)) || _start.handleEvent(ev, _x, _y, mixer)) {
        for (size_t i = 0; i < _toggles.size(); ++i)
            if (_toggles[i].checked())
                return std::optional<Difficulty>(Difficulty(i + 1));
        // None checked: the level stays as it was (0x48c2b5).
        return std::optional<Difficulty>(_current);
    }
    if (_cancel.handleEvent(ev, _x, _y, mixer))
        return std::optional<Difficulty>();
    return std::nullopt;
}

void DifficultyWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    if (_font.loaded())
        _font.draw(screen, _x + _background.w / 2 - _font.width(kHeading) / 2, _y + kHeadingY, kHeading);
    for (const Toggle &toggle : _toggles)
        toggle.draw(screen, _x, _y);
    _cancel.draw(screen, _x, _y, now);
    _start.draw(screen, _x, _y, now);
}

} // namespace reluva::stawayteam
