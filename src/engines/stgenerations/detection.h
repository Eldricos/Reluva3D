// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::stgenerations {

// The parts of Star Trek: Generations a folder can hold: the game as its
// setup installed it (sgens.exe and DATA/), and each of its two CDs (their
// GENS/ folders: MUSIC.1 is on disc 1, MUSIC.2 on disc 2). The installed
// game is what runs; the CDs add the story (STELLAR.DAT), space combat
// (COMBAT.DAT), the music and the movies.
struct GamePart {
    enum Kind { kInstall, kDisc1, kDisc2 } kind;
    std::filesystem::path dir; // the install's folder, or the disc's (holding GENS/)
};

// The parts in `dir`, and in the folders directly inside it (an install
// and its CDs side by side, or an import's disc1/ and disc2/).
std::vector<GamePart> findGameParts(const std::filesystem::path &dir);

// An installed game in `dir` itself: which release, by kVariants' files;
// else, an executable naming the game, as a release Reluva doesn't know
// (`unknown` set). Nothing if there's no installed game there.
std::vector<DetectedGame> detectInstall(const std::string &engineId, const std::vector<GameVariant> &variants,
                                        const std::filesystem::path &dir, bool *unknown = nullptr);

// Copying an install and its CDs into a folder to run from: the install as
// it is, then from each CD the files the setup left on it (the story, space
// combat, the music and the movies) into disc1/GENS/ or disc2/GENS/.
void addPartToImport(ImportPlan &plan, const GamePart &part);
// The plan's notes: what it copies, and what's still missing.
void noteImport(ImportPlan &plan);

} // namespace reluva::stgenerations
