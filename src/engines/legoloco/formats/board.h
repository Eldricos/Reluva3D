// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace reluva::legoloco {

/** A minifigure in a piece: one of its five slots. */
struct BoardMinifig {
    uint16_t resource = 0; // 0 for an empty slot
    uint32_t value = 0;
    std::string name;
};

/** A piece on the board: a 128-byte record. */
struct BoardPiece {
    uint16_t resource = 0;
    uint16_t column = 0;
    uint16_t row = 0;
    uint32_t value8 = 0;  // at +8: what the game gives the piece's +0x1c method
    uint32_t value12 = 0; // at +12
    std::string name;     // the player's name for it ("Building" until named)
    std::array<BoardMinifig, 5> minifigs;
};

/** A train: a 44-byte record, its name at +32. */
struct BoardTrain {
    std::array<uint8_t, 32> fields{};
    std::string name;
};

/**
 * A board (`.sav`): the ready-made layouts, the screen saver's, the area
 * layouts in the resource file, and the player's saves
 * (docs/engines/legoloco/formats/sav.md).
 */
struct Board {
    uint16_t version = 0; // 8 in every file seen
    uint16_t columns = 0;
    uint16_t rows = 0;
    uint32_t declaredPieces = 0;  // what the header says; a layout may hold fewer
    std::string backdrop;         // a picture's name in art-res\backdrop, or empty
    std::vector<uint8_t> squares; // columns * rows, row by row
    std::vector<BoardPiece> pieces;
    std::vector<BoardTrain> trains;

    /** A square's value. */
    uint8_t square(int column, int row) const { return squares[size_t(row) * columns + size_t(column)]; }
};

/**
 * Reads a board. As the game does, a file that ends before the pieces or
 * trains its header counts gives those it has; one that ends inside its
 * header or squares fails.
 */
Error parseBoard(std::span<const uint8_t> data, Board &out);

} // namespace reluva::legoloco
