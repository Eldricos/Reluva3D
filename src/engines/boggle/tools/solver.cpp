// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/solver.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char** argv) {
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle all-words-check <game folder>\n");return 2;}
    auto fail=[](const Error& e){std::fprintf(stderr,"%s\n",e.message.c_str());return 1;};
    Resources resources;if(auto e=resources.open(argv[2]))return fail(e);
    Dictionary dictionary;if(auto e=resources.dictionary("Dicts/English.dic",dictionary))return fail(e);
    Md5 digest;size_t total=0;
    auto number=[&](uint32_t n){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(n>>(i*8));digest.update(b,4);};
    for(auto variant:{ClassicVariant::Original,ClassicVariant::Master}) {
        ClassicOptions options;options.variant=variant;DiceSet dice;Board board;
        if(auto e=resources.diceSet(options.dice(),dice))return fail(e);
        if(auto e=Board::generate(dice,0x1997,board))return fail(e);
        std::vector<AcceptedWord> found;if(auto e=findBoardWords(board,dictionary,nullptr,found))return fail(e);
        // An independent word-by-word path check verifies completeness on the
        // supplied initial boards. No retail words or faces are printed.
        size_t expected=0;std::vector<size_t> path;
        for(const auto& word:dictionary.words()) {
            std::string normalized;if(word.size()<(options.side()==5?4:3) || !englishWord(word,normalized) || normalized!=word)continue;
            const auto result=board.find(word,path);
            if(result==PathResult::Limit)return fail(Error::fail("Reference board lookup exceeded its limit"));
            if(result==PathResult::Found)++expected;
        }
        if(expected!=found.size())return fail(Error::fail("Board discovery differs from independent dictionary scan"));
        number(uint32_t(options.side()));number(uint32_t(found.size()));
        for(const auto& word:found) {
            if(!dictionary.contains(word.word) || !board.matches(word.word,word.path))return fail(Error::fail("Invalid discovered word/path"));
            digest.update(word.word.data(),word.word.size()+1);number(uint32_t(word.points));number(uint32_t(word.path.size()));
            for(size_t cell:word.path)number(uint32_t(cell));
        }
        total+=found.size();
    }
    std::printf("Complete board vocabularies 2, discovered words %zu\n",total);
    std::printf("decoded all words: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"all-words-check","<game folder>","verify complete board vocabulary against independent lookups; report metadata only",check}});
}
