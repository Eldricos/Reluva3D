// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/game/map.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::stawayteam {

/**
 * The random numbers the ground's fractal is made with: R250, each number
 * the XOR of those 250 and 147 before it (0x4abf1f), its first 250 made
 * by a linear congruential generator from a seed (0x54c040)
 * (docs/engines/stawayteam/systems/maps.md#the-fractal).
 */
class R250 {
public:
    explicit R250(uint32_t seed);

    uint32_t next();

private:
    std::array<uint32_t, 250> _table{};
    int _j = 0;
    int _k = 103;
};

/**
 * One of the 256 generators the program makes as a map starts, the nth
 * seeded with 946415 (n + 1) (0x536991); a corner's seed picks one.
 */
const R250 &groundGenerator(int n);

/** The side of the ground's cells in pixels, and so of their fractal less one. */
constexpr int kGroundCell = 64;

/**
 * A cell's ground values, (kGroundCell + 1) square, row by row: its four
 * corners' values, spread over it by midpoint displacement, its edges by
 * the seeds of the corners they start from so that the cells beside agree
 * (0x538310).
 */
std::vector<uint8_t> groundFractal(const GroundMap &ground, int cx, int cy);

} // namespace reluva::stawayteam
