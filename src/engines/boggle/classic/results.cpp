// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/solver.h"
#include <map>
namespace reluva::boggle {
Error RoundResults::build(const ClassicRound& round) {
    clear();
    if(round.phase()!=RoundPhase::Results || round.candidate())return Error::fail("Finish dictionary review before publishing results");
    for(const auto& word:round.words())if(word.state!=WordState::Declined) {
        words_.push_back(word);score_+=word.entry.points;
    }
    if(score_!=round.score()){clear();return Error::fail("Inconsistent final round score");}
    board_=round.board();ready_=true;return Error::success();
}
Error RoundResults::showAll(bool enabled,const Dictionary& dictionary,const UserDictionary* user) {
    if(!ready_)return Error::fail("Finish the round and dictionary review before showing all words");
    if(enabled && !availableReady_) {
        std::vector<AcceptedWord> discovered;
        if(auto error=findBoardWords(board_,dictionary,user,discovered))return error;
        std::map<std::string,RoundWord> merged;
        for(const auto& word:words_)merged.emplace(word.entry.word,word);
        std::set<std::string> missed;
        for(auto& word:discovered)if(!merged.contains(word.word)) {
            missed.insert(word.word);const auto key=word.word;
            merged.emplace(key,RoundWord{std::move(word),WordState::Accepted});
        }
        std::vector<RoundWord> rows;rows.reserve(merged.size());
        for(auto& [name,word]:merged)rows.push_back(std::move(word));
        available_=std::move(rows);missed_=std::move(missed);availableReady_=true;
    }
    all_=enabled;return Error::success();
}
std::optional<size_t> RoundResults::rowAt(int x,int y,bool artwork,size_t scroll) const {
    if(!ready_ || (artwork && (historyOlder.contains(x,y) || historyNewer.contains(x,y))))return {};
    const int left=artwork?633:442,top=artwork?58:217,right=artwork?792:776;
    const size_t visible=artwork?ClassicArtwork::visibleWords:14;
    if(x<left || x>=right || y<top || y>=top+int(visible)*17)return {};
    const auto view=wordHistoryView(words().size(),visible,scroll);
    const size_t index=view.begin+size_t((y-top)/17);
    return index<view.end?std::optional<size_t>(index):std::nullopt;
}
size_t RoundResults::scrollFor(size_t index,bool artwork,size_t scroll) const {
    const size_t visible=artwork?ClassicArtwork::visibleWords:14;
    const auto view=wordHistoryView(words().size(),visible,scroll);
    if(!ready_ || index>=words().size())return std::min(scroll,view.maximum);
    size_t first=view.begin;
    if(index<view.begin)first=index;
    else if(index>=view.end)first=index-visible+1;
    return view.maximum-first;
}
} // namespace reluva::boggle
