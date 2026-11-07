// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/cursor.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
#include <algorithm>
#include "engines/boggle/classic/solver.h"
namespace {
int check(int argc,char** argv) {
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle computer-check <game folder>\n");return 2;}
    auto fail=[](const Error& e){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;};
    Resources resources;if(auto e=resources.open(argv[2]))return fail(e);
    Dictionary english,vocabulary;
    if(auto e=resources.dictionary("Dicts/English.dic",english))return fail(e);
    if(auto e=resources.dictionary("Dicts/AI.dic",vocabulary))return fail(e);
    Md5 digest;size_t plans=0,words=0;
    auto number=[&](uint32_t n){uint8_t bytes[4];for(int i=0;i<4;++i)bytes[i]=uint8_t(n>>(i*8));digest.update(bytes,4);};
    for(auto variant:{ClassicVariant::Original,ClassicVariant::Master})for(uint32_t seed:{0x1997u,0x1998u}) {
        ClassicOptions options;options.variant=variant;DiceSet dice;Board board;
        if(auto e=resources.diceSet(options.dice(),dice))return fail(e);
        if(auto e=Board::generate(dice,seed,board))return fail(e);
        for(size_t slot=0;slot<4;++slot)for(unsigned level=0;level<4;++level) {
            ComputerPlan plan;if(auto e=plan.build(board,vocabulary,english,ComputerLevel(level),seed,slot))return fail(e);
            if(usesCommonVocabulary(ComputerLevel(level))) {
                for(const auto& word:plan.words())
                    if(!vocabulary.contains(word.word) || word.word.size()-board.faces()[word.path.back()].size()>=9)
                        return fail(Error::fail("Common computer escaped its vocabulary/search boundary"));
            } else {
                if(!std::is_sorted(plan.words().begin(),plan.words().end(),[](const auto& a,const auto& b){return a.word.size()>b.word.size();}))
                    return fail(Error::fail("Mr. Boggle did not prioritize longest words"));
                std::vector<AcceptedWord> complete;
                if(auto error=findBoardWords(board,english,nullptr,complete))return fail(error);
                std::vector<std::string> expected,actual;
                for(const auto& word:complete)expected.push_back(word.word);
                for(const auto& word:plan.words())actual.push_back(word.word);
                std::sort(actual.begin(),actual.end());
                if(actual!=expected)return fail(Error::fail("Mr. Boggle did not retain the complete main-dictionary board vocabulary"));
            }
            ClassicRound round(english);if(auto e=round.board(board,0))return fail(e);
            if(auto e=round.start(0,0))return fail(e);
            number(uint32_t(board.side()));number(seed);number(uint32_t(slot));number(level);number(uint32_t(plan.words().size()));
            std::vector<ClassicPoint> centers;
            for(size_t i=0;i<board.faces().size();++i)centers.push_back({int(i%board.side())*100,int(i/board.side())*100});
            for(uint64_t now=6000;!plan.exhausted();now+=6000) {
                const auto* pending=plan.nextWord();const auto due=plan.due();
                const auto traced=computerCursorFrame(*pending,centers,{0,0},due,due,{900,500});
                if(!traced.active || traced.cubes!=pending->path.size() || plan.nextWord()!=pending || plan.due()!=due)
                    return fail(Error::fail("Computer cursor preview changed a plan or failed to finish its path"));
                const auto* word=plan.take(now);
                if(!word || plan.take(now) || round.submit(word->word,now,word->path)!=Submission::Accepted)
                    return fail(Error::fail("Computer plan failed paced dictionary/path validation"));
                digest.update(word->word.data(),word->word.size()+1);number(uint32_t(word->points));
                for(size_t cell:word->path)number(uint32_t(cell));
                ++words;
            }
            ++plans;
        }
    }
    std::printf("Classic computer plans %zu, legal submissions %zu\n",plans,words);
    std::printf("decoded computers: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"computer-check","<game folder>","validate native computer plans against supplied dictionaries and boards; metadata only",check}});
}
