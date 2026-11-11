// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/ai_policy.h"
#include "engines/boggle/classic/solver.h"
#include <algorithm>
#include <limits>
namespace reluva::boggle {
namespace {
uint64_t addTime(uint64_t start,uint64_t delay) {
    return start>std::numeric_limits<uint64_t>::max()-delay?std::numeric_limits<uint64_t>::max():start+delay;
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
                          ComputerLevel level,uint32_t seed,size_t slot,size_t visits,const ComputerGeometry* suppliedGeometry) {
    words_.clear();delays_.clear();motions_.clear();at_=0;due_=lastSubmitted_=finishAt_=finishDelay_=0;
    if(!validComputerLevel(level) || level==ComputerLevel::Human || slot>=4)
        return Error::fail("Invalid computer participant");
    const bool common=usesCommonVocabulary(level);std::vector<AcceptedWord> pool;
    if(auto error=findBoardWords(board,common?commonVocabulary:english,nullptr,pool,visits,common?9:64))return error;
    pool.erase(std::remove_if(pool.begin(),pool.end(),[&](const auto& word){return !english.contains(word.word);}),pool.end());
    // Board DFS discovers paths in cube-index order; prefix words precede extensions.
    std::sort(pool.begin(),pool.end(),[](const auto& a,const auto& b){return a.path!=b.path?a.path<b.path:a.word<b.word;});
    uint32_t state=seed^(uint32_t(slot+1)*0x9e3779b9u);
    auto draw=[&](){state=state*1664525u+1013904223u;return (state>>16)&32767u;};
    std::vector<AiChoice> choices;if(auto error=chooseComputerWords(pool,level,draw,choices))return error;
    const auto& profile=aiDifficulty(level);uint64_t minimum=profile.minimum,span=profile.span;
    const auto geometry=suppliedGeometry?*suppliedGeometry:defaultComputerGeometry(board.side());
    std::vector<AcceptedWord> words;std::vector<uint64_t> delays;std::vector<ComputerMotion> motions;
    ClassicPoint origin=geometry.origin;
    for(size_t i=0;i<choices.size();++i) {
        const auto& choice=choices[i];uint64_t thinking=0,gap=0;
        if(i) {
            gap=common?uint64_t(int64_t(profile.gap)*1000+(int(draw()%2000)-1000)*100)/1000:profile.gap;
            if(common && !choice.followup) {
                if(choice.credit) {
                    const uint64_t available=minimum+draw()%1500,rebate=profile.rebate*choice.credit;
                    thinking=available>rebate?available-rebate:0;
                } else {
                    thinking=minimum+draw()%span;
                    if(choice.remaining<profile.scarcity)thinking+=(profile.scarcity-choice.remaining)*profile.penalty;
                    span+=profile.spanStep;
                }
                minimum+=profile.minimumStep;
            }
        }
        std::vector<uint64_t> holds;
        for(size_t cell:pool[choice.word].path){(void)cell;holds.push_back(common?uint64_t(int64_t(profile.hold)*1000+(int(draw()%2000)-1000)*40)/1000:profile.hold);}
        ComputerMotion motion;
        if(auto error=makeComputerMotion(pool[choice.word],geometry,origin,profile.cubeSpeed,profile.submitSpeed,holds,motion))return error;
        delays.push_back(((gap+thinking+49)/50)*50+motion.duration);motions.push_back(std::move(motion));words.push_back(pool[choice.word]);origin=geometry.submit;
    }
    if(!words.empty() && common) {
        // Sequence random draws explicitly for consistent Windows/Linux replay.
        const auto gapSample=draw()%2000;
        const auto searchSample=draw()%7000;
        const auto wait=uint64_t(int64_t(profile.gap)*1000+(int(gapSample)-1000)*100)/1000+10000+searchSample;
        finishDelay_=((wait+49)/50)*50+50;
    }
    words_=std::move(words);delays_=std::move(delays);motions_=std::move(motions);
    if(!delays_.empty())due_=delays_.front();
    return Error::success();
}
const AcceptedWord* ComputerPlan::take(uint64_t elapsed) {
    if(exhausted() || elapsed<due_ || (at_ && elapsed<=lastSubmitted_))return nullptr;
    const auto* word=&words_[at_++];lastSubmitted_=elapsed;
    if(exhausted()){due_=0;finishAt_=addTime(elapsed,finishDelay_);}
    else due_=addTime(elapsed,delays_[at_]);
    return word;
}
const AcceptedWord* ComputerPlan::takeScheduled(uint64_t elapsed,uint64_t& submittedAt) {
    if(exhausted() || elapsed<due_)return nullptr;
    const auto scheduled=due_;const auto* word=take(scheduled);
    if(word)submittedAt=scheduled;
    return word;
}
} // namespace reluva::boggle
