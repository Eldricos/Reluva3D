// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"

#include "common/detection.h"

#include <cstdint>
#include <filesystem>
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
    // How its client game lays out what it adds to the scene; single
    // player's, where it differs.
    RefEntityLayout refEntity, spRefEntity;
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

// What a folder holding a game (its install folder: the game folder, and
// the single-player code beside it) has of it, from the files' signatures:
// the patch level ("1.0", "1.1", "1.2"), whether single player can run and
// which release its code is from (empty if unknown), and a description
// to show ("1.2", "1.2, no single player").
struct Release {
    std::string patch;
    bool singlePlayer = false;
    std::string singlePlayerPatch;
    std::string summary;
    // The Expansion Pack: its pak (pak3.pk3), and its single-player code
    // (beside the game folder, as its own setup leaves it, or in
    // expansion/, as an import does, the 1.x code staying beside it).
    bool expansion = false;
    bool expansionSinglePlayer = false;
};
Release describeRelease(const std::filesystem::path& dir, const GameInfo& game);

// The Expansion Pack's pak, as the game folder holds it.
constexpr const char* kExpansionPak = "pak3.pk3";
// Whether `file` is the Expansion Pack's game DLL or menus DLL.
bool isExpansionDll(const std::filesystem::path& file);
// Whether `file` is the Expansion Pack's pak.
bool isExpansionPak(const std::filesystem::path& file);
// The folder whose single-player DLLs to run: with the Expansion Pack,
// its own (expansion/, or beside the game folder when those are its);
// without, the 1.x ones beside it (none when those are the expansion's).
// Empty if there are none.
std::filesystem::path singlePlayerDir(const std::filesystem::path& dir, const GameInfo& game, bool expansion);

// A file a patch brings, by its signature (as detection's): where it goes
// in the game's folder, and the patch level it gives (empty for one that
// says nothing about it, as the DLLs).
struct PatchFile {
    const char* to;
    const char* md5;
    int64_t size;
    const char* patch;
};
// The files of the game's patches that Reluva uses (Elite Force's 1.2
// patch, eliteforcepatch1_2.exe, holds them all: pak1.pk3 from 1.1,
// pak2.pk3, and single player's DLLs as 1.1 left them).
const std::vector<PatchFile>& patchFiles(const GameInfo& game);

} // namespace reluva::idtech3
