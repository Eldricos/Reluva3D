// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: boards (.sav), built here.

#include "engines/legoloco/formats/board.h"

#include "tests/support/test.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, size_t at, uint16_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}

void put32(Bytes &b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

void putString(Bytes &b, size_t at, const char *s) {
    std::memcpy(&b[at], s, std::strlen(s));
}

// A 4 by 3 board with `pieces` pieces counted, `present` of them in the
// file, and `trains` trains.
Bytes makeBoard(uint32_t pieces, uint32_t present, uint16_t trains) {
    Bytes b(0x114 + 12 + present * 0x80 + (present == pieces ? trains * 0x2c : 0), 0);
    put16(b, 0, 8);
    put16(b, 2, 4);
    put16(b, 4, 3);
    put32(b, 8, pieces);
    put16(b, 12, trains);
    putString(b, 0xe, "WINTER");
    for (size_t i = 0; i < 12; ++i)
        b[0x114 + i] = uint8_t(i % 8);
    for (uint32_t i = 0; i < present; ++i) {
        const size_t r = 0x114 + 12 + i * 0x80;
        put16(b, r, uint16_t(3100 + i));
        put16(b, r + 2, uint16_t(i));
        put16(b, r + 4, 2);
        put32(b, r + 8, 1);
        putString(b, r + 0x10, "Building");
        put16(b, r + 0x1c + 0x14, 7230);
        put32(b, r + 0x1c + 0x14 + 4, 9);
        putString(b, r + 0x1c + 0x14 + 8, "Rachel");
    }
    if (present == pieces)
        for (uint16_t t = 0; t < trains; ++t)
            putString(b, 0x114 + 12 + pieces * 0x80 + t * 0x2c + 0x20, "Loco");
    return b;
}

void testBoard() {
    Board board;
    CHECK(!parseBoard(makeBoard(2, 2, 1), board));
    CHECK(board.version == 8 && board.columns == 4 && board.rows == 3 && board.backdrop == "WINTER");
    CHECK(board.squares.size() == 12 && board.square(1, 2) == 1);
    CHECK(board.pieces.size() == 2 && board.declaredPieces == 2);
    if (board.pieces.size() == 2) {
        const BoardPiece &p = board.pieces[1];
        CHECK(p.resource == 3101 && p.column == 1 && p.row == 2 && p.value8 == 1 && p.name == "Building");
        CHECK(p.minifigs[0].resource == 0 && p.minifigs[1].resource == 7230 && p.minifigs[1].value == 9 &&
              p.minifigs[1].name == "Rachel");
    }
    CHECK(board.trains.size() == 1 && board.trains[0].name == "Loco");
}

// The area layouts count more pieces than they hold; the game takes those there are.
void testFewerPieces() {
    Board board;
    CHECK(!parseBoard(makeBoard(5, 3, 0), board));
    CHECK(board.pieces.size() == 3 && board.declaredPieces == 5);
}

void testShort() {
    Board board;
    Bytes b = makeBoard(0, 0, 0);
    b.pop_back();
    CHECK(bool(parseBoard(b, board)));
    b.resize(0x113);
    CHECK(bool(parseBoard(b, board)));
}

} // namespace

static const reluva::test::Suite kSuite("legoloco.unit.boards",
                                        {
                                            {"board", [](const std::filesystem::path &) { testBoard(); }},
                                            {"fewer pieces", [](const std::filesystem::path &) { testFewerPieces(); }},
                                            {"short", [](const std::filesystem::path &) { testShort(); }},
                                        });
