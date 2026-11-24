// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A saved park (`.TPWS`), as SimCoaster's `Data/PreBuilt/prebuilt.TPWS`:
 * a header, then the park's state, deflated. Of the state only the park's
 * grid of cells is read: each cell's kind, its attribute map's tile and
 * what's built on it. See docs/engines/tpw/games/simcoaster/formats/tpws.md.
 */
class SavedPark {
public:
    /** The grid's cells a side. */
    static constexpr int kSide = 128;

    /** What's built on a cell, by the save. */
    enum class Built : uint8_t { kNothing = 0, kPath = 1, kQueue = 2 };

    /** A cell of the grid: what's known of its record. */
    struct Cell {
        uint8_t kind = 0;  // 3, or 7 for the cells with an extra entry
        uint8_t flags = 0; // the kind's high byte, not worked out
        uint8_t tile = 0;  // the world's attribute map's tile, as base.map has it
        Built built = Built::kNothing;
    };

    /**
     * Reads the save: its version, the state inflated, and the grid found
     * in it. Fails without the state's chunk, on a stream that won't
     * inflate to its size, or without the grid.
     */
    Error parse(const std::vector<uint8_t> &data);

    /** The version it was saved by: "Beta_21". */
    const std::string &version() const { return _version; }
    /** The state, inflated. */
    const std::vector<uint8_t> &state() const { return _state; }
    /** Cell (x, y), as the park's settings number them; both 0 to kSide − 1. */
    const Cell &cell(int x, int y) const { return _cells[size_t(y) * kSide + size_t(x)]; }

private:
    std::string _version;
    std::vector<uint8_t> _state;
    std::vector<Cell> _cells;
};

} // namespace reluva::tpw
