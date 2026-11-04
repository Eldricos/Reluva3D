// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/layout.h"
namespace reluva::boggle {
Error RoundResults::build(const ClassicRound& round) {
    clear();
    if(round.phase()!=RoundPhase::Results || round.candidate())return Error::fail("Finish dictionary review before publishing results");
    for(const auto& word:round.words())if(word.state!=WordState::Declined) {
        words_.push_back(word);score_+=word.entry.points;
    }
    if(score_!=round.score()){clear();return Error::fail("Inconsistent final round score");}
    ready_=true;return Error::success();
}
std::optional<size_t> RoundResults::rowAt(int x,int y,bool artwork,size_t scroll) const {
    if(!ready_ || (artwork && (historyOlder.contains(x,y) || historyNewer.contains(x,y))))return {};
    const int left=artwork?633:442,top=artwork?58:217,right=artwork?792:776;
    const size_t visible=artwork?ClassicArtwork::visibleWords:14;
    if(x<left || x>=right || y<top || y>=top+int(visible)*17)return {};
    const auto view=wordHistoryView(words_.size(),visible,scroll);
    const size_t index=view.begin+size_t((y-top)/17);
    return index<view.end?std::optional<size_t>(index):std::nullopt;
}
size_t RoundResults::scrollFor(size_t index,bool artwork,size_t scroll) const {
    const size_t visible=artwork?ClassicArtwork::visibleWords:14;
    const auto view=wordHistoryView(words_.size(),visible,scroll);
    if(!ready_ || index>=words_.size())return std::min(scroll,view.maximum);
    size_t first=view.begin;
    if(index<view.begin)first=index;
    else if(index>=view.end)first=index-visible+1;
    return view.maximum-first;
}
} // namespace reluva::boggle
