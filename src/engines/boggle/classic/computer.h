// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/ai_motion.h"
namespace reluva::boggle {
// Identity is explicit: a human can use any of these display names.
enum class ComputerLevel { Human = -1, Beginner, Intermediate, Advanced, MrBoggle };
bool validComputerLevel(ComputerLevel level);
std::string computerName(ComputerLevel level);
// The first three Classic opponents use the separate AI vocabulary.
bool usesCommonVocabulary(ComputerLevel level);
// Classic selection/grouping and level parameters established by original
// behavioral comparisons. Random seeds and display geometry are native.
// Times are cumulative active milliseconds, excluding handoff/other turns.
class ComputerPlan {
public:
    Error build(const Board& board,const Dictionary& commonVocabulary,const Dictionary& english,
                ComputerLevel level,uint32_t seed,size_t slot,size_t visits=500000,const ComputerGeometry* geometry=nullptr);
    const AcceptedWord* take(uint64_t elapsed);
    // Hidden play catches up in logical order, independent of frame/input rate.
    const AcceptedWord* takeScheduled(uint64_t elapsed,uint64_t& submittedAt);
    const AcceptedWord* nextWord() const { return exhausted()?nullptr:&words_[at_]; }
    const ComputerMotion* nextMotion() const { return exhausted()?nullptr:&motions_[at_]; }
    uint64_t due() const { return due_; }
    bool exhausted() const { return at_==words_.size(); }
    bool finished(uint64_t elapsed) const { return exhausted() && elapsed>=finishAt_; }
    const std::vector<AcceptedWord>& words() const { return words_; }
private:
    std::vector<AcceptedWord> words_;
    std::vector<uint64_t> delays_;
    std::vector<ComputerMotion> motions_;
    size_t at_=0;
    uint64_t due_=0,lastSubmitted_=0,finishAt_=0,finishDelay_=0;
};
} // namespace reluva::boggle
