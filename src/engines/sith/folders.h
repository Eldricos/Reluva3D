// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/sith/games.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * Which of `game`'s CDs `dir` is, or a copy of: 1 or 2, by its
 * `GAMEDATA/RESOURCE/JK_.CD`; 0 when it's none of them.
 */
int cdNumber(const GameInfo &game, const std::filesystem::path &dir);

/** The same, from the marker's bytes (all of them: 4 to be one). */
int cdNumber(const GameInfo &game, const std::vector<uint8_t> &mark);

/**
 * Where the game's files are: the install, and each CD's files where
 * they were found (empty when not). The install doesn't hold the CDs'
 * cut scenes.
 */
struct GameFolders {
    /** The game whose program is in the install; Jedi Knight's where none is. */
    const GameInfo *game = nullptr;
    std::filesystem::path install;
    std::array<std::filesystem::path, kCdCount> cds;
    // How each CD was found, for messages.
    std::array<std::string, kCdCount> cdSources;
};

/**
 * Each CD's folder for an install, the first of:
 * - the target's `cd1path` or `cd2path` setting (relative to the install if
 *   not absolute); when it's set but isn't that CD, that CD isn't used at
 *   all;
 * - the install itself, or a folder in it (where *Import Game...* puts
 *   them: `cd1`, `cd2`);
 * - a folder beside the install.
 */
GameFolders findFolders(const std::filesystem::path &install, const std::map<std::string, std::string> &settings);

/** A cut scene on one of the CDs. */
struct CutScene {
    /** Its file's name, as on the CD ("23A.SMK"). */
    std::string name;
    /** Which CD it's on: 1 or 2. */
    int cd = 0;
    std::filesystem::path path;
};

/**
 * Every Smacker cut scene (.smk, Jedi Knight's) in the CDs'
 * GAMEDATA/RESOURCE/VIDEO folders, CD 1's
 * then CD 2's, each sorted by name.
 */
std::vector<CutScene> findCutScenes(const GameFolders &folders);

} // namespace reluva::sith
