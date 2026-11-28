// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"
#include "common/types.h"
#include "engines/nfs/folders.h"

#include <string_view>
#include <vector>

namespace reluva::nfs {

/** The games the engine knows, for what differs between them. */
enum class Game {
    kNfs3,  ///< Need for Speed III: Hot Pursuit, its own code in nfs3/
    kNfs2Se ///< Need for Speed II SE, its own code in nfs2se/
};

/**
 * What the shared Need for Speed code needs to know about one game. Another
 * game of the family is added with an entry here and its own detection
 * entries, not a new engine.
 */
struct GameInfo {
    Game game;
    const char *gameId;     // stable id stored in the config, e.g. "nfs3"
    const char *name;       // shown to the user
    const char *executable; // the game's program, in the install and on the CD
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

/** Why `folders` can't be `game`'s (its tracks aren't there), or success. */
Error checkFolders(const GameInfo &game, const GameFolders &folders);

} // namespace reluva::nfs
