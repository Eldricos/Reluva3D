// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The computer players, as Clue.exe's (read from its code; see
// docs/engines/clue/systems/ai.md): what each knows of who holds which card, kept as
// it keeps it, and its choices. The set-up screen's three levels differ
// only in what each takes in of others' suggestions and accusations, and
// in two of the choices (below).
//
// Notebook is a fuller deduction of who holds what, kept for its tests
// and for later use; the computer players don't use it.

#include "engines/clue/game/rules.h"

#include <array>
#include <map>
#include <random>
#include <vector>

namespace reluva::clue {

// Who holds each card, as far as can be told: the seats 0-5 and the case
// file (kCaseFile).
class Notebook {
public:
    static constexpr int kCaseFile = kSuspectCount;
    static constexpr int kOwners = kSuspectCount + 1;
    enum Mark : uint8_t { kUnknown, kYes, kNo };

    // A fresh notebook for the seat `me`: its hand known, the seats not
    // playing holding nothing, every playing seat `handSize` cards.
    void reset(const Game &game, int me, bool countCards, bool useClues);

    Mark mark(int card, int owner) const { return _marks[size_t(card)][size_t(owner)]; }
    int holder(int card) const; // the owner known to hold it, or -1
    bool inCaseFile(int card) const { return mark(card, kCaseFile) == kYes; }
    bool outOfCaseFile(int card) const { return mark(card, kCaseFile) == kNo; }
    int solved(int kind) const; // 0 suspects, 1 weapons, 2 rooms: the card known in the case file, or -1
    bool complete() const { return solved(0) >= 0 && solved(1) >= 0 && solved(2) >= 0; }
    int unknownCount() const; // cards whose place isn't known

    // What can be seen happen.
    void holds(int owner, int card);                             // shown it, or it's in one's hand
    void lacks(int owner, int card);                             // couldn't show it
    void holdsOneOf(int owner, const std::array<int, 3> &cards); // showed one, not seen which
    void notAll(const std::array<int, 3> &cards);                // a wrong accusation: not all three in the case file

    void save(std::ostream &out) const;
    Error load(std::istream &in);

private:
    void set(int card, int owner, Mark m);
    void settle(); // deduce until nothing more can be

    std::array<std::array<Mark, kOwners>, kCardCount> _marks{};
    std::array<int, kOwners> _handSize{};
    std::vector<std::pair<int, std::array<int, 3>>> _oneOf;
    std::vector<std::array<int, 3>> _notAll;
    bool _countCards = true;
    bool _useClues = true;
    bool _contradiction = false;
};

class ComputerPlayer {
public:
    enum Level { kNovice, kNormal, kExpert };

    void reset(const Game &game, int me, uint32_t seed, int level = kExpert);

    // After a suggestion is answered (or not): what this player can see of
    // it. A wrong accusation: what was accused, and by whom.
    void observe(const Game &game);
    void observeAccusation(const Accusation &a, int accuser);

    enum class Action { kRoll, kPassage, kWalk, kSuggest, kAccuse, kEndTurn };
    Action decide(const Game &game);
    std::vector<Place> chooseWalk(const Game &game);
    Accusation chooseSuggestion(const Game &game);
    Accusation chooseAccusation() const;
    int chooseShow(const Game &game, int suggester);

    bool ready() const;

    // Saved and loaded with the game (the board's distances worked out again).
    void save(std::ostream &out) const;
    Error load(std::istream &in, const Game &game);
    int level() const { return _level; }

private:
    // What it knows, as Clue.exe's notebook keeps it (0x42CC20 on): cards
    // as bits 0-20 (suspects, weapons, rooms).
    struct Mind {
        uint32_t current = 0;  // the suggestion last made
        uint32_t known = 0;    // every card known to be held
        uint32_t solution = 0; // the answer's cards found
        std::array<uint32_t, kSuspectCount> has{};
        std::array<uint32_t, kSuspectCount> hasNot{};
        std::array<std::vector<uint32_t>, kSuspectCount> oneOf; // holds one of each
        std::array<uint32_t, kSuspectCount> shownTo{};          // what it has shown each
        uint32_t shownAny = 0;
        int moves = 0; // moves made, of the room it heads for
    };
    void settle();
    void findSolution();
    bool takesIn(); // a level's chance of taking in others' play
    uint32_t pickBit(uint32_t mask);
    uint32_t fallback(uint32_t kind, const Game &game);
    uint32_t pickCard(uint32_t kind, const Game &game);
    int pickRoom(const Game &game);
    int closestRoom(const Game &game) const;
    void measure(const Board &board);
    int distance(const Place &from, int room) const;

    int _me = 0;
    int _level = kExpert;
    Rng _rng;
    Mind _mind;
    int _target = kNoRoom; // the room it heads for this turn
    // The steps from each corridor square to each room.
    std::array<std::map<Square, int>, kRoomCount> _toRoom;
};

} // namespace reluva::clue
