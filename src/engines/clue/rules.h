// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The rules of Clue as the game's own texts give them (Data/<lang>/Text):
// the deal, turns, the die or the No Die Roll variant's nine moves, moving,
// suggestions, disproving and accusations. Only the rules: no pictures, no
// sounds, no choices of the computer players (see ai.h).

#include "engines/clue/board.h"

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
constexpr int suspectCard(int s) { return s; }
constexpr int weaponCard(int w) { return kSuspectCount + w; }
constexpr int roomCard(int r) { return kSuspectCount + kWeaponCount + r; }

// Who, with what, where: the case file, a suggestion or an accusation.
struct Accusation {
    int suspect = 0, weapon = 0, room = 0;
    bool has(int card) const { return card == suspectCard(suspect) || card == weaponCard(weapon) || card == roomCard(room); }
    bool operator==(const Accusation& o) const { return suspect == o.suspect && weapon == o.weapon && room == o.room; }
};

// Saved games are Reluva's own: whitespace-separated numbers, strings by
// their length (see docs/engines/clue.md).
namespace saveio {
void putString(std::ostream& out, const std::string& s);
bool getString(std::istream& in, std::string& s);
} // namespace saveio

class Game {
public:
    // Where the turn is.
    enum class Phase {
        Start,     // before moving: roll, take a passage, suggest (if brought in by a suggestion), accuse, end
        Moving,    // the die rolled (or, without it, the moves to spend): a move to choose
        Moved,     // moved: suggest if in a room just entered, accuse, end
        Disprove,  // a suggestion made: disprover() is to show a card
        Suggested, // the suggestion answered: accuse, end
        Over,
    };
    static constexpr int kMovesPerTurn = 9, kActionCost = 3; // No Die Roll's

    struct Seat {
        bool playing = false;
        bool out = false; // accused wrongly
        std::vector<int> hand;
    };

    // A new game: the players' suspects (at least three); the case file
    // drawn and the rest dealt from the rng.
    Error start(const Board& board, const std::array<bool, kSuspectCount>& playing, bool noDieRoll, uint32_t seed);

    const Board& board() const { return *board_; }
    const Seat& seat(int suspect) const { return seats_[size_t(suspect)]; }
    const Accusation& solution() const { return solution_; }
    bool noDieRoll() const { return noDieRoll_; }
    int turn() const { return turn_; }
    Phase phase() const { return phase_; }
    const Place& place(int suspect) const { return places_[size_t(suspect)]; }
    int roll() const { return roll_; }        // the die's number, when rolled
    int moves() const { return moves_; }      // No Die Roll: the moves left this turn
    int winner() const { return winner_; }    // -1: none (yet, or everyone accused wrongly)

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
    bool canWalk(const std::vector<Place>& path) const;
    // The next places a path being planned can go on to (empty path: from
    // where the player is).
    std::vector<Place> nextSteps(const std::vector<Place>& path) const;
    // Every place the player can end the move at, each by a shortest path
    // there (its last place).
    std::vector<std::vector<Place>> destinations() const;
    bool canMove() const; // walking is what the player may do now
    Error walk(const std::vector<Place>& path);
    Error takePassage();

    // A suggestion in the room the player is in: the suspect named is
    // brought there. Then the others, in turn from the player's left, are
    // asked: the first holding a card named must show one.
    Error suggest(int suspect, int weapon);
    const Accusation& suggestion() const { return suggestion_; }
    int disprover() const { return disprover_; } // -1 when nobody could
    std::vector<int> disproving() const;         // the disprover's cards that answer it
    Error show(int card);
    int shown() const { return shown_; }         // the card shown, -1 for none
    // Who was asked and couldn't answer the last suggestion, in order.
    const std::vector<int>& passed() const { return passed_; }

    // Right: the player wins. Wrong: the player is out (but still shows
    // cards), moved off a doorway if blocking one; the game is over when
    // no one is left.
    Error accuse(const Accusation& a);
    Error endTurn();

    // The whole state of the game, to be saved; loaded on the same board.
    void save(std::ostream& out) const;
    Error load(std::istream& in, const Board& board);

    // The square suspects stand on when in a room, kept apart.
    Square standingSquare(int suspect) const;

private:
    bool occupied(const Square& s, int except) const;
    bool stepAllowed(const std::vector<Place>& path, const Place& next) const;
    int stepLimit() const;
    void beginTurn();

    const Board* board_ = nullptr;
    std::array<Seat, kSuspectCount> seats_;
    std::array<Place, kSuspectCount> places_;
    // Brought into a room by someone's suggestion since their last turn:
    // may suggest there without moving.
    std::array<bool, kSuspectCount> broughtIn_{};
    // The room each last suggested in and hasn't left since: no second
    // suggestion there until they leave and come back.
    std::array<int, kSuspectCount> suggestedIn_{};
    Accusation solution_;
    bool noDieRoll_ = false;
    std::mt19937 rng_;
    int turn_ = 0;
    Phase phase_ = Phase::Over;
    int roll_ = 0, moves_ = 0;
    int startRoom_ = kNoRoom; // the room the player started the turn in
    bool enteredRoom_ = false;
    bool suggestedThisTurn_ = false;
    Accusation suggestion_;
    int disprover_ = -1, shown_ = -1;
    std::vector<int> passed_;
    int winner_ = -1;
};

} // namespace reluva::clue
