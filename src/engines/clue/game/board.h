// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Clue board as clue.lvl lays it out: its squares are the mansion's
// 64 x 64 floor blocks. See docs/engines/clue/formats/board.md.

#include "common/types.h"
#include "engines/clue/formats/level.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reluva::clue {

/** How many cards of each kind there are. */
///@{
constexpr int kSuspectCount = 6;
constexpr int kWeaponCount = 6;
constexpr int kRoomCount = 9;
///@}
/** The cards, in the game's order (Notepad.txt's): suspects, weapons, rooms. */
///@{
enum Suspect { kScarlet, kMustard, kWhite, kGreen, kPeacock, kPlum };
enum Weapon { kKnife, kCandlestick, kRevolver, kRope, kPipe, kWrench };
enum Room { kHall, kLounge, kDiningRoom, kKitchen, kBallroom, kConservatory, kBilliardRoom, kLibrary, kStudy };
///@}
/** Not in a room. */
constexpr int kNoRoom = -1;

/**
 * A square of the board, by its column and row in the world's 64-unit grid
 * (whose lines are at 16 + 64 n).
 */
struct Square {
    int x = 0;
    int y = 0;
    /** Ordered by column, then row, for sets and maps. */
    bool operator<(const Square &o) const { return x != o.x ? x < o.x : y < o.y; }
    /** The same square, or not. */
    ///@{
    bool operator==(const Square &o) const { return x == o.x && y == o.y; }
    bool operator!=(const Square &o) const { return !(*this == o); }
    ///@}
};

/** Where a suspect is: on a square of the corridors, or in a room. */
struct Place {
    Square square;
    int room = kNoRoom;
    /** Whether it's in a room, not on a corridor's square. */
    bool inRoom() const { return room != kNoRoom; }
    /** The same room, or the same corridor square: in a room, the square doesn't count. */
    bool operator==(const Place &o) const { return room == o.room && (inRoom() || square == o.square); }
};

/** The board: its corridors, rooms, doors, starting squares and secret passages. */
class Board {
public:
    /** A door: a room's square and the corridor's in front of it. */
    struct Door {
        int room = kNoRoom;
        // the room's square and the corridor's in front of it
        Square inside;
        Square outside;
    };

    /** A square's size, and where the grid's lines start, in world units. */
    ///@{
    static constexpr int kSquareSize = 64;
    static constexpr int kGridOrigin = 16;
    ///@}
    /** The square a point of the world is on. */
    static Square squareAt(int worldX, int worldY);
    /** Where a suspect stands on a square: as clue.lvl places them, 24 in from its corner. */
    static void standingPoint(const Square &s, int &worldX, int &worldY);

    /** Works the board out from the mansion's placements; fails if a room, door or start isn't found. */
    Error build(const Level &level);

    /** Whether a square is a corridor's. */
    bool isCorridor(const Square &s) const { return _corridor.count(s) != 0; }
    /** The room a square is in; kNoRoom outside rooms. */
    int roomAt(const Square &s) const;
    /** Every corridor square. */
    const std::set<Square> &corridor() const { return _corridor; }
    /** A room's squares; `room` must be one. */
    const std::vector<Square> &roomSquares(int room) const { return _roomSquares[size_t(room)]; }
    /** Every door. */
    const std::vector<Door> &doors() const { return _doors; }
    /** Where a suspect starts. */
    const Square &start(int suspect) const { return _start[size_t(suspect)]; }
    /** The room a room's secret passage leads to, or kNoRoom. */
    int passage(int room) const { return _passage[size_t(room)]; }
    /**
     * A room's squares in the order suspects take them: the original's, as
     * recorded, then along the room's front edges (see board.cpp).
     */
    const std::vector<Square> &standing(int room) const { return _standing[size_t(room)]; }

    /**
     * The places a step from a place leads to: a corridor's neighbours, a
     * door's room from the square in front of it, a room's door squares.
     */
    std::vector<Place> neighbours(const Place &p) const;

private:
    std::set<Square> _corridor;
    std::map<Square, int> _rooms;
    std::array<std::vector<Square>, kRoomCount> _roomSquares;
    std::array<std::vector<Square>, kRoomCount> _standing;
    std::vector<Door> _doors;
    std::array<Square, kSuspectCount> _start{};
    std::array<int, kRoomCount> _passage{};
};

} // namespace reluva::clue
