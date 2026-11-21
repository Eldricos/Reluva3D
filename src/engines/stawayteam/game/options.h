// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"

#include <filesystem>

namespace reluva::stawayteam {

/**
 * The game's options, as its settings window changes them and the game
 * keeps them between runs, under "Options" (0x5bc607): scrolling, pausing
 * when a team member is hit off the screen, and the screen's brightness.
 * Kept in `options.ini` where Reluva keeps the game's files
 * (docs/engines/stawayteam/systems/menus.md#the-settings-window).
 */
class GameOptions {
public:
    // The program's defaults and ranges (0x5bc628, 0x62de44...).
    static constexpr float kScrollSpeedDefault = 450.0f;
    static constexpr float kScrollSpeedMin = 112.5f;
    static constexpr float kScrollSpeedMax = 1800.0f;
    static constexpr float kScrollAccelDefault = 15.0f;
    static constexpr float kScrollAccelMin = 7.5f;
    static constexpr float kScrollAccelMax = 75.0f;
    static constexpr float kBrightnessMin = 1.0f;
    static constexpr float kBrightnessMax = 1.5f;

    /** Reads them from `file`; one that isn't there leaves the defaults. */
    void load(const std::filesystem::path &file);
    /** Writes them back to the file load() read; nothing without one. */
    void save() const;

    float scrollSpeed = kScrollSpeedDefault;
    float scrollAccel = kScrollAccelDefault;
    bool pauseWhenHit = true;
    float brightness = kBrightnessMin; // the screen's gamma

private:
    std::filesystem::path _path;
    Config _file;
    bool _loaded = false;
};

} // namespace reluva::stawayteam
