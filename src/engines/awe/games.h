// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/detection.h"
#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::awe {

struct GameState;

/** A function in a game's executable, and its first bytes there (to be sure it's that release's). */
struct CodeAt {
    uint32_t address = 0;
    uint8_t bytes[8] = {};
};

/**
 * Where a game's executable has its path-finder, to run it in the x86
 * interpreter beside Reluva's port (GamePathFinder, reluva-tool awe
 * path-check). Both games' is the same code.
 */
struct PathFinderCode {
    CodeAt statics;     ///< a static of its own, made at start-up
    CodeAt constructor; ///< the object's
    CodeAt buildTables; ///< its tables, built once
    CodeAt search;      ///< one search on a grid
    CodeAt operatorNew; ///< the C library's, replaced by the interpreter's
    CodeAt operatorDelete;
    uint32_t objectSize = 0;
};

/**
 * How a game's dialogue tables are read: the columns by position, the same
 * in both games up to Povent (the 14th), and what the rest are. Orient
 * Express's are the defaults (motoe.exe 0x438f00).
 */
struct DialogueTables {
    bool target = true;      ///< the 7th column the speaker's Target (head turns); else read and dropped
    bool duration = true;    ///< the 15th a line's time ("a, b"); else unread
    bool optional = true;    ///< the 16th Optional; else unread
    bool translation = true; ///< a Line Translation other than "0" said in the line's place
    int wrap = 944;          ///< the box's width in pixels, past which a line wraps
    const char *events = "text/dialogue/events/"; ///< a line's event files, <name>_<act>_<code>.txt
};

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
    /**
     * A condition word only this game has (`words[0]`, lower case): sets
     * `holds` and returns true, or returns false for a word it hasn't.
     */
    bool (*condition)(const GameState &state, const std::vector<std::string> &words, bool &holds) = nullptr;
    /** A script verb only this game has that changes its state alone: done, or false for one it hasn't. */
    bool (*verb)(GameState &state, const std::vector<std::string> &words) = nullptr;
    DialogueTables dialogue;   ///< its dialogue tables' layout
    const char *exe = nullptr; ///< its executable, in the game's folder
    PathFinderCode pathFinder; ///< in that executable (the English release's)
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
