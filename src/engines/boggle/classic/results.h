// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/history.h"
#include <optional>
#include <set>
namespace reluva::boggle {
// An immutable view of the adjudicated round. Declined provisional entries
// contribute neither a row nor points; pending entries prevent publication.
struct CompetitionPlayer;
class RoundResults {
public:
    Error build(const ClassicRound& round);
    Error build(const ClassicRound& round,const CompetitionPlayer& player);
    bool shared(const std::string& word) const {return shared_.contains(word);}
    void clear(){shared_.clear();counted_=0;words_.clear();available_.clear();missed_.clear();board_={};score_=0;ready_=false;all_=false;availableReady_=false;}
    bool ready() const {return ready_;}
    const std::vector<RoundWord>& words() const {return all_?available_:words_;}
    size_t countedCount() const {return counted_;}
    bool allWords() const {return all_;}
    bool missed(const std::string& word) const {return all_ && missed_.contains(word);}
    Error showAll(bool enabled,const Dictionary& dictionary,const UserDictionary* user=nullptr);
    int score() const {return score_;}
    std::optional<size_t> rowAt(int x,int y,bool artwork,size_t scroll) const;
    size_t scrollFor(size_t index,bool artwork,size_t scroll) const;
private:
    bool ready_=false;
    int score_=0;
    size_t counted_=0;
    std::set<std::string> shared_;
    Board board_;
    bool all_=false,availableReady_=false;
    std::vector<RoundWord> words_,available_;
    std::set<std::string> missed_;
};
inline constexpr HistoryButton allWordsArtwork{24,448,180,29},allWordsPrototype{430,76,180,24};
} // namespace reluva::boggle
