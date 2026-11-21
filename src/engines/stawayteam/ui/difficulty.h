// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace reluva::stawayteam {

/** The game's difficulty levels, as the program numbers them (0x48c2ff). */
enum class Difficulty { kEasy = 1, kNormal = 2 };

/**
 * The difficulty window, which Start New Game opens over the main menu:
 * "Select a difficulty level", a toggle for each level, START GAME and
 * CANCEL (docs/engines/stawayteam/systems/menus.md#the-difficulty-window).
 */
class DifficultyWindow {
public:
    /**
     * The window on a screen of that size, `current` checked; `data` must
     * outlive it. From the settings window its START GAME is CONTINUE.
     */
    DifficultyWindow(const Archive &data, int screenW, int screenH, Difficulty current, bool fromSettings = false);

    /**
     * What `ev` does to it: once it's closed, the level chosen (START
     * GAME), or nullopt inside an optional (CANCEL); until then nothing.
     */
    std::optional<std::optional<Difficulty>> handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

private:
    image::Image _background;
    Font _font;       // the window's, the heading's and the toggles'
    Font _buttonFont; // the buttons'
    std::vector<Toggle> _toggles;
    PictureButton _start;
    PictureButton _cancel;
    Difficulty _current;
    int _x;
    int _y;
};

} // namespace reluva::stawayteam
