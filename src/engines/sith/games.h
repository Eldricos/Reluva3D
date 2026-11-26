// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva::sith {

/** The most CDs a game uses: Jedi Knight's two. */
constexpr int kCdCount = 2;

/** A game on the Sith engine, and what tells its program and CDs. */
struct GameInfo {
    const char *gameId;
    const char *title;
    /** The program, in the install and in the CD's GAMEDATA/EXE. */
    const char *program;
    /** The key the program keeps its settings in, under HKEY_LOCAL_MACHINE. */
    const char *registryKey;
    /**
     * Each CD the game asks for, by its GAMEDATA/RESOURCE/JK_.CD; all
     * zero where there's none. Mysteries of the Sith's second is Jedi
     * Knight's CD 2, which it asks for once.
     */
    std::array<std::array<uint8_t, 4>, kCdCount> cdMarks;
    /** Each CD's volume label. */
    std::array<const char *, kCdCount> cdLabels;
};

/** The games the engine knows, in the order the launcher lists them. */
const std::vector<GameInfo> &games();

/** The game with this id, or nullptr. */
const GameInfo *findGame(std::string_view gameId);

/** The game whose program is in `install`, or nullptr. */
const GameInfo *gameInstalled(const std::filesystem::path &install);

/**
 * The releases recognised in an install, each by its files' signatures
 * (`reluva --hash`).
 */
const std::vector<GameVariant> &detectionTable();

/**
 * The games found by their files' names alone, as "(unknown release)":
 * allowed to run, never promised to.
 */
const std::vector<GameVariant> &fallbackTable();

/**
 * The releases on a game's first CD (or a copy of it, or its image), as
 * the detection table and the fallback table have them for an install;
 * for importing, never for running from. `files` finds a file on the CD.
 */
std::vector<DetectedGame> detectCd(const FileLookup &files);

/** The releases in `dir`: the detection table's, or else the fallback's. */
std::vector<DetectedGame> detectGames(const std::filesystem::path &dir);

} // namespace reluva::sith
