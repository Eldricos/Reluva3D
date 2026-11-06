// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/computer.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char** argv) {
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle background-check <game folder>\n");return 2;}
    auto fail=[](const Error& error){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;};
    Resources resources;if(auto error=resources.open(argv[2]))return fail(error);
    Dictionary english,vocabulary;
    if(auto error=resources.dictionary("Dicts/English.dic",english))return fail(error);
    if(auto error=resources.dictionary("Dicts/AI.dic",vocabulary))return fail(error);
    Md5 digest;size_t schedules=0,submissions=0;
    auto number=[&](uint32_t n){uint8_t bytes[4];for(int i=0;i<4;++i)bytes[i]=uint8_t(n>>(i*8));digest.update(bytes,4);};
    for(auto variant:{ClassicVariant::Original,ClassicVariant::Master})for(uint32_t seed:{0x1997u,0x1998u}) {
        ClassicOptions options;options.variant=variant;DiceSet dice;Board board;
        if(auto error=resources.diceSet(options.dice(),dice))return fail(error);
        if(auto error=Board::generate(dice,seed,board))return fail(error);
        for(size_t slot=0;slot<4;++slot)for(unsigned level=0;level<4;++level) {
            ComputerPlan original;if(auto error=original.build(board,vocabulary,english,ComputerLevel(level),seed,slot))return fail(error);
            for(uint64_t duration:{0u,60000u,180000u,300000u}) {
                const uint64_t end=duration?duration-1:137000;
                std::vector<std::pair<std::string,uint64_t>> baseline;
                int baselineScore=0;
                for(uint64_t step:{end,uint64_t(997)}) {
                    ComputerPlan plan=original;ClassicRound round(english);
                    if(auto error=round.board(board,duration))return fail(error);
                    if(auto error=round.start(0,duration))return fail(error);
                    std::vector<std::pair<std::string,uint64_t>> moves;
                    for(uint64_t frame=0;;frame=std::min(end,frame+step)) {
                        uint64_t at=0;
                        while(const auto* word=plan.takeScheduled(frame,at)) {
                            if((duration && at>=duration) || round.submit(word->word,at,word->path)!=Submission::Accepted)
                                return fail(Error::fail("Background word failed the canonical round validator"));
                            moves.push_back({word->word,at});
                        }
                        if(frame==end)break;
                    }
                    round.finish();
                    if(step==end){baseline=moves;baselineScore=round.score();}
                    else if(moves!=baseline || round.score()!=baselineScore)
                        return fail(Error::fail("Background results depend on service-frame cadence"));
                }
                number(uint32_t(board.side()));number(seed);number(uint32_t(slot));number(level);number(uint32_t(duration));
                number(uint32_t(baselineScore));number(uint32_t(baseline.size()));
                for(const auto& [word,at]:baseline){digest.update(word.data(),word.size()+1);number(uint32_t(at));}
                ++schedules;submissions+=baseline.size();
            }
        }
    }
    std::printf("Background schedules %zu, legal submissions %zu\n",schedules,submissions);
    std::printf("decoded background: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"background-check","<game folder>","verify hidden computer schedules, timers and frame independence; metadata only",check}});
}
