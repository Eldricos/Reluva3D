// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include <array>
#include <cstdint>
#include <string>

namespace reluva::stawayteam {

/**
 * The sound's settings, which the settings window's CONFIGURE SOUND opens
 * (0x56e8f0): the master, effects' and music's volumes, each with a mute
 * button, and voiceovers, stereo and mixing, two buttons each; each
 * change made at once to `options` and `mixer`
 * (docs/engines/stawayteam/systems/menus.md#the-sound-window).
 */
class SoundOptionsWindow {
public:
    /**
     * The window on a screen of that size; `data` and `options` must
     * outlive it.
     */
    SoundOptionsWindow(const Archive &data, GameOptions &options, int screenW, int screenH);

    /** What `ev` does to it, the volumes set on `mixer` (nullptr: none): true once it's to close. */
    bool handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

private:
    // The buttons and sliders set from `_options` (0x56eb41...).
    void showOptions();

    GameOptions &_options;
    image::Image _background;
    Font _font;       // Trek Small: the labels' and sliders'
    Font _pairFont;   // Small Blue: the paired buttons' labels
    Font _buttonFont; // Placar Condensed: CONTINUE's
    std::array<std::string, 6> _labels;
    std::array<int, 6> _labelX{};
    int _labelY0 = 0;
    std::array<Slider, 3> _sliders; // master, effects, music
    ButtonGroup _buttons;           // 3 mutes, then ON, OFF, STEREO, MONO, FASTER, BETTER
    PictureButton _continue;
    int _x;
    int _y;
};

} // namespace reluva::stawayteam
