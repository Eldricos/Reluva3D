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

// The cards, in the game's order (Notepad.txt's): suspects, weapons, rooms.
constexpr int kSuspectCount = 6, kWeaponCount = 6, kRoomCount = 9;
enum Suspect { kScarlet, kMustard, kWhite, kGreen, kPeacock, kPlum };
enum Weapon { kKnife, kCandlestick, kRevolver, kRope, kPipe, kWrench };
enum Room { kHall, kLounge, kDiningRoom, kKitchen, kBallroom, kConservatory, kBilliardRoom, kLibrary, kStudy };
constexpr int kNoRoom = -1;

// A square of the board, by its column and row in the world's 64-unit grid
// (whose lines are at 16 + 64 n).
struct Square {
    int x = 0, y = 0;
    bool operator<(const Square &o) const { return x != o.x ? x < o.x : y < o.y; }
    bool operator==(const Square &o) const { return x == o.x && y == o.y; }
    bool operator!=(const Square &o) const { return !(*this == o); }
};

// Where a suspect is: on a square of the corridors, or in a room.
struct Place {
    Square square;
    int room = kNoRoom;
    bool inRoom() const { return room != kNoRoom; }
    bool operator==(const Place &o) const { return room == o.room && (inRoom() || square == o.square); }
};

class Board {
public:
    struct Door {
        int room = kNoRoom;
        Square inside, outside; // the room's square and the corridor's in front of it
    };

    static constexpr int kSquareSize = 64, kGridOrigin = 16;
    static Square squareAt(int worldX, int worldY);
    // Where a suspect stands on a square: as clue.lvl places them, 24 in from its corner.
    static void standingPoint(const Square &s, int &worldX, int &worldY);

    Error build(const Level &level);

    bool isCorridor(const Square &s) const { return _corridor.count(s) != 0; }
    int roomAt(const Square &s) const; // kNoRoom outside rooms
    const std::set<Square> &corridor() const { return _corridor; }
    const std::vector<Square> &roomSquares(int room) const { return _roomSquares[size_t(room)]; }
    const std::vector<Door> &doors() const { return _doors; }
    const Square &start(int suspect) const { return _start[size_t(suspect)]; }
    int passage(int room) const { return _passage[size_t(room)]; } // the room it leads to, or kNoRoom
    // A room's squares in the order suspects take them: the original's, as
    // recorded, then along the room's front edges (see board.cpp).
    const std::vector<Square> &standing(int room) const { return _standing[size_t(room)]; }

    // The places a step from a place leads to: a corridor's neighbours, a
    // door's room from the square in front of it, a room's door squares.
    std::vector<Place> neighbours(const Place &p) const;

private:
    std::set<Square> _corridor;
    std::map<Square, int> _rooms;
    std::array<std::vector<Square>, kRoomCount> _roomSquares, _standing;
    std::vector<Door> _doors;
    std::array<Square, kSuspectCount> _start{};
    std::array<int, kRoomCount> _passage{};
};

} // namespace reluva::clue
