// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/solver.h"
#include "engines/boggle/classic/user_dictionary.h"
#include <algorithm>
#include <array>
#include <set>
namespace reluva::boggle {
Error findBoardWords(const Board& board,const Dictionary& dictionary,const UserDictionary* user,
                     std::vector<AcceptedWord>& out,size_t limit) {
    out.clear();
    if(board.side()!=4 && board.side()!=5)return Error::fail("A Classic board is required for word discovery");
    std::array<size_t,26> available{};
    for(const auto& face:board.faces())for(char c:face)++available[size_t(c-'A')];
    const size_t minimum=board.side()==5?4:3;
    std::set<std::string> eligible;
    auto add=[&](const std::string& word) {
        if(word.size()<minimum || word.size()>64)return;
        std::string normalized;if(!englishWord(word,normalized) || normalized!=word)return;
        std::array<size_t,26> used{};
        for(char c:word)if(++used[size_t(c-'A')]>available[size_t(c-'A')])return;
        eligible.insert(word);
    };
    for(const auto& word:dictionary.words()) {
        add(word);if(eligible.size()>50000)return Error::fail("Board vocabulary exceeds the discovery limit");
    }
    if(user)for(const auto& word:user->words()) {
        add(word);if(eligible.size()>50000)return Error::fail("Board vocabulary exceeds the discovery limit");
    }
    if(eligible.empty())return Error::success();
    const std::vector<std::string> words(eligible.begin(),eligible.end());
    std::vector<std::vector<size_t>> paths(words.size());
    std::vector<std::vector<size_t>> neighbors(board.faces().size());
    for(size_t i=0;i<neighbors.size();++i)for(size_t j=0;j<neighbors.size();++j)
        if(board.adjacent(i,j))neighbors[i].push_back(j);
    size_t visits=0,found=0;bool exhausted=false;std::string prefix;std::vector<size_t> path;
    auto walk=[&](auto&& self,size_t cell,uint32_t used,size_t first,size_t last)->void {
        if(found==words.size() || exhausted)return;
        if(visits==limit){exhausted=true;return;}++visits;
        const size_t length=prefix.size();prefix+=board.faces()[cell];
        const auto begin=std::lower_bound(words.begin()+first,words.begin()+last,prefix);
        const auto end=std::lower_bound(begin,words.begin()+last,prefix+"[");
        if(begin!=end) {
            const size_t lo=size_t(begin-words.begin()),hi=size_t(end-words.begin());
            used|=uint32_t(1)<<cell;path.push_back(cell);
            if(*begin==prefix && paths[lo].empty()){paths[lo]=path;++found;}
            for(size_t next:neighbors[cell])if(!(used&(uint32_t(1)<<next)))self(self,next,used,lo,hi);
            path.pop_back();
        }
        prefix.resize(length);
    };
    for(size_t cell=0;cell<board.faces().size();++cell)walk(walk,cell,0,0,words.size());
    if(exhausted)return Error::fail("Board-word search limit reached; no complete list is available");
    std::vector<AcceptedWord> complete;complete.reserve(found);
    for(size_t i=0;i<words.size();++i)if(!paths[i].empty())complete.push_back({words[i],classicPoints(words[i].size(),minimum),std::move(paths[i])});
    out=std::move(complete);return Error::success();
}
} // namespace reluva::boggle
