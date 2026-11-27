// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace reluva::stawayteam {

/**
 * The settings window, which Settings opens over the main menu: the
 * screen's brightness, scrolling and pausing when hit, each changed in
 * `options` as it's set, and buttons to the keyboard's and sound's
 * settings, back to the defaults, and to the difficulty
 * (docs/engines/stawayteam/systems/menus.md#the-settings-window).
 */
class SettingsWindow {
public:
    /** What the player chose that leaves the window. */
    enum class Choice { kNone, kContinue, kDifficulty, kKeyboard, kSound };

    /**
     * The window on a screen of that size, showing `options`; `data` and
     * `options` must outlive it.
     */
    SettingsWindow(const Archive &data, GameOptions &options, int screenW, int screenH);

    /** What `ev` does to it: kContinue once it's to close; the others, a window to open over it. */
    Choice handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

private:
    // The sliders set from `_options` (0x486b30).
    void showOptions();

    GameOptions &_options;
    image::Image _background;
    Font _buttonFont; // Placar Condensed
    Font _font;       // Trek Small: the labels', sliders' and check box's
    PictureButton _keyboard;
    PictureButton _sound;
    std::array<std::string, 3> _labels;
    std::array<int, 3> _labelY{};
    std::array<Slider, 3> _sliders;
    CheckBox _pause;
    std::shared_ptr<audio::Sound> _switch; // UI/Button Switch.wav
    PictureButton _continue;
    PictureButton _reset;
    PictureButton _difficulty;
    int _x;
    int _y;
    int _labelX = 0;
};

} // namespace reluva::stawayteam
