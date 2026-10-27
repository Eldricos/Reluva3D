// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"

#include "common/detection.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// What the shared id Tech 3 code needs to know about one game. Everything
// game-specific the engine does is keyed off this, so another id Tech 3
// game (Quake III Arena, Jedi Outcast...) is added with a new entry here
// and its own detection entries, not a new engine.
//
// Whose game code it is: which of Reluva's hosts can run the game's own
// programs (its QVMs and DLLs, whose engine calls are numbered and laid
// out per game). None: its files can be looked at, but not played.
enum class GameCode { None, EliteForce };

// A player model's three parts ("model/skin").
struct PlayerModels {
    std::string head, torso, legs;
};

struct GameInfo {
    std::string gameId;  // stable id stored in the config, e.g. "stvef"
    std::string name;    // shown to the user
    std::string baseDir; // the folder holding the game's own .pk3 files
    GameCode code = GameCode::None;
    // How its client game lays out what it adds to the scene.
    RefEntityLayout refEntity;
    // The command's button bits for attacking (and the second attack,
    // where the game has one; 0 if not).
    int32_t buttonAttack = 1, buttonAltAttack = 0;
    // Multiplayer's model for a player who hasn't chosen one.
    std::string playerModel;
    // Single player's: the player's name until one is set; its game code
    // and menus (native DLLs beside the
    // game's folder; empty if it has none), the first level, levels that
    // aren't single player's (by name prefix), and the player as each sex.
    std::string spPlayerName, spGameDll, spUiDll, firstMap;
    std::vector<std::string> multiplayerMapPrefixes;
    PlayerModels malePlayer, femalePlayer;
    // Its movies: video/<name> and their file type.
    std::string cinematicExtension;
};

// Every game the engine knows; nullptr if `gameId` isn't one.
const std::vector<GameInfo>& games();
const GameInfo* findGame(std::string_view gameId);

// The detection table for every game, for MetaEngine::detect.
const std::vector<GameVariant>& detectionTable();
// The same games by file names only, for releases not in detectionTable()
// yet; consulted only when nothing there matches.
const std::vector<GameVariant>& fallbackTable();

} // namespace reluva::idtech3
