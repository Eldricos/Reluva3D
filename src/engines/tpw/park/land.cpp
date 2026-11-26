// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's land, inside its fence: the world's to start with, more as
// missions are done. See docs/engines/tpw/games/simcoaster/systems/land.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <string>
#include <vector>

namespace reluva::tpw {

namespace {

// hoardings.map: a byte for each of the world's 128 × 128 cells, 0 where
// it's the park's.
constexpr int kHoardingsSide = 128;

} // namespace

void Park::loadLand(const SearchSet &files) {
    _owned.assign(_ground.size(), 1);
    std::vector<uint8_t> data;
    if (files.read(_folder + "/hoardings.map", data) || data.size() != size_t(kHoardingsSide) * kHoardingsSide)
        return;
    // Laid out as the attribute map is: row x, column y.
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y)
            _owned[size_t(y) * size_t(_mapWidth) + size_t(x)] =
                x < kHoardingsSide && y < kHoardingsSide && data[size_t(x) * kHoardingsSide + size_t(y)] == 0;
}

// The clicks are the fence's corners, from a point by the entrance: x 0
// four cells past the entrance's second cell, the eight-cell gap the
// fence leaves (x −8 to 0) about the entrance; y 0 the cell past it. So
// every mission's fence lies along the worlds' walls (inferred).
void Park::growLand(const std::vector<Cell> &clicks) {
    if (clicks.size() < 3 || _fixed.entrance.size() < 2 || _owned.empty())
        return;
    const Cell origin{_fixed.entrance[1].x + 4, _fixed.entrance[0].y + 1};
    const size_t n = clicks.size();
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y) {
            // A cell's the park's if its middle's inside the fence.
            const float px = float(x - origin.x) + 0.5f;
            const float py = float(y - origin.y) + 0.5f;
            bool inside = false;
            for (size_t i = 0; i < n; ++i) {
                const Cell &a = clicks[i];
                const Cell &b = clicks[(i + 1) % n];
                if ((float(a.y) > py) != (float(b.y) > py) &&
                    px < float(a.x) + (py - float(a.y)) * float(b.x - a.x) / float(b.y - a.y))
                    inside = !inside;
            }
            if (inside)
                _owned[size_t(y) * size_t(_mapWidth) + size_t(x)] = 1;
        }
}

bool Park::pathLaid(const Cell &cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= _mapWidth || cell.y >= _mapHeight)
        return false;
    return _ground[size_t(cell.y) * size_t(_mapWidth) + size_t(cell.x)] == Ground::kPath;
}

bool Park::owns(const Cell &cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= _mapWidth || cell.y >= _mapHeight)
        return false;
    return _owned.empty() || _owned[size_t(cell.y) * size_t(_mapWidth) + size_t(cell.x)] != 0;
}

} // namespace reluva::tpw
