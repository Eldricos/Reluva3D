// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace reluva::legoisland {

/** Frames a second when the settings don't say: a display's. */
constexpr int kDefaultFrameRate = 60;

/**
 * How a game is played, from the launcher's settings for it (the engine's
 * options, MetaEngine::engineOptions).
 */
struct PlaySettings {
    int frameRate = kDefaultFrameRate; // at most so many a second; 0, no limit
    bool turningByTime = true;
    bool joystick = false;
    // The program's registry settings, as CONFIG.EXE writes them
    // (GameProcess::setSetting): only those set, the rest left at the
    // program's own defaults (but for the island's model, GameProcess's).
    std::vector<std::pair<std::string, std::string>> registry;
};

/** The game's PlaySettings from its settings (GameTarget::settings). */
PlaySettings playSettings(const std::map<std::string, std::string> &settings);

} // namespace reluva::legoisland
