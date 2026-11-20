// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/awe/game/walk_grid.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::x86 {
class Cpu;
class Win32Process;
} // namespace reluva::x86

namespace reluva::awe {

struct GameInfo;

/**
 * The game's own path-finder (motoe.exe's 0x42e840, ac.exe's 0x426620: the
 * same code), run in the x86 interpreter, as granny2.dll is: kept to check
 * Reluva's port of it (WalkGrid::search) against, with reluva-tool awe
 * path-check. See docs/engines/awe/games/motoe/formats/ac2.md.
 */
class GamePathFinder {
public:
    GamePathFinder();
    ~GamePathFinder();
    /**
     * Loads the executable and makes the path-finder as the game does. Fails
     * if `game`'s path-finder isn't known or `exe` isn't the release it was
     * read from.
     * @param exe `game`'s executable (GameInfo::exe), whose path-finder is
     *        where `game` says (GameInfo::pathFinder).
     * @param single The x87 at 24 bits once the statics are made, as in the
     *        game (setSinglePrecision); else at 53 throughout.
     */
    Error load(const std::vector<uint8_t> &exe, const GameInfo &game, bool single = true);
    /** A point of a path found: a cell's coordinates, and the heading there. */
    struct Point {
        double a = 0;
        double b = 0;
        double heading = 0;
    };
    /**
     * One search (0x42ef40) on `grid` from one cell to another, the points
     * as the game gives them (the goal first).
     * @param openThrees Cells of 3 taken as open, as the game's later tries take them.
     */
    Error search(const WalkGrid &grid, bool openThrees, WalkGrid::Cell from, WalkGrid::Cell to, bool allowTwos,
                 std::vector<Point> &out);
    /**
     * A walk's path as the game plans one (0x440ce0): cells from the start
     * to the goal; empty if there's none (or the goal is under 3 cells
     * away). `allowTwos` when walking to a walk point.
     */
    std::vector<WalkGrid::Cell> findPath(const WalkGrid &grid, WalkGrid::Cell from, WalkGrid::Cell to, bool allowTwos);
    /** Whether load() succeeded. */
    bool loaded() const { return _object != 0; }
    /**
     * The x87's precision control while the game's code runs: 53 bits
     * (Windows' default for a program), or 24 bits, as Direct3D 8 leaves
     * it after the game's CreateDevice (no D3DCREATE_FPU_PRESERVE:
     * 0x401f01).
     */
    void setSinglePrecision(bool on);

private:
    std::string _exe; // its name, for messages
    uint32_t _search = 0;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    uint32_t _object = 0;
};

} // namespace reluva::awe
