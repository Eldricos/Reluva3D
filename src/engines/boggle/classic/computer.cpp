// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/solver.h"
#include <algorithm>
#include <limits>
#include <map>
namespace reluva::boggle {
namespace {
// Measured base-word delay bounds; see docs/engines/boggle/thinking.md.
// Selection, jitter and applying these bounds to every native word are ours.
struct ThinkingBounds {
    uint64_t minimum,span,minimumStep,spanStep,scarcity,penalty;
};
constexpr ThinkingBounds thinking[]={{1500,2000,150,700,9,3000},
    {1200,1700,50,500,7,2000},{1000,1000,50,300,5,1000}};
uint64_t addTime(uint64_t start,uint64_t delay) {
    return start>std::numeric_limits<uint64_t>::max()-delay?
        std::numeric_limits<uint64_t>::max():start+delay;
}
// Our disjoint pairing policy; the verified append forms are documented in
// docs/engines/boggle/plurals.md. Never synthesize an entry or its cube path.
std::vector<uint8_t> groupPlurals(std::vector<AcceptedWord>& words) {
    const size_t count=words.size();std::map<std::string,size_t> index;
    std::vector<size_t> order,partner(count,count);std::vector<uint8_t> paired(count),plural(count);
    for(size_t i=0;i<count;++i){index.emplace(words[i].word,i);order.push_back(i);}
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b) {
        return words[a].word.size()!=words[b].word.size()?words[a].word.size()<words[b].word.size():a<b;
    });
    for(size_t base:order) {
        if(paired[base])continue;
        for(const char* suffix:{"S","ES"}) {
            const auto found=index.find(words[base].word+suffix);
            if(found==index.end() || paired[found->second])continue;
            const size_t child=found->second;partner[base]=child;paired[base]=paired[child]=1;plural[child]=1;break;
        }
    }
    std::vector<AcceptedWord> grouped;std::vector<uint8_t> followups;
    grouped.reserve(count);followups.reserve(count);
    for(size_t i=0;i<count;++i)if(!plural[i]) {
        grouped.push_back(std::move(words[i]));followups.push_back(0);
        if(partner[i]!=count){grouped.push_back(std::move(words[partner[i]]));followups.push_back(1);}
    }
    words=std::move(grouped);return followups;
}
}
bool validComputerLevel(ComputerLevel level) { return int(level)>=-1 && int(level)<=3; }
bool usesCommonVocabulary(ComputerLevel level) { return int(level)>=0 && int(level)<=2; }
std::string computerName(ComputerLevel level) {
    switch(level) {
    case ComputerLevel::Beginner:return "CPU Beginner";
    case ComputerLevel::Intermediate:return "CPU Intermediate";
    case ComputerLevel::Advanced:return "CPU Advanced";
    case ComputerLevel::MrBoggle:return "CPU Mr. Boggle";
    default:return "";
    }
}
Error ComputerPlan::build(const Board& board,const Dictionary& commonVocabulary,const Dictionary& english,
                          ComputerLevel level,uint32_t seed,size_t slot,size_t visits) {
    words_.clear();delays_.clear();at_=0;due_=lastSubmitted_=0;
    if(!validComputerLevel(level) || level==ComputerLevel::Human || slot>=4)
        return Error::fail("Invalid computer participant");
    std::vector<AcceptedWord> found;
    const bool common=usesCommonVocabulary(level);
    if(auto error=findBoardWords(board,common?commonVocabulary:english,nullptr,found,visits,common?9:64))return error;
    const unsigned strength=unsigned(level);
    auto rank=[&](const std::string& word) {
        uint32_t hash=2166136261u^seed^(uint32_t(slot+1)*0x9e3779b9u);
        for(unsigned char c:word)hash=(hash^c)*16777619u;
        // Spread the small differences between similar words before sampling.
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;return hash;
    };
    for(auto& word:found)
        if(english.contains(word.word) && rank(word.word)%4<=strength)
            words_.push_back(std::move(word));
    std::sort(words_.begin(),words_.end(),[&](const auto& a,const auto& b) {
        if(level==ComputerLevel::MrBoggle && a.word.size()!=b.word.size())return a.word.size()>b.word.size();
        const auto x=rank(a.word),y=rank(b.word);return x!=y?x<y:a.word<b.word;
    });
    const auto followups=common?groupPlurals(words_):std::vector<uint8_t>(words_.size());
    const size_t groups=size_t(std::count(followups.begin(),followups.end(),uint8_t(0)));
    size_t baseOrdinal=0;
    delays_.reserve(words_.size());
    // The solver bounds the pool to 50,000 words, keeping these products small.
    for(size_t i=0;i<words_.size();++i) {
        uint64_t delay=1000; // Native plural presentation / Mr. Boggle cadence.
        if(common && !followups[i]) {
            const auto& bounds=thinking[strength];const uint64_t remaining=groups-baseOrdinal;
            const uint64_t span=bounds.span+baseOrdinal*bounds.spanStep;
            uint32_t jitter=rank(words_[i].word)^0xa511e9b3u;
            jitter^=jitter>>16;jitter*=0x85ebca6bu;jitter^=jitter>>13;
            delay=bounds.minimum+baseOrdinal*bounds.minimumStep+uint64_t(jitter)%span;
            if(remaining<bounds.scarcity)delay+=(bounds.scarcity-remaining)*bounds.penalty;
            ++baseOrdinal;
        }
        delays_.push_back(delay);
    }
    if(!delays_.empty())due_=delays_.front();
    return Error::success();
}
const AcceptedWord* ComputerPlan::take(uint64_t elapsed) {
    if(exhausted() || elapsed<due_ || (at_ && elapsed<=lastSubmitted_))return nullptr;
    // A stalled window may produce one word, never a catch-up burst.
    const auto* word=&words_[at_++];lastSubmitted_=elapsed;
    due_=exhausted()?0:addTime(elapsed,delays_[at_]);
    return word;
}
const AcceptedWord* ComputerPlan::takeScheduled(uint64_t elapsed,uint64_t& submittedAt) {
    if(exhausted() || elapsed<due_)return nullptr;
    const auto scheduled=due_;const auto* word=take(scheduled);
    if(word)submittedAt=scheduled;
    return word;
}
} // namespace reluva::boggle
