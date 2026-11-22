// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <string_view>
#include <vector>

namespace reluva::tpw {

/**
 * What the shared Theme Park World engine code needs to know about one
 * game. Another game on the engine (Theme Park World itself) is added
 * with a new entry here and its own detection entries, not a new engine.
 */
struct GameInfo {
    const char *gameId; // stored in the config, e.g. "simcoaster"
    const char *name;   // shown to the user
};

/** Every game the engine knows. */
const std::vector<GameInfo> &games();
/** The game with that id, or nullptr if `gameId` isn't one. */
const GameInfo *findGame(std::string_view gameId);

/** The detection table for every game, for MetaEngine::detect. */
const std::vector<GameVariant> &detectionTable();
/**
 * The same games by file names only, for releases not in detectionTable()
 * yet; consulted only when nothing there matches.
 */
const std::vector<GameVariant> &fallbackTable();

} // namespace reluva::tpw
