// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The rules of Clue as the game's own texts give them (Data/<lang>/Text):
// the deal, turns, the die or the No Die Roll variant's nine moves, moving,
// suggestions, disproving and accusations. Only the rules: no pictures, no
// sounds, no choices of the computer players (see ai.h).

#include "engines/clue/game/board.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <random>
#include <vector>

namespace reluva::clue {

// The 21 cards as numbers: the suspects 0-5, the weapons 6-11, the rooms
// 12-20, each in the game's order.
constexpr int kCardCount = kSuspectCount + kWeaponCount + kRoomCount;
constexpr int suspectCard(int s) {
    return s;
}
constexpr int weaponCard(int w) {
    return kSuspectCount + w;
}
constexpr int roomCard(int r) {
    return kSuspectCount + kWeaponCount + r;
}

// Who, with what, where: the case file, a suggestion or an accusation.
struct Accusation {
    int suspect = 0, weapon = 0, room = 0;
    bool has(int card) const {
        return card == suspectCard(suspect) || card == weaponCard(weapon) || card == roomCard(room);
    }
    bool operator==(const Accusation &o) const { return suspect == o.suspect && weapon == o.weapon && room == o.room; }
};

// Saved games are Reluva's own: whitespace-separated numbers, strings by
// their length (see docs/engines/clue/systems/saves.md).
namespace saveio {
void putString(std::ostream &out, const std::string &s);
bool getString(std::istream &in, std::string &s);
} // namespace saveio

// The Mersenne Twister the rules and the computer players draw from, kept
// with its seed and how many numbers it has given: that is how it's saved.
// std::mt19937's own text differs between standard libraries (libstdc++
// writes its state as stored, with an index; Visual Studio's in order,
// without), so a game saved on one system wouldn't load on another.
class Rng {
public:
    using result_type = uint32_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return 0xffffffffu; }
    void seed(uint32_t s) {
        _seed = s;
        _draws = 0;
        _gen.seed(s);
    }
    result_type operator()() {
        ++_draws;
        return result_type(_gen());
    }
    void save(std::ostream &out) const;
    bool load(std::istream &in); // false if damaged

private:
    std::mt19937 _gen; // seeded with 5489 until seed(), as std::mt19937 is
    uint32_t _seed = 5489;
    uint64_t _draws = 0;
};

class Game {
public:
    // Where the turn is.
    enum class Phase {
        kStart,     // before moving: roll, take a passage, suggest (if brought in by a suggestion), accuse, end
        kMoving,    // the die rolled (or, without it, the moves to spend): a move to choose
        kMoved,     // moved: suggest if in a room just entered, accuse, end
        kDisprove,  // a suggestion made: disprover() is to show a card
        kSuggested, // the suggestion answered: accuse, end
        kOver,
    };
    static constexpr int kMovesPerTurn = 9, kActionCost = 3; // No Die Roll's

    struct Seat {
        bool playing = false;
        bool out = false; // accused wrongly
        std::vector<int> hand;
    };

    // A new game: the players' suspects (at least three); the case file
    // drawn and the rest dealt from the rng.
    Error start(const Board &board, const std::array<bool, kSuspectCount> &playing, bool noDieRoll, uint32_t seed);

    const Board &board() const { return *_board; }
    const Seat &seat(int suspect) const { return _seats[size_t(suspect)]; }
    const Accusation &solution() const { return _solution; }
    bool noDieRoll() const { return _noDieRoll; }
    int turn() const { return _turn; }
    Phase phase() const { return _phase; }
    const Place &place(int suspect) const { return _places[size_t(suspect)]; }
    int roll() const { return _roll; }     // the die's number, when rolled
    int moves() const { return _moves; }   // No Die Roll: the moves left this turn
    int winner() const { return _winner; } // -1: none (yet, or everyone accused wrongly)

    // What the player whose turn it is may do now.
    bool canRoll() const;
    bool canTakePassage() const;
    bool canSuggest() const;
    bool canAccuse() const;
    bool canEndTurn() const;
    bool trapped() const; // in a room whose doors are all blocked, with no passage

    // Rolls the die (1-6).
    int rollDie();
    // Whether a path from where the player is may be walked now: each step
    // to a neighbour, no square twice, none taken by another suspect, not
    // back into the room left; the die's number of steps exactly, or fewer
    // ending in a room (or, without the die, as many as there are moves).
    bool canWalk(const std::vector<Place> &path) const;
    // The next places a path being planned can go on to (empty path: from
    // where the player is).
    std::vector<Place> nextSteps(const std::vector<Place> &path) const;
    // Every place the player can end the move at, each by a shortest path
    // there (its last place).
    std::vector<std::vector<Place>> destinations() const;
    bool canMove() const; // walking is what the player may do now
    Error walk(const std::vector<Place> &path);
    Error takePassage();

    // A suggestion in the room the player is in: the suspect named is
    // brought there. Then the others, in turn from the player's left, are
    // asked: the first holding a card named must show one.
    Error suggest(int suspect, int weapon);
    const Accusation &suggestion() const { return _suggestion; }
    int disprover() const { return _disprover; } // -1 when nobody could
    std::vector<int> disproving() const;         // the disprover's cards that answer it
    Error show(int card);
    int shown() const { return _shown; } // the card shown, -1 for none
    // Who was asked and couldn't answer the last suggestion, in order.
    const std::vector<int> &passed() const { return _passed; }

    // Right: the player wins. Wrong: the player is out (but still shows
    // cards), moved off a doorway if blocking one; the game is over when
    // no one is left.
    Error accuse(const Accusation &a);
    Error endTurn();

    // The whole state of the game, to be saved; loaded on the same board.
    void save(std::ostream &out) const;
    Error load(std::istream &in, const Board &board);

    // The square suspects stand on when in a room, kept apart.
    Square standingSquare(int suspect) const;

private:
    bool occupied(const Square &s, int except) const;
    bool stepAllowed(const std::vector<Place> &path, const Place &next) const;
    int stepLimit() const;
    void beginTurn();

    const Board *_board = nullptr;
    std::array<Seat, kSuspectCount> _seats;
    std::array<Place, kSuspectCount> _places;
    // Brought into a room by someone's suggestion since their last turn:
    // may suggest there without moving.
    std::array<bool, kSuspectCount> _broughtIn{};
    // The room each last suggested in and hasn't left since: no second
    // suggestion there until they leave and come back.
    std::array<int, kSuspectCount> _suggestedIn{};
    Accusation _solution;
    bool _noDieRoll = false;
    Rng _rng;
    int _turn = 0;
    Phase _phase = Phase::kOver;
    int _roll = 0, _moves = 0;
    int _startRoom = kNoRoom; // the room the player started the turn in
    bool _enteredRoom = false;
    bool _suggestedThisTurn = false;
    Accusation _suggestion;
    int _disprover = -1, _shown = -1;
    std::vector<int> _passed;
    int _winner = -1;
};

} // namespace reluva::clue
