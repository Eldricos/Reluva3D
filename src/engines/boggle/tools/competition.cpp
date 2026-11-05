// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/competition.h"
#include "engines/boggle/classic/solver.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
#include <memory>
namespace {
int check(int argc,char** argv) {
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle competition-check <game folder>\n");return 2;}
    auto fail=[](const Error& e){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;};
    Resources resources;if(auto e=resources.open(argv[2]))return fail(e);
    Dictionary dictionary;if(auto e=resources.dictionary("Dicts/English.dic",dictionary))return fail(e);
    Md5 digest;size_t matches=0,players=0;
    auto number=[&](uint32_t n){uint8_t bytes[4];for(int i=0;i<4;++i)bytes[i]=uint8_t(n>>(i*8));digest.update(bytes,4);};
    for(auto variant:{ClassicVariant::Original,ClassicVariant::Master}) {
        ClassicOptions options;options.variant=variant;DiceSet dice;Board board;
        if(auto e=resources.diceSet(options.dice(),dice))return fail(e);
        if(auto e=Board::generate(dice,0x1997,board))return fail(e);
        std::vector<AcceptedWord> found;if(auto e=findBoardWords(board,dictionary,nullptr,found))return fail(e);
        for(size_t count:{size_t(1),size_t(2),size_t(4)}) {
            std::vector<std::unique_ptr<ClassicRound>> rounds;
            std::vector<const ClassicRound*> input;
            std::vector<int> expected(count,0);
            for(size_t p=0;p<count;++p) {
                auto round=std::make_unique<ClassicRound>(dictionary);
                if(auto e=round->board(board))return fail(e);
                if(auto e=round->start(0))return fail(e);
                for(size_t i=0;i<found.size();++i) {
                    const auto mask=uint8_t((1+i%15)&((1u<<count)-1));
                    if(!(mask&(1u<<p)))continue;
                    if(round->submit(found[i].word,0,found[i].path)!=Submission::Accepted)
                        return fail(Error::fail("Competition fixture submission was rejected"));
                    if((mask&(mask-1))==0)expected[p]+=found[i].points;
                }
                round->finish();input.push_back(round.get());rounds.push_back(std::move(round));
            }
            ClassicCompetition results;if(auto e=results.build(input))return fail(e);
            number(uint32_t(board.side()));number(uint32_t(count));
            for(size_t p=0;p<count;++p) {
                const auto& player=results.players()[p];
                if(player.score!=expected[p])return fail(Error::fail("Competition final score differs from independent ownership totals"));
                number(uint32_t(player.score));number(uint32_t(player.words.size()));
                for(const auto& row:player.words) {
                    if(!board.matches(row.word.entry.word,row.word.entry.path))return fail(Error::fail("Competition path changed during scoring"));
                    number(row.owners);number(uint32_t(row.points));digest.update(row.word.entry.word.data(),row.word.entry.word.size()+1);
                }
            }
            ++matches;players+=count;
        }
    }
    std::printf("Classic competitions %zu, finalized players %zu\n",matches,players);
    std::printf("decoded competitions: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"competition-check","<game folder>","verify final shared-word scoring on both Classic boards; report metadata only",check}});
}
