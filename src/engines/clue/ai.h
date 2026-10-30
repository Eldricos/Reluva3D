// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The computer players: a notebook of what each knows of who holds which
// card, deduced from what it sees, and their choices. The three levels of
// the set-up screen, matched to recordings of the original's (see
// docs/engines/clue.md):
//
// - Novice: names anyone and anything, itself often; notes only what it is
//   shown and who couldn't answer its own suggestions.
// - Normal: names what it doesn't know; notes the same.
// - Expert: also notes others' wrong accusations; names a kind's answer
//   once known and, among the rest, the card fewest might hold, never
//   itself in doubt; shows a card already seen when it can.
//
// The notebook can also count each player's cards and work out which of
// three an unseen card was; no level does, as the original's experts are
// no quicker than without (docs/engines/clue.md).

#include "engines/clue/rules.h"

#include <array>
#include <map>
#include <random>
#include <vector>

namespace reluva::clue {

// Who holds each card, as far as can be told: the seats 0-5 and the case
// file (kCaseFile).
class Notebook {
public:
    static constexpr int kCaseFile = kSuspectCount, kOwners = kSuspectCount + 1;
    enum Mark : uint8_t { Unknown, Yes, No };

    // A fresh notebook for the seat `me`: its hand known, the seats not
    // playing holding nothing, every playing seat `handSize` cards.
    void reset(const Game& game, int me, bool countCards, bool useClues);

    Mark mark(int card, int owner) const { return marks_[size_t(card)][size_t(owner)]; }
    int holder(int card) const;  // the owner known to hold it, or -1
    bool inCaseFile(int card) const { return mark(card, kCaseFile) == Yes; }
    bool outOfCaseFile(int card) const { return mark(card, kCaseFile) == No; }
    int solved(int kind) const;  // 0 suspects, 1 weapons, 2 rooms: the card known in the case file, or -1
    bool complete() const { return solved(0) >= 0 && solved(1) >= 0 && solved(2) >= 0; }
    int unknownCount() const;    // cards whose place isn't known

    // What can be seen happen.
    void holds(int owner, int card);              // shown it, or it's in one's hand
    void lacks(int owner, int card);              // couldn't show it
    void holdsOneOf(int owner, const std::array<int, 3>& cards); // showed one, not seen which
    void notAll(const std::array<int, 3>& cards); // a wrong accusation: not all three in the case file

    void save(std::ostream& out) const;
    Error load(std::istream& in);

private:
    void set(int card, int owner, Mark m);
    void settle(); // deduce until nothing more can be

    std::array<std::array<Mark, kOwners>, kCardCount> marks_{};
    std::array<int, kOwners> handSize_{};
    std::vector<std::pair<int, std::array<int, 3>>> oneOf_;
    std::vector<std::array<int, 3>> notAll_;
    bool countCards_ = true, useClues_ = true;
    bool contradiction_ = false;
};

class ComputerPlayer {
public:
    enum Level { Novice, Normal, Expert };

    void reset(const Game& game, int me, uint32_t seed, int level = Expert);

    // After a suggestion is answered (or not): what this player can see of
    // it. A wrong accusation: what was accused.
    void observe(const Game& game);
    void observeAccusation(const Accusation& a);

    enum class Action { Roll, Passage, Walk, Suggest, Accuse, EndTurn };
    Action decide(const Game& game);
    std::vector<Place> chooseWalk(const Game& game);
    Accusation chooseSuggestion(const Game& game);
    Accusation chooseAccusation() const;
    int chooseShow(const Game& game, int suggester);

    bool ready() const { return notes_.complete(); }

    // Saved and loaded with the game (the board's distances worked out again).
    void save(std::ostream& out) const;
    Error load(std::istream& in, const Game& game);
    const Notebook& notebook() const { return notes_; }
    int level() const { return level_; }

private:
    void measure(const Board& board);
    int distance(const Place& from, int room) const;
    int target(const Game& game) const;
    bool roomWanted(int room) const;
    int pick(int kind, const Game& game);

    int me_ = 0;
    int level_ = Expert;
    std::mt19937 rng_;
    Notebook notes_;
    // The steps from each corridor square to each room.
    std::array<std::map<Square, int>, kRoomCount> toRoom_;
    std::map<int, std::vector<int>> shownTo_; // the cards it has shown each suspect
};

} // namespace reluva::clue
