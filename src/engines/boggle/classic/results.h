// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/history.h"
#include <optional>
namespace reluva::boggle {
// An immutable view of the adjudicated round. Declined provisional entries
// contribute neither a row nor points; pending entries prevent publication.
class RoundResults {
public:
    Error build(const ClassicRound& round);
    void clear(){words_.clear();score_=0;ready_=false;}
    bool ready() const {return ready_;}
    const std::vector<RoundWord>& words() const {return words_;}
    int score() const {return score_;}
    std::optional<size_t> rowAt(int x,int y,bool artwork,size_t scroll) const;
    size_t scrollFor(size_t index,bool artwork,size_t scroll) const;
private:
    bool ready_=false;
    int score_=0;
    std::vector<RoundWord> words_;
};
} // namespace reluva::boggle
