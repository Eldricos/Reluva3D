// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <string_view>
#include <vector>

namespace reluva::age {

// What the shared Angel Game Engine code needs to know about one game.
// Another AGE game (Midtown Madness) is added with a new entry here and
// its own detection entries, not a new engine.
struct GameInfo {
    const char* gameId; // stable id stored in the config, e.g. "mm2"
    const char* name;   // shown to the user
};

// Every game the engine knows; nullptr if `gameId` isn't one.
const std::vector<GameInfo>& games();
const GameInfo* findGame(std::string_view gameId);

// The detection table for every game, for MetaEngine::detect.
const std::vector<GameVariant>& detectionTable();
// The same games by file names only, for releases not in detectionTable()
// yet; consulted only when nothing there matches.
const std::vector<GameVariant>& fallbackTable();

} // namespace reluva::age
