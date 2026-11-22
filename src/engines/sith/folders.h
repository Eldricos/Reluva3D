// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva::sith {

/** Jedi Knight came on two CDs; the install holds neither CD's cut scenes. */
constexpr int kCdCount = 2;

/**
 * Which of Jedi Knight's CDs `dir` is, or a copy of: 1 or 2, by its
 * `GAMEDATA/RESOURCE/JK_.CD`; 0 when it's neither.
 */
int cdNumber(const std::filesystem::path &dir);

/**
 * Where the game's files are: the install, and each CD's files where
 * they were found (empty when not).
 */
struct GameFolders {
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
 * Every cut scene (.smk) in the CDs' GAMEDATA/RESOURCE/VIDEO folders, CD 1's
 * then CD 2's, each sorted by name.
 */
std::vector<CutScene> findCutScenes(const GameFolders &folders);

} // namespace reluva::sith
