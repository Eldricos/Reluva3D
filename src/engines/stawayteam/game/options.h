// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/config.h"
#include "engines/stawayteam/game/controls.h"

#include <filesystem>
#include <string>

namespace reluva::stawayteam {

/**
 * The game's options, as its settings windows change them and the game
 * keeps them between runs: under "Options" (0x5bc607) scrolling, pausing
 * when a team member is hit off the screen, and the screen's brightness;
 * and the sound's (0x56e7b5), the volumes in hundredths of a decibel.
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
    // The sound's, the volumes in hundredths of a decibel (0x56e7b5).
    static constexpr int kVolumeMin = -3333; // muted
    static constexpr int kMusicVolumeDefault = -800;
    static constexpr float kEffectsVolumeMuted = 0.25f;
    static constexpr int kMixingFaster = 32;
    static constexpr int kMixingBetter = 5;

    /** Reads them from `file`; one that isn't there leaves the defaults. */
    void load(const std::filesystem::path &file);
    /** Writes them back to the file load() read; nothing without one. */
    void save() const;

    float scrollSpeed = kScrollSpeedDefault;
    float scrollAccel = kScrollAccelDefault;
    bool pauseWhenHit = true;
    bool showPauseHelp = true;         // the First Hit Action not yet run (0x5ca39b)
    float brightness = kBrightnessMin; // the screen's gamma

    int masterVolume = 0;
    bool masterMute = false;
    float effectsVolume = 1.0f; // 0.5 to 1, a scale
    bool effectsMute = false;
    int musicVolume = kMusicVolumeDefault;
    bool musicMute = false;
    bool playVoiceovers = true;
    bool stereo = true;
    int mixingChannels = kMixingFaster;

    std::string netPlayerName; // the name last typed in Choose Netgame

    /** The keys and mouse buttons the game's actions are bound to. */
    Controls controls;

    /**
     * The sound's options on `mixer`: the master volume over music and
     * effects, a muted one at its least (0x56f7b1).
     */
    void applySound(audio::Mixer &mixer) const;

private:
    std::filesystem::path _path;
    Config _file;
    bool _loaded = false;
};

} // namespace reluva::stawayteam
