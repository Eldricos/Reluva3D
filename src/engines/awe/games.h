// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/detection.h"
#include "common/types.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace reluva::awe {

/**
 * What the shared AWE code needs to know of one game. A game's own code is
 * in its folder (motoe/, attwn/); everything here is what the rest of the
 * engine asks of it.
 */
struct GameInfo {
    const char *id;                    ///< as the launcher stores it: "motoe"
    const char *title;                 ///< the window's, as the original's
    std::vector<GameVariant> variants; ///< its releases, by their files' signatures
    std::vector<GameVariant> fallback; ///< any other release: the same files, whatever their contents
    bool playable = false;             ///< whether reluva plays it yet (else the data viewer's only)
    /** Its install's files as the game opens them; null: the folder as it is. */
    Error (*openFiles)(const std::filesystem::path &dir, std::unique_ptr<Archive> &out) = nullptr;
    int width = 1024; ///< the game's frame: its window, its scenes' pictures and cameras
    int height = 768;
};

/** The AWE games, in the order they're recognised. */
const std::vector<const GameInfo *> &games();

/** The game whose id is `id`, or nullptr. */
const GameInfo *findGame(std::string_view id);

/** The AWE games in `dir`: every game's releases first, then the fallbacks. */
std::vector<DetectedGame> detectGames(const std::filesystem::path &dir);

/** `game`'s files in `dir`, as it opens them (the folder as it is without a game). */
Error openGameFiles(const GameInfo *game, const std::filesystem::path &dir, std::unique_ptr<Archive> &out);

/**
 * The files of the AWE game in `dir`, whichever it is, as it opens them;
 * the folder as it is if none is recognised (`game` then null).
 */
Error openInstall(const std::filesystem::path &dir, std::unique_ptr<Archive> &out, const GameInfo **game = nullptr);

} // namespace reluva::awe
