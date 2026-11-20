// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/formats/ac2.h"

#include <array>
#include <cstdint>
#include <vector>

// Where characters can walk in a camera's view: the grid in the scene's
// CAMR record, 64 x 64 cells over the floor (y = 0), centred on the
// world's origin. See docs/engines/awe/games/motoe/formats/ac2.md.
//
// Cell (a, b) runs along x with a and back along z with b; its centre is
//   x = (a - 32) * size + size / 2,  z = (32 - b) * size - size / 2
// (motoe.exe 0x406dd0; the other way, 0x406e60). Its value: 0 open, 1
// blocked, 2 blocked unless a search allows it, 3 blocked for the first
// try at a path and open after (0x42e8a0, 0x42e9e0). Scripts walk to
// cells by number: "walk 31 0".

namespace reluva::awe {

/** A camera's walk grid, and the path-finder that searches it. */
class WalkGrid {
public:
    /** Cells across and down. */
    static constexpr int kSize = 64;
    /** A cell by its numbers; (-1, -1) is none. */
    struct Cell {
        int a = -1;
        int b = -1;
        /** Whether it names a cell (on the grid or not). */
        bool valid() const { return a >= 0 && b >= 0; }
        /** Whether it's the same cell. */
        bool operator==(const Cell &o) const { return a == o.a && b == o.b; }
    };

    /** From a CAMR record; false if it has no grid. */
    bool load(const Ac2Record &camr);
    /** Whether there's no grid (none loaded, or the record had none). */
    bool empty() const { return _size <= 0; }
    /** A cell's width, in world units. */
    int cellSize() const { return _size; }
    /** A cell's value; 1 (blocked) off the grid. */
    uint8_t at(int a, int b) const { return inside(a, b) ? _cells[size_t(a * kSize + b)] : 1; }
    /** Sets a cell's value; nothing off the grid. */
    void set(Cell c, uint8_t value) {
        if (inside(c.a, c.b))
            _cells[size_t(c.a * kSize + c.b)] = value;
    }
    /** Whether (a, b) is on the grid. */
    static bool inside(int a, int b) { return a >= 0 && b >= 0 && a < kSize && b < kSize; }

    /** A cell's centre on the floor, in the world. */
    void centre(Cell c, float &x, float &z) const;
    /**
     * The cell holding (x, z), or an invalid one off the grid. A point on
     * a boundary goes to the higher a and the higher b, as in the game.
     */
    Cell cellAt(float x, float z) const;
    /** Whether a cell stops a search: off the grid, 1, 3 unless `openThrees`, 2 unless `openTwos`. */
    bool blocked(Cell c, bool openThrees, bool openTwos = false) const;

    /**
     * A walk's path as the game plans one (motoe.exe 0x440ce0): cells from
     * the start to the goal; empty if there is none. It tries three
     * times, each less strict: as the grid is, then with 3s open, then
     * also with the start and goal cells open; and doesn't walk to a cell
     * less than 3 steps away (in a plus b). `allowTwos` when walking to a
     * walk point.
     */
    std::vector<Cell> findPath(Cell from, Cell to, bool allowTwos = false) const;

    /** A point of a path: a cell's centre, in cells, and the heading of the turn into it. */
    struct Point {
        double a = 0;
        double b = 0;
        double heading = 0;
    };
    /**
     * One search as the game's path-finder makes it (0x42ef40): A* over
     * the cells, then the points dropped that a turn and a straight line
     * can go past (see docs/engines/awe/games/motoe/formats/ac2.md). `out`
     * gets the points left, the goal first and the start left out.
     * @return How many, or 0 or less if there's no path.
     */
    int search(Cell from, Cell to, bool openThrees, bool allowTwos, std::vector<Point> &out) const;
    /**
     * The x87's precision the search follows: 24 bits, as in the game
     * (the default), or 53 (for comparing with the game's code run so).
     */
    static void setSinglePrecision(bool on);

private:
    int _size = 0;
    std::array<uint8_t, kSize * kSize> _cells{};
};

} // namespace reluva::awe
