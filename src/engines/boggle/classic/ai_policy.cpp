// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/ai_policy.h"
#include <algorithm>
#include <list>
#include <numeric>
namespace reluva::boggle {
namespace {
// Measured U.S. English Classic parameters. See docs/engines/boggle/ai-milestone.md.
constexpr AiDifficulty difficulties[]={
    {{1000,1000,1000,650,300,100,0,0,500,250,500},500,250,500,200,1500,2000,150,700,1000,9,3000,400,600,50,300},
    {{1000,1000,1000,700,500,250,100,0,700,400,600},700,400,600,300,1200,1700,50,500,1200,7,2000,500,650,50,300},
    {{1000,1000,1000,1000,1000,850,650,450,900,900,900},900,900,900,700,1000,1000,50,300,1400,5,1000,600,700,50,300},
    {{},1000,1000,1000,1000,0,0,0,0,0,0,0,400,500,300,400}
};
std::string past(const std::string& base) {
    if(base.size()<2)return {};
    const char last=base.back();if(last=='E')return base+"D";
    if(std::string_view("AEIOUY").find(last)!=std::string_view::npos)return {};
    const bool repeat=std::string_view("AEIOU").find(base[base.size()-2])!=std::string_view::npos;
    return base+(repeat?std::string(1,last):std::string())+"ED";
}
struct Group {std::vector<size_t> words;unsigned credit=0;};
}
const AiDifficulty& aiDifficulty(ComputerLevel level) {
    return difficulties[validComputerLevel(level) && level!=ComputerLevel::Human?unsigned(level):0];
}
Error chooseComputerWords(const std::vector<AcceptedWord>& pool,ComputerLevel level,
                          const std::function<uint32_t()>& draw,std::vector<AiChoice>& out) {
    out.clear();
    if(!validComputerLevel(level) || level==ComputerLevel::Human || !draw || pool.size()>50000)
        return Error::fail("Invalid Classic AI candidate pool");
    std::vector<size_t> order(pool.size());std::iota(order.begin(),order.end(),0);
    if(level==ComputerLevel::MrBoggle) {
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return pool[a].word.size()>pool[b].word.size();});
        for(size_t i=0;i<order.size();++i)out.push_back({order[i],0,false,order.size()-i});
        return Error::success();
    }
    for(const auto& word:pool)if(word.word.size()<3 || word.word.size()>10)
        return Error::fail("Unsupported Classic common AI candidate length");
    const auto& difficulty=aiDifficulty(level);
    auto chance=[&](int perMille){return int(draw()%1000)<perMille;};
    // Random relocation produces the candidate ordering used by the selection
    // rules. A vector implementation avoids any original list representation.
    if(order.size()>2)for(size_t i=0;i<pool.size()*5;++i) {
        const size_t from=draw()%order.size(),word=order[from];order.erase(order.begin()+from);
        const size_t after=draw()%order.size();order.insert(order.begin()+after+1,word);
    }
    if(pool.size()<15) {
        for(size_t word:order)if(chance(difficulty.lengths[pool[word].word.size()]-400))out.push_back({word});
        std::reverse(out.begin(),out.end());
        for(size_t i=0;i<out.size();++i)out[i].remaining=out.size()-i;
        return Error::success();
    }
    std::vector<uint8_t> available(pool.size(),1);std::list<Group> groups;
    auto variants=[&](size_t base,bool pastFirst=false) {
        std::vector<size_t> result;const auto& text=pool[base].word;
        size_t plural=pool.size(),pastWord=pool.size();
        auto findPlural=[&](){for(size_t word:order)if(available[word] &&
            (pool[word].word==text+"S" || pool[word].word==text+"ES") && chance(difficulty.plural)) {plural=word;break;}};
        auto findPast=[&](){const auto form=past(text);for(size_t word:order)if(available[word] &&
            pool[word].word==form && chance(difficulty.past)) {pastWord=word;break;}};
        // Prefix recognition probes the past gate first, while initial/ending
        // recognition probes the plural first. Submission order stays plural/past.
        if(pastFirst){findPast();findPlural();}else{findPlural();findPast();}
        if(plural!=pool.size())result.push_back(plural);
        if(pastWord!=pool.size())result.push_back(pastWord);
        return result;
    };
    auto makeGroup=[&](size_t base,unsigned credit,const std::vector<size_t>& extra) {
        Group group{{base},credit};available[base]=0;
        for(size_t word:extra)if(available[word]){group.words.push_back(word);available[word]=0;}
        return group;
    };
    // Recognizable suffixes seed groups; shared endings can seed a group even
    // when the base has no plural/past form. Eligibility is probability gated.
    for(size_t base:order)if(available[base]) {
        const auto& text=pool[base].word;const size_t length=text.size();
        if(length>7 || !chance(difficulty.lengths[length]))continue;
        const auto extra=variants(base);bool grouped=!extra.empty();
        if(grouped)groups.push_back(makeGroup(base,0,extra));
        for(size_t word:order)if(available[word] && word!=base) {
            const auto& candidate=pool[word].word;
            if(candidate.size()<length || candidate.size()>7 || candidate.size()-length>1)continue;
            if(!chance(difficulty.lengths[candidate.size()]) || !chance(difficulty.ending))continue;
            if(candidate.substr(candidate.size()-length+1)!=text.substr(1))continue;
            if(!grouped){groups.push_back(makeGroup(base,0,{}));grouped=true;}
            groups.push_back(makeGroup(word,unsigned(length-1),variants(word)));
        }
    }
    // Extend existing groups with shared prefixes, considering longer prefixes
    // first. Newly inserted groups are also eligible to recognize more words.
    for(auto current=groups.begin();current!=groups.end();++current) {
        const std::string text=pool[current->words.front()].word;
        for(size_t length=text.size();length>=3;--length)for(size_t word:order)if(available[word]) {
            const auto& candidate=pool[word].word;
            if(candidate.size()<length || candidate.size()>7)continue;
            if(!chance(difficulty.lengths[candidate.size()]) || !chance(difficulty.prefix))continue;
            if(candidate.compare(0,length,text,0,length)!=0)continue;
            groups.insert(std::next(current),makeGroup(word,unsigned(length),variants(word,true)));
        }
    }
    size_t remaining=groups.size();
    for(const auto& group:groups) {
        for(size_t i=0;i<group.words.size();++i)out.push_back({group.words[i],group.credit,i!=0,remaining});
        --remaining;
    }
    return Error::success();
}
} // namespace reluva::boggle
