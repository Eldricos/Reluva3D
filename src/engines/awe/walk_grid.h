// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ac2.h"

#include <array>
#include <cstdint>
#include <vector>

// Where characters can walk in a camera's view: the grid in the scene's
// CAMR record, 64 x 64 cells over the floor (y = 0), centred on the
// world's origin. See docs/formats/awe/ac2.md.
//
// Cell (a, b) runs along x with a and back along z with b; its centre is
//   x = (a - 32) * size + size / 2,  z = (32 - b) * size - size / 2
// (motoe.exe 0x406dd0; the other way, 0x406e60). Its value: 0 open, 1
// blocked, 2 blocked unless a search allows it, 3 blocked for the first
// try at a path and open after (0x42e8a0, 0x42e9e0). Scripts walk to
// cells by number: "walk 31 0".

namespace reluva::awe {

class WalkGrid {
public:
    static constexpr int kSize = 64;
    struct Cell {
        int a = -1, b = -1;
        bool valid() const { return a >= 0 && b >= 0; }
        bool operator==(const Cell& o) const { return a == o.a && b == o.b; }
    };

    // From a CAMR record; false if it has no grid.
    bool load(const Ac2Record& camr);
    bool empty() const { return size_ <= 0; }
    int cellSize() const { return size_; }
    uint8_t at(int a, int b) const { return inside(a, b) ? cells_[size_t(a * kSize + b)] : 1; }
    void set(Cell c, uint8_t value) {
        if (inside(c.a, c.b))
            cells_[size_t(c.a * kSize + c.b)] = value;
    }
    static bool inside(int a, int b) { return a >= 0 && b >= 0 && a < kSize && b < kSize; }

    void centre(Cell c, float& x, float& z) const;
    // The cell holding (x, z), or an invalid one off the grid. A point on
    // a boundary goes to the higher a and the higher b, as in the game.
    Cell cellAt(float x, float z) const;
    // Whether a cell stops a search: off the grid, 1, 3 unless
    // `openThrees`, 2 unless `openTwos`.
    bool blocked(Cell c, bool openThrees, bool openTwos = false) const;

    // A walk's path as the game plans one (motoe.exe 0x440ce0): cells from
    // the start to the goal; empty if there is none. It tries three
    // times, each less strict: as the grid is, then with 3s open, then
    // also with the start and goal cells open; and doesn't walk to a cell
    // less than 3 steps away (in a plus b). `allowTwos` when walking to a
    // walk point.
    std::vector<Cell> findPath(Cell from, Cell to, bool allowTwos = false) const;

    // One search as the game's path-finder makes it (0x42ef40): A* over
    // the cells, then the points dropped that a turn and a straight line
    // can go past (see docs/formats/awe/ac2.md). `out` gets the points
    // left, the goal first and the start left out: cell centres, and the
    // heading of the turn into each. Returns how many, or 0 or less if
    // there's no path.
    struct Point {
        double a = 0, b = 0, heading = 0;
    };
    int search(Cell from, Cell to, bool openThrees, bool allowTwos, std::vector<Point>& out) const;

private:
    int size_ = 0;
    std::array<uint8_t, kSize * kSize> cells_{};
};

} // namespace reluva::awe
