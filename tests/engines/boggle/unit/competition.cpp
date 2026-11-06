// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/competition.h"
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/support/test.h"
#include <memory>
using namespace reluva;
using namespace reluva::boggle;
namespace {
Dictionary dictionary() {
    std::vector<uint8_t> bytes(12);const std::string words[]={"AAA","AAAA","AAAAA","AAAAAA","AAAAAAA","AAAAAAAA","QUAAA"};
    for(const auto& word:words){bytes.insert(bytes.end(),word.begin(),word.end());bytes.push_back(0);}
    bytes.push_back(0);test::fixtures::put32(bytes,4,(1u<<0)|(1u<<16));test::fixtures::put32(bytes,8,uint32_t(bytes.size()-12));
    Dictionary out;CHECK(!out.read(bytes));return out;
}
Board board(size_t side) {
    std::vector<std::string> faces(side*side,"A");faces[0]="Qu";Board out;CHECK(!out.set(side,faces));return out;
}
void scoring() {
    auto dict=dictionary();const auto cubes=board(4);
    std::vector<std::unique_ptr<ClassicRound>> rounds;
    for(int i=0;i<4;++i){rounds.push_back(std::make_unique<ClassicRound>(dict));CHECK(!rounds.back()->board(cubes));CHECK(!rounds.back()->start(0));}
    const std::vector<std::vector<std::string>> words={{"AAA","AAAA","AAAAA","QUAAA"},{"AAAA","AAAAAA","QUAAA"},{"AAAA","AAAAAAA"},{"AAAAAAAA","AAAA"}};
    for(size_t i=0;i<4;++i){for(const auto& word:words[i])CHECK(rounds[i]->submit(word,0)==Submission::Accepted);rounds[i]->finish();}
    ClassicCompetition results;
    CHECK(!results.build({rounds[0].get()}));CHECK(results.players()[0].score==6);
    CHECK(!results.build({rounds[0].get(),rounds[1].get(),rounds[2].get()}));
    CHECK(results.players().size()==3 && results.players()[0].words[1].owners==7);
    CHECK(!results.build({rounds[0].get(),rounds[1].get(),rounds[2].get(),rounds[3].get()}));
    CHECK(results.ready() && results.players().size()==4);
    const int expected[]={3,3,5,11};
    for(size_t i=0;i<4;++i) {
        CHECK(results.players()[i].score==expected[i]);CHECK(results.players()[i].words.size()==words[i].size());
        for(const auto& row:results.players()[i].words) {
            CHECK(cubes.matches(row.word.entry.word,row.word.entry.path));
            if(row.word.entry.word=="AAAA")CHECK(row.owners==15 && row.shared() && row.points==0);
            if(row.word.entry.word=="QUAAA")CHECK(row.owners==3 && row.shared() && row.points==0 && row.word.entry.points==2 && row.word.entry.path.size()==4);
        }
    }
    RoundResults review;CHECK(!review.build(*rounds[0],results.players()[0]));
    CHECK(review.score()==3 && review.countedCount()==2 && review.words().size()==4);
    CHECK(review.shared("AAAA") && review.shared("QUAAA") && review.words()[1].entry.points==0);
    CHECK(!review.showAll(true,dict));CHECK(review.score()==3 && review.countedCount()==2 && review.shared("AAAA"));
    auto corrupt=results.players()[0];++corrupt.score;CHECK(bool(review.build(*rounds[0],corrupt)));CHECK(!review.ready() && !review.shared("AAAA"));
    CHECK(!review.build(*rounds[0]));CHECK(review.score()==6 && review.countedCount()==4 && !review.shared("AAAA"));
    CHECK(rounds[0]->score()==6 && rounds[1]->score()==6); // provisional source scores are untouched
    CHECK(!rounds[0]->start(1));CHECK(results.players()[0].score==3); // immutable completed match
    results.clear();CHECK(!results.ready() && results.players().empty());
}
void decisions(const std::filesystem::path& tmp) {
    auto dict=dictionary();UserDictionary user;CHECK(!user.load(tmp / "save"));const auto cubes=board(5);
    ClassicRound one(dict,&user),two(dict,&user);CHECK(!one.board(cubes));CHECK(!two.board(cubes));
    CHECK(!one.start(0));CHECK(!two.start(0));
    CHECK(one.submit("QUAAAA",0)==Submission::UnknownWord);CHECK(two.submit("QUAAAA",0)==Submission::UnknownWord);
    one.finish();two.finish();ClassicCompetition results;CHECK(bool(results.build({&one,&two})));CHECK(!results.ready());
    CHECK(!one.decide(DictionaryChoice::Yes));CHECK(bool(results.build({&one,&two})));CHECK(!results.ready());
    CHECK(!two.decide(DictionaryChoice::No));CHECK(!results.build({&one,&two}));
    CHECK(results.players()[0].score==3 && results.players()[1].score==0 && results.players()[1].words.empty());
    const auto& row=results.players()[0].words[0];CHECK(row.word.state==WordState::Added && row.owners==1 && !row.shared());
    CHECK(!two.start(1));CHECK(two.submit("QUAAAA",1)==Submission::Accepted);two.finish();
    CHECK(!results.build({&one,&two}));CHECK(results.players()[0].score==0 && results.players()[1].score==0);
    CHECK(results.players()[0].words[0].shared());
}
void guards() {
    auto dict=dictionary();ClassicRound one(dict),two(dict);ClassicCompetition results;
    CHECK(bool(results.build({})));CHECK(bool(results.build({nullptr})));CHECK(bool(results.build({&one,&one})));
    CHECK(!one.board(board(4)));CHECK(!one.start(0));one.finish();
    CHECK(!results.build({&one}));CHECK(bool(results.build({&one,nullptr})));CHECK(!results.ready());
    CHECK(bool(results.build({&one,&one,&one,&one,&one})));
    CHECK(!two.board(board(5)));CHECK(!two.start(0));two.finish();CHECK(bool(results.build({&one,&two})));
    auto different=board(4);auto faces=different.faces();faces[1]="B";CHECK(!different.set(4,faces));
    CHECK(!two.board(different));CHECK(!two.start(0));two.finish();CHECK(bool(results.build({&one,&two})));
    CHECK(!two.board(board(4)));CHECK(bool(results.build({&one,&two})));CHECK(!two.start(0));CHECK(bool(results.build({&one,&two})));
    two.finish();CHECK(!results.build({&one,&two}));CHECK(results.players().size()==2 && results.players()[0].score==0 && results.players()[1].score==0);
}
}
static const test::Suite suite("boggle.unit.competition",{
    {"four-player shared-word cancellation, Qu paths and independent snapshots",[](const auto&){scoring();}},
    {"Master custom words count only after each player's final decision",[](const auto& tmp){decisions(tmp);}},
    {"player bounds, null/repeated rounds, board identity and finalization guards",[](const auto&){guards();}},
});
