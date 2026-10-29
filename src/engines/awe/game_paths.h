// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/awe/walk_grid.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::x86 {
class Cpu;
class Win32Process;
} // namespace reluva::x86

namespace reluva::awe {

// motoe.exe's path-finder (0x42e840), run in the x86 interpreter, as
// granny2.dll is: kept to check Reluva's port of it (WalkGrid::search)
// against, with reluva-tool awe-path. See docs/formats/awe/ac2.md.
class GamePathFinder {
public:
    GamePathFinder();
    ~GamePathFinder();
    // `single`: the x87 at 24 bits once the statics are made, as in the
    // game (setSinglePrecision); else at 53 throughout.
    Error load(const std::vector<uint8_t>& exe, bool single = true);
    struct Point {
        double a = 0, b = 0, heading = 0;
    };
    // One search (0x42ef40) on `grid` from one cell to another.
    Error search(const WalkGrid& grid, bool openThrees, WalkGrid::Cell from, WalkGrid::Cell to, bool allowTwos,
                 std::vector<Point>& out);
    // A walk's path as the game plans one (0x440ce0): cells from the start
    // to the goal; empty if there's none (or the goal is under 3 cells
    // away). `allowTwos` when walking to a walk point.
    std::vector<WalkGrid::Cell> findPath(const WalkGrid& grid, WalkGrid::Cell from, WalkGrid::Cell to,
                                         bool allowTwos);
    bool loaded() const { return object_ != 0; }
    // The x87's precision control while the game's code runs: 53 bits
    // (Windows' default for a program), or 24 bits, as Direct3D 8 leaves
    // it after the game's CreateDevice (no D3DCREATE_FPU_PRESERVE:
    // 0x401f01).
    void setSinglePrecision(bool on);

private:
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    uint32_t object_ = 0;
};

} // namespace reluva::awe
