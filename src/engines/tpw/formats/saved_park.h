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
 * grid of cells is read (each cell's kind, its attribute map's tile, what's
 * built on it and how it's drawn), and where its objects are. See docs/engines/tpw/games/simcoaster/formats/tpws.md.
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
        /**
         * What fills it: 1 path, 3 a queue, 4 an object, 8 an obstacle, 9 and 10 an attraction's entrance and
         * exit, 0 nothing.
         */
        uint8_t fill = 0;
        /** The piece it's drawn as: on a queue, 2 straight, 4 a bend, 5 where it meets the path. */
        uint8_t piece = 0;
        uint16_t angle = 0; // the piece turned, in degrees
    };

    /** What a cell's fill says. */
    ///@{
    static constexpr uint8_t kFillObject = 4;
    static constexpr uint8_t kFillObstacle = 8;
    static constexpr uint8_t kFillEntrance = 9;
    static constexpr uint8_t kFillExit = 10;
    ///@}

    /**
     * An object placed in the park: a ride, a shop, a feature, an
     * obstacle, by its number (its settings' `Info.Id`).
     */
    struct Object {
        uint16_t id = 0;
        /** Its first cell's middle, in cells: the corner its model is laid from, plus a half. */
        float x = 0;
        float y = 0;
        uint32_t angle = 0; // degrees
        /** Whether it's in the park: the game's own fixed items and spare objects are kept at (0.5, 0.5). */
        bool placed() const { return x != 0.5f || y != 0.5f; }
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
    /** The objects, in the save's order. */
    const std::vector<Object> &objects() const { return _objects; }

private:
    std::string _version;
    std::vector<uint8_t> _state;
    std::vector<Cell> _cells;
    std::vector<Object> _objects;
};

} // namespace reluva::tpw
