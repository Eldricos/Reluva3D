// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::legoisland {

/** The game's id, as `kVariants` and the launcher's config have it. */
inline constexpr const char *kGameId = "legoisland";

/**
 * The releases of LEGO Island in `dir`: those `kVariants` knows by their
 * files' hashes, else the game by its files' names as an unknown release.
 */
std::vector<DetectedGame> detectGame(const std::string &engineId, const std::filesystem::path &dir);

/**
 * One of the two parts of LEGO Island a folder can hold: the game's own
 * files as the installer copies them (an install, or the CD's DATA/DISK
 * folder), or the SI archives the game streams from the CD's LEGO/SCRIPTS
 * folder. The game runs from both together.
 */
struct GamePart {
    /** Which part it is. */
    enum Kind { kInstall, kScripts } kind;
    /** The folder holding LEGO1.DLL (an install), or LEGO/ (the CD's scripts). */
    std::filesystem::path dir;
};

/**
 * The parts in `dir`, in its DATA/DISK folder, and in the folders directly
 * inside it (an install and a copy of the CD side by side): one of each
 * kind, the first found, `dir` itself before its folders.
 */
std::vector<GamePart> findGameParts(const std::filesystem::path &dir);

/**
 * Adds a part's files to `plan`, laid out as one folder the game runs
 * from: the install's files where they are, the CD's SI archives in its
 * lego/Scripts folder. A file the plan already has is left as it is.
 */
void addPartToImport(ImportPlan &plan, const GamePart &part);

/** Sets `plan`'s notes: what it holds, and what the game still lacks. */
void noteImport(ImportPlan &plan);

/** The SI archives on the English 1.0 CD, from its LEGO/SCRIPTS folder. */
const std::vector<std::string> &cdScripts();

} // namespace reluva::legoisland
