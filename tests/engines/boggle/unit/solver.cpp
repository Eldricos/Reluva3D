// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/solver.h"
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;using namespace reluva::boggle;
namespace {
Dictionary dictionary(std::vector<std::string> list) {
    std::sort(list.begin(),list.end());std::vector<uint8_t> bytes(12);uint32_t mask=0;
    for(const auto& word:list){mask|=uint32_t(1)<<unsigned(word[0]-'A');bytes.insert(bytes.end(),word.begin(),word.end());bytes.push_back(0);}
    bytes.push_back(0);test::fixtures::put32(bytes,4,mask);test::fixtures::put32(bytes,8,uint32_t(bytes.size()-12));
    Dictionary out;CHECK(!out.read(bytes));return out;
}
void paths(const std::filesystem::path& tmp) {
    auto words=dictionary({"CAT","CATS","QUIT","QUITS","QIT","ACA","ABC","XYZ","CA"});
    Board board;CHECK(!board.set(4,{"C","A","T","S","X","Qu","I","T","B","D","E","F","G","H","J","K"}));
    UserDictionary user;CHECK(!user.load(tmp / "save"));CHECK(!user.add({"CAX","CAT"}));
    std::vector<AcceptedWord> found;CHECK(!findBoardWords(board,words,&user,found));
    std::vector<std::string> names;for(const auto& word:found){names.push_back(word.word);CHECK(board.matches(word.word,word.path));CHECK(word.points==classicPoints(word.word.size()));}
    CHECK(names==std::vector<std::string>({"CAT","CATS","CAX","QUIT","QUITS"}));
    const auto qu=std::find_if(found.begin(),found.end(),[](const auto& w){return w.word=="QUIT";});CHECK(qu!=found.end());if(qu!=found.end())CHECK(qu->path.size()==3);
    CHECK(bool(findBoardWords(board,words,&user,found,1)));CHECK(found.empty());
    CHECK(bool(findBoardWords(Board{},words,&user,found)));CHECK(found.empty());
    Board noReuse;std::vector<std::string> faces(16,"X");faces[0]="A";faces[1]="B";faces[15]="A";
    CHECK(!noReuse.set(4,faces));auto repeated=dictionary({"ABA","BAA"});
    CHECK(!findBoardWords(noReuse,repeated,nullptr,found));CHECK(found.empty());
    Board noWrap;faces.assign(16,"X");faces[3]="C";faces[4]="A";faces[5]="T";CHECK(!noWrap.set(4,faces));
    auto wrapped=dictionary({"CAT"});CHECK(!findBoardWords(noWrap,wrapped,nullptr,found));CHECK(found.empty());
    Board groups;CHECK(!groups.set(5,std::vector<std::string>(25,"AB")));
    auto partial=dictionary({"AABB","ABAB"});CHECK(bool(findBoardWords(groups,partial,nullptr,found,3)));CHECK(found.empty());
    auto unavailable=dictionary({"ZZZ"});CHECK(!findBoardWords(board,unavailable,nullptr,found,0));CHECK(found.empty());
}
void bounded() {
    Board board;std::vector<std::string> faces(16,"A");faces.back()="QU";CHECK(!board.set(4,faces));
    auto words=dictionary({std::string(9,'A'),std::string(10,'A'),std::string(8,'A')+"QU",std::string(9,'A')+"QU"});
    std::vector<AcceptedWord> found;CHECK(!findBoardWords(board,words,nullptr,found,500000,9));
    CHECK(found.size()==2);
    for(const auto& word:found) {
        CHECK(board.matches(word.word,word.path));
        CHECK(word.word.size()==9 || (word.word.size()==10 && board.faces()[word.path.back()]=="QU"));
    }
    CHECK(!findBoardWords(board,words,nullptr,found) && found.size()==4);
    CHECK(bool(findBoardWords(board,words,nullptr,found,500000,0)) && found.empty());
    CHECK(bool(findBoardWords(board,words,nullptr,found,500000,65)) && found.empty());
    // Ineligible long words must not spend the bounded traversal's visit budget.
    auto onlyLong=dictionary({std::string(10,'A')});CHECK(!findBoardWords(board,onlyLong,nullptr,found,0,9) && found.empty());
}
void masterAndResults(const std::filesystem::path& tmp) {
    const auto words=dictionary({"AAA","AAAA","AAAAA","AAAAAA"});
    Board master;CHECK(!master.set(5,std::vector<std::string>(25,"A")));
    std::vector<AcceptedWord> found;CHECK(!findBoardWords(master,words,nullptr,found));CHECK(found.size()==3);
    // Repeated-letter paths stop once all eligible targets are proved, without
    // exploring every permutation of the same word.
    CHECK(!findBoardWords(master,words,nullptr,found,6));CHECK(found.size()==3);
    UserDictionary user;CHECK(!user.load(tmp / "save"));CHECK(!user.add({"AAAAAAA"}));
    ClassicRound round(words,&user);CHECK(!round.board(master));RoundResults results;
    CHECK(bool(results.showAll(true,words,&user)));CHECK(!round.start(0));
    CHECK(round.submit("AAAA",0,{20,21,22,23})==Submission::Accepted);round.finish();CHECK(!results.build(round));
    CHECK(!results.showAll(true,words,&user));CHECK(results.allWords() && results.words().size()==4);
    CHECK(results.score()==1 && results.countedCount()==1);CHECK(!results.missed("AAAA"));CHECK(results.missed("AAAAAAA"));
    CHECK(results.words()[0].entry.path==std::vector<size_t>({20,21,22,23})); // retained submitted path
    CHECK(results.words()[3].entry.points==5); // potential points, not awarded
    CHECK(!results.showAll(false,words,&user));CHECK(results.words().size()==1 && results.score()==1);
    CHECK(!results.showAll(true,words,&user));CHECK(results.words().size()==4 && results.score()==1);
    results.clear();CHECK(!results.allWords() && !results.ready() && results.words().empty());
    CHECK(!master.set(5,std::vector<std::string>(25,"B")));CHECK(!round.board(master));CHECK(!round.start(0));round.finish();
    CHECK(!results.build(round));CHECK(!results.showAll(true,words,&user));CHECK(results.words().empty());
}
}
static const test::Suite suite("boggle.unit.solver",{
    {"complete English paths, adjacency, no reuse, Qu, deduplication and limits",[](const auto& tmp){paths(tmp);}},
    {"prefix limits stop extensions, allow a final Qu crossing and clear failures",[](const auto&){bounded();}},
    {"Master minimum, repeated letters, saved words and score-preserving view toggles",[](const auto& tmp){masterAndResults(tmp);}},
});
