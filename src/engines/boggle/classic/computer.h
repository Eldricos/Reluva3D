// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
namespace reluva::boggle {
// Identity is explicit: a human can use any of these display names.
enum class ComputerLevel { Human = -1, Beginner, Intermediate, Advanced, MrBoggle };
bool validComputerLevel(ComputerLevel level);
std::string computerName(ComputerLevel level);
// The first three Classic opponents use the separate AI vocabulary.
bool usesCommonVocabulary(ComputerLevel level);
// Native selection with measured common base-word delay bounds.
// Plural pairing/presentation, other related words, jitter and Mr. Boggle pacing
// retain native policies; see docs/engines/boggle/plurals.md.
// Times are cumulative active milliseconds, excluding handoff/other turns.
class ComputerPlan {
public:
    Error build(const Board& board,const Dictionary& commonVocabulary,const Dictionary& english,
                ComputerLevel level,uint32_t seed,size_t slot,size_t visits=500000);
    const AcceptedWord* take(uint64_t elapsed);
    // Hidden play catches up in logical order, independent of frame/input rate.
    const AcceptedWord* takeScheduled(uint64_t elapsed,uint64_t& submittedAt);
    const AcceptedWord* nextWord() const { return exhausted()?nullptr:&words_[at_]; }
    uint64_t due() const { return due_; }
    bool exhausted() const { return at_==words_.size(); }
    const std::vector<AcceptedWord>& words() const { return words_; }
private:
    std::vector<AcceptedWord> words_;
    std::vector<uint64_t> delays_;
    size_t at_=0;
    uint64_t due_=0,lastSubmitted_=0;
};
} // namespace reluva::boggle
