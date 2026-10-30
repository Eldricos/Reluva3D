// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A computer player: what it has learned, and its choices. For now a plain
// one: it heads for the nearest room it knows nothing of, suggests what it
// doesn't know, and accuses once a single card of each kind is left. How
// the original's three levels play is for later (docs/engines/clue.md).

#include "engines/clue/rules.h"

#include <array>
#include <map>
#include <random>
#include <vector>

namespace reluva::clue {

class ComputerPlayer {
public:
    void reset(const Game& game, int me, uint32_t seed);

    // What it learns from each suggestion answered (shown: the card it was
    // shown, when it made the suggestion; -1 otherwise).
    void observe(const Game& game, int suggester, const Accusation& s, int disprover, int shown);

    enum class Action { Roll, Passage, Walk, Suggest, Accuse, EndTurn };
    Action decide(const Game& game);
    std::vector<Place> chooseWalk(const Game& game);
    Accusation chooseSuggestion(const Game& game);
    Accusation chooseAccusation() const;
    int chooseShow(const Game& game, int suggester);

    bool ready() const; // one card of each kind left: it knows the answer
    bool knownOut(int card) const { return out_[size_t(card)]; }

private:
    int distance(const Place& from, int room) const;
    int target(const Game& game) const;
    int pick(int first, int count);

    int me_ = 0;
    std::mt19937 rng_;
    std::array<bool, kCardCount> out_{};   // known not to be in the case file
    std::array<bool, kCardCount> found_{}; // known to be in it
    // The steps from each corridor square to each room.
    std::array<std::map<Square, int>, kRoomCount> toRoom_;
    std::map<int, std::vector<int>> shownTo_; // the cards it has shown each suspect
};

} // namespace reluva::clue
