// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
namespace reluva::boggle {
struct CompetitionWord {
    RoundWord word;
    uint8_t owners=0; // one bit per input player, including this player
    int points=0; // zero when another player kept the same word
    bool shared() const {return owners && (owners & (owners-1));}
};
struct CompetitionPlayer {
    std::vector<CompetitionWord> words;
    int score=0;
};
// Finished Classic rounds on the same board, in player-slot order. This model
// neither mutates provisional rounds nor runs opponents or player turns.
class ClassicCompetition {
public:
    static constexpr size_t maximumPlayers=4;
    Error build(const std::vector<const ClassicRound*>& rounds);
    void clear(){players_.clear();}
    bool ready() const {return !players_.empty();}
    const std::vector<CompetitionPlayer>& players() const {return players_;}
private:
    std::vector<CompetitionPlayer> players_;
};
}
