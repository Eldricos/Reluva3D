// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: The board a level lays out: squares, doors,
// passages and moves (docs/engines/clue/formats/board.md).

#include "engines/clue/game/ai.h"
#include "engines/clue/game/board.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/formats/resources.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"
#include "tests/engines/clue/support.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;
using namespace reluva::clue::testing;

static void testBoard() {
    Board board;
    CHECK(!board.build(boardLevel()));
    CHECK(board.corridor().size() == 54);
    CHECK(board.doors().size() == size_t(kRoomCount));
    for (int r = 0; r < kRoomCount; ++r) {
        CHECK(board.roomSquares(r).size() == 4);
        CHECK((board.roomAt({3 * r + 1, 1}) == r));
    }
    CHECK((board.roomAt({2, 0}) == kNoRoom && board.isCorridor({2, 2}) && !board.isCorridor({2, 1})));
    CHECK((board.doors()[0].room == kHall && board.doors()[0].inside == Square{0, 1} &&
           board.doors()[0].outside == Square{0, 2}));
    CHECK((board.start(kPlum) == Square{25, 3}));
    CHECK(board.passage(kKitchen) == kStudy && board.passage(kStudy) == kKitchen && board.passage(kHall) == kNoRoom);
    // The chair's square comes last of a room's places to stand.
    CHECK((board.standing(kLounge).back() == Square{4, 0}));
    int wx;
    int wy;
    Board::standingPoint({29, 11}, wx, wy);
    CHECK(wx == 1896 && wy == 744); // where clue.lvl stands Miss Scarlet
    CHECK((Board::squareAt(1896, 744) == Square{29, 11} && Board::squareAt(15, 15) == Square{-1, -1}));
    // Steps: along the corridor, in at a door, out of a room by its doors.
    const auto n = board.neighbours({{3, 2}, kNoRoom});
    CHECK((n.size() == 4 && std::count(n.begin(), n.end(), Place{{}, kLounge}) == 1));
    CHECK((board.neighbours({{}, kLounge}) == std::vector<Place>({{{3, 2}, kNoRoom}})));
    // Without one of the rooms the board can't be made.
    Level partial = boardLevel();
    partial.placements.erase(
        std::remove_if(partial.placements.begin(), partial.placements.end(),
                       [&](const Placement &p) { return partial.objects[p.object] == "study:Ochair"; }),
        partial.placements.end());
    CHECK(board.build(partial));
}

static const reluva::test::Suite kSuite("clue.unit.board",
                                        {
                                            {"board", [](const std::filesystem::path &) { testBoard(); }},
                                        });
