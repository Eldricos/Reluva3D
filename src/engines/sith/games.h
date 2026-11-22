// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva::sith {

/** A game on the Sith engine. */
struct GameInfo {
    const char *gameId;
    const char *title;
};

/** The games the engine knows, in the order the launcher lists them. */
const std::vector<GameInfo> &games();

/** The game with this id, or nullptr. */
const GameInfo *findGame(std::string_view gameId);

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
 * The releases on a CD 1 (or a copy of it), as the detection table and the
 * fallback table have them for an install; for importing, never for
 * running from.
 */
std::vector<DetectedGame> detectCd(const std::filesystem::path &dir);

/** The releases in `dir`: the detection table's, or else the fallback's. */
std::vector<DetectedGame> detectGames(const std::filesystem::path &dir);

} // namespace reluva::sith
