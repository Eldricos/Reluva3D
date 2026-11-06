// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/solver.h"
#include <algorithm>
#include <limits>
namespace reluva::boggle {
bool validComputerLevel(ComputerLevel level) { return int(level)>=-1 && int(level)<=3; }
std::string computerName(ComputerLevel level) {
    switch(level) {
    case ComputerLevel::Beginner:return "CPU Beginner";
    case ComputerLevel::Intermediate:return "CPU Intermediate";
    case ComputerLevel::Advanced:return "CPU Advanced";
    case ComputerLevel::MrBoggle:return "CPU Mr. Boggle";
    default:return "";
    }
}
Error ComputerPlan::build(const Board& board,const Dictionary& vocabulary,const Dictionary& english,
                          ComputerLevel level,uint32_t seed,size_t slot,size_t visits) {
    words_.clear();at_=0;interval_=due_=0;
    if(!validComputerLevel(level) || level==ComputerLevel::Human || slot>=4)
        return Error::fail("Invalid computer participant");
    std::vector<AcceptedWord> found;
    if(auto error=findBoardWords(board,vocabulary,nullptr,found,visits))return error;
    const unsigned strength=unsigned(level);
    const size_t minimum=board.side()==5?4:3;
    const size_t extra[]={1,2,4,64};
    auto rank=[&](const std::string& word) {
        uint32_t hash=2166136261u^seed^(uint32_t(slot+1)*0x9e3779b9u);
        for(unsigned char c:word)hash=(hash^c)*16777619u;
        // Spread the small differences between similar words before sampling.
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;return hash;
    };
    for(auto& word:found)
        if(english.contains(word.word) && word.word.size()<=minimum+extra[strength] && rank(word.word)%4<=strength)
            words_.push_back(std::move(word));
    std::sort(words_.begin(),words_.end(),[&](const auto& a,const auto& b) {
        const auto x=rank(a.word),y=rank(b.word);return x!=y?x<y:a.word<b.word;
    });
    const uint64_t intervals[]={6000,4000,2000,1000};
    interval_=intervals[strength];due_=interval_;
    return Error::success();
}
const AcceptedWord* ComputerPlan::take(uint64_t elapsed) {
    if(exhausted() || elapsed<due_)return nullptr;
    // A stalled window may produce one word, never a catch-up burst.
    due_=elapsed>std::numeric_limits<uint64_t>::max()-interval_?
        std::numeric_limits<uint64_t>::max():elapsed+interval_;
    return &words_[at_++];
}
} // namespace reluva::boggle
