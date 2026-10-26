// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <string_view>
#include <vector>

namespace reluva::nfs {

// What the shared Need for Speed code needs to know about one game. NFS III
// and High Stakes share most of their formats, so another game is added
// with a new entry here and its own detection entries, not a new engine.
struct GameInfo {
    const char* gameId;     // stable id stored in the config, e.g. "nfs3"
    const char* name;       // shown to the user
    const char* executable; // the game's program, in the install and on the CD
};

// Every game the engine knows; nullptr if `gameId` isn't one.
const std::vector<GameInfo>& games();
const GameInfo* findGame(std::string_view gameId);

// The detection table for every game, for MetaEngine::detect.
const std::vector<GameVariant>& detectionTable();
// The same games by file names only, for releases not in detectionTable()
// yet; consulted only when nothing there matches.
const std::vector<GameVariant>& fallbackTable();

} // namespace reluva::nfs
