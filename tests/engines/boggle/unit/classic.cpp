// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "tests/support/test.h"
#include <algorithm>

using namespace reluva;
using namespace reluva::boggle;
namespace {
Dictionary dictionary() {
    std::vector<uint8_t> bytes;
    auto add = [&](uint32_t n) { for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(n >> (i * 8))); };
    // Own vocabulary and an intentionally unsorted root suffix list.
    const char list[] = "CAT\0CATS\0QUIT\0QUITS\0TAC\0ACA\0AB\0ZZZ\0";
    const std::string words(list, sizeof(list));
    // Derive the continuation hint from each supplied suffix's first letter.
    uint32_t mask = 0; for (size_t i = 0; i < words.size();) { if (!words[i]) break; mask |= 1u << unsigned(words[i] - 'A'); while (i < words.size() && words[i]) ++i; ++i; }
    add(0); add(mask); add(uint32_t(words.size())); bytes.insert(bytes.end(), words.begin(), words.end());
    Dictionary result; CHECK(!result.read(bytes)); return result;
}
Board board() {
    Board b; CHECK(!b.set(4, {"C","A","T","S", "X","Qu","I","T", "B","D","E","F", "G","H","J","K"})); return b;
}
void keyboard() {
    std::string input;bool following=false;
    CHECK(appendEnglishCubes("q",input,following) && input=="QU" && following);
    CHECK(appendEnglishCubes("u",input,following) && input=="QU" && !following);
    CHECK(appendEnglishCubes("it",input,following) && input=="QUIT");
    input.clear();following=false;
    CHECK(appendEnglishCubes("qit",input,following) && input=="QUIT");
    input.clear();following=false;
    CHECK(appendEnglishCubes("quu",input,following) && input=="QUU");
    eraseEnglishCube(input);CHECK(input=="QU");eraseEnglishCube(input);CHECK(input.empty());
    input=std::string(63,'A');following=false;
    CHECK(!appendEnglishCubes("q",input,following) && input.size()==63 && !following);
    CHECK(!appendEnglishCubes("a!",input,following) && input.size()==63);
}
void boardKeyboard() {
    Board b;CHECK(!b.set(4,{"C","X","X","C","A","X","X","A","X","X","T","X","X","X","X","X"}));
    std::string input;std::vector<size_t> path;bool following=false;
    CHECK(enterBoardText(b,"CA",input,path,following)==KeyboardResult::Entered);
    CHECK(input=="CA" && path==std::vector<size_t>({0,4}));
    CHECK(enterBoardText(b,"T",input,path,following)==KeyboardResult::Entered);
    CHECK(input=="CAT" && path==std::vector<size_t>({3,7,10})); // earlier ambiguity must not prevent a valid word
    const auto prior=path;
    CHECK(enterBoardText(b,"CZ",input,path,following)==KeyboardResult::NotOnBoard);
    CHECK(input=="CAT" && path==prior); // no reuse or disconnected extension
    input.clear();path.clear();
    CHECK(enterBoardText(b,"ZZCATZ",input,path,following)==KeyboardResult::NotOnBoard);
    CHECK(input=="CAT" && b.matches(input,path));
    CHECK(enterBoardText(b,"!",input,path,following)==KeyboardResult::InvalidLetters && input=="CAT");
    input.clear();path.clear();
    CHECK(enterBoardText(b,"C",input,path,following,0)==KeyboardResult::Limit && input.empty() && path.empty());
    b=board();
    CHECK(enterBoardText(b,"Q",input,path,following)==KeyboardResult::Entered && input=="QU" && following);
    CHECK(enterBoardText(b,"U",input,path,following)==KeyboardResult::Entered && input=="QU" && !following);
    CHECK(enterBoardText(b,"IT",input,path,following)==KeyboardResult::Entered && input=="QUIT" && path.size()==3);
    path.pop_back();eraseEnglishCube(input);CHECK(input=="QUI" && b.matches(input,path));
    path.pop_back();eraseEnglishCube(input);CHECK(input=="QU" && b.matches(input,path));
    path.pop_back();eraseEnglishCube(input);CHECK(input.empty() && path.empty());
    CHECK(enterBoardText(b,"U",input,path,following)==KeyboardResult::NotOnBoard && input.empty());
    CHECK(enterBoardText(b,"QIT",input,path,following)==KeyboardResult::Entered && input=="QUIT");
}
void sharedReview(const std::filesystem::path& tmp) {
    auto d=dictionary();UserDictionary user;CHECK(!user.load(tmp / "save"));
    ClassicRound first(d,&user),second(d,&user),declined(d,&user);
    for(auto* round:{&first,&second,&declined}) {
        CHECK(!round->board(board()));CHECK(!round->start(0));
        CHECK(round->submit("CAI",1)==Submission::UnknownWord);round->finish();
    }
    CHECK(!declined.decide(DictionaryChoice::No));
    CHECK(!first.decide(DictionaryChoice::Yes));second.acceptKnownPending();declined.acceptKnownPending();
    CHECK(!second.candidate() && second.score()==1 && second.accepted().size()==1);
    CHECK(!declined.candidate() && declined.score()==0 && declined.accepted().empty());
}
void paths() {
    auto b = board(); std::vector<size_t> path;
    CHECK(b.adjacent(0,5)); CHECK(!b.adjacent(3,4)); CHECK(!b.adjacent(0,0)); CHECK(!b.adjacent(0,99));
    CHECK(b.find("CATS",path) == PathResult::Found); CHECK(b.matches("CATS",path));
    CHECK(b.find("QUIT",path) == PathResult::Found); CHECK(path.size() == 3); CHECK(path[0] == 5);
    CHECK(b.find("QUITS",path) == PathResult::Found); CHECK(path.size() == 4);
    CHECK(b.matches("QUITS",path)); CHECK(classicPoints(5) == 2);
    CHECK(b.find("QIT",path) == PathResult::Missing); CHECK(path.empty());
    CHECK(b.find("ACA",path) == PathResult::Missing); // The single A cannot be revisited.
    CHECK(!b.matches("ACA", {1,0,1})); CHECK(!b.matches("SX", {3,4}));
    CHECK(!b.matches("CAT", {0,1,99})); CHECK(!b.matches("CAT", {}));
    CHECK(b.find("CATS",path,0) == PathResult::Limit); CHECK(path.empty());
    CHECK(b.find(std::string(65,'A'),path) == PathResult::Missing);
}
void rounds() {
    auto d = dictionary(); ClassicRound round(d); auto b = board();
    CHECK(!round.board(b)); CHECK(round.phase() == RoundPhase::Ready);
    CHECK(round.submit("CAT",0) == Submission::NotPlaying);
    CHECK(!round.start(1000,1000)); CHECK(round.remaining(999) == 1000);
    CHECK(bool(round.start(1001)));
    CHECK(round.submit("ab",1100) == Submission::TooShort);
    CHECK(round.submit("cat",1100) == Submission::Accepted); CHECK(round.score() == 1);
    CHECK(round.submit("CAT",1200) == Submission::Duplicate); CHECK(round.score() == 1);
    CHECK(round.submit("QUIT",1300,{5,6,7}) == Submission::Accepted); CHECK(round.score() == 2);
    CHECK(round.submit("QUITS",1350,{5,6,7,3}) == Submission::Accepted); CHECK(round.score() == 4);
    CHECK(round.submit("ACA",1400) == Submission::NotOnBoard);
    CHECK(round.submit("ZZZ",1400) == Submission::NotOnBoard);
    CHECK(round.submit("NOTAWORD",1400) == Submission::NotOnBoard);
    CHECK(round.submit("C A T",1400) == Submission::InvalidLetters);
    CHECK(bool(round.board(b))); CHECK(round.remaining(1999) == 1);
    CHECK(round.submit("CATS",2000) == Submission::NotPlaying);
    CHECK(round.phase() == RoundPhase::Results && round.score() == 4);
    CHECK(!round.start(3000)); CHECK(round.remaining(3000) == 180000 && round.score() == 0);
    CHECK(round.submit("CATS",3000,{0,1,2,3}) == Submission::Accepted);
    round.finish(); CHECK(round.remaining(3000) == 0); CHECK(round.accepted().size() == 1);
    CHECK(!round.board(b)); CHECK(round.phase() == RoundPhase::Ready && round.accepted().empty());
}
void review(const std::filesystem::path& tmp) {
    auto d = dictionary(); UserDictionary user; CHECK(!user.load(tmp / "save"));
    ClassicRound round(d, &user); CHECK(!round.board(board())); CHECK(!round.start(0));
    CHECK(round.submit("CAT",1) == Submission::Accepted);
    CHECK(round.submit("CAX",2) == Submission::UnknownWord);
    CHECK(round.submit("cax",3) == Submission::Duplicate);
    CHECK(round.submit("XAC",4) == Submission::UnknownWord);
    CHECK(round.submit("CATI",5) == Submission::UnknownWord);
    CHECK(round.submit("CATIZ",6) == Submission::NotOnBoard);
    CHECK(round.score() == 4); // CATI is four letters, so each entry is one point.
    CHECK(round.words().size() == 4 && round.accepted().size() == 1);
    CHECK(!round.candidate()); round.finish(); CHECK(round.candidate());
    CHECK(bool(round.start(10))); CHECK(bool(round.board(board())));
    CHECK(!round.decide(DictionaryChoice::Yes)); CHECK(user.contains("CAX"));
    CHECK(round.candidate()->entry.word == "XAC"); CHECK(round.score() == 4);
    CHECK(!round.decide(DictionaryChoice::No)); CHECK(!user.contains("XAC")); CHECK(round.score() == 3);
    CHECK(round.words()[2].state == WordState::Declined);
    CHECK(!round.decide(DictionaryChoice::YesAll)); CHECK(user.contains("CATI")); CHECK(!round.candidate());
    CHECK(round.accepted().size() == 3 && round.score() == 3);
    UserDictionary restarted; CHECK(!restarted.load(tmp / "save"));
    ClassicRound again(d, &restarted); CHECK(!again.board(board())); CHECK(!again.start(0));
    CHECK(again.submit("CAX",1) == Submission::Accepted); CHECK(again.score() == 1);
    again.finish(); CHECK(!again.candidate());
    ClassicRound decline(d); CHECK(!decline.board(board())); CHECK(!decline.start(0));
    CHECK(decline.submit("CAX",1) == Submission::UnknownWord);
    CHECK(decline.submit("XAC",2) == Submission::UnknownWord); decline.finish();
    CHECK(bool(decline.decide(DictionaryChoice::YesAll))); // absent save: no decisions consumed
    CHECK(decline.candidate() && decline.score() == 2);
    CHECK(!decline.decide(DictionaryChoice::NoAll)); CHECK(decline.score() == 0 && !decline.candidate());
    CHECK(decline.words()[0].state == WordState::Declined && decline.words()[1].state == WordState::Declined);
    CHECK(!decline.start(100)); CHECK(decline.words().empty());
}

void master(const std::filesystem::path& tmp) {
    auto d = dictionary(); UserDictionary user; CHECK(!user.load(tmp / "save")); CHECK(!user.add({"CAT"}));
    Board b; std::vector<std::string> faces(25, "X");
    faces[20] = "C"; faces[21] = "A"; faces[22] = "T"; faces[23] = "S"; faces[24] = "Qu";
    faces[19] = "I"; faces[18] = "T"; faces[16] = "A";
    CHECK(!b.set(5, faces)); CHECK(!b.adjacent(4,5)); CHECK(b.adjacent(18,24));
    CHECK(!b.matches("QUQU",{24,24}));
    ClassicRound round(d,&user); CHECK(!round.board(b)); CHECK(round.minimum() == 4);
    CHECK(!round.start(0)); CHECK(round.submit("CAT",1) == Submission::TooShort);
    CHECK(round.submit("CAX",1) == Submission::TooShort); CHECK(round.words().empty());
    CHECK(round.submit("CATS",2,{20,21,22,23}) == Submission::Accepted); CHECK(round.score() == 1);
    CHECK(round.submit("QUIT",3,{24,19,18}) == Submission::Accepted); CHECK(round.score() == 2);
    CHECK(round.submit("QUITS",4,{24,19,18,23}) == Submission::Accepted); CHECK(round.score() == 4);
    CHECK(round.submit("CAAX",5,{20,21,16,11}) == Submission::UnknownWord); CHECK(round.score() == 5);
    round.finish(); CHECK(!round.decide(DictionaryChoice::YesAll)); CHECK(user.contains("CAAX"));
    CHECK(!round.start(100)); CHECK(round.submit("CAAX",101) == Submission::Accepted);
    round.finish();
    for (uint64_t seconds = 60; seconds <= 300; seconds += 30) {
        const uint64_t ms = seconds * 1000;
        CHECK(!round.board(b,ms)); CHECK(round.remaining(0) == ms && round.timed());
        CHECK(!round.start(1000,ms)); round.tick(1000+ms-1); CHECK(round.phase() == RoundPhase::Playing);
        CHECK(round.submit("CATS",1000+ms) == Submission::NotPlaying);
        CHECK(round.score() == 0 && round.remaining(1000+ms) == 0);
    }
    CHECK(!round.board(b,0)); CHECK(!round.timed() && round.remaining(0) == 0);
    CHECK(!round.start(1000,0)); round.tick(UINT64_MAX);
    CHECK(round.phase() == RoundPhase::Playing); CHECK(round.submit("CATS",UINT64_MAX) == Submission::Accepted);
    CHECK(round.score() == 1);
    CHECK(round.submit("CAAXX",UINT64_MAX,{20,21,16,11,6}) == Submission::UnknownWord);
    CHECK(round.score() == 3 && !round.candidate()); round.finish();
    CHECK(round.phase() == RoundPhase::Results && round.candidate());
    CHECK(!round.decide(DictionaryChoice::NoAll)); CHECK(round.score() == 1);
    CHECK(bool(round.start(0,3600001))); CHECK(bool(round.board(b,3600001)));
    CHECK(!round.board(board())); CHECK(round.minimum() == 3 && round.timed());
    CHECK(!round.start(0)); CHECK(round.submit("CAT",1) == Submission::Accepted);
}

void scoringAndGeneration() {
    for (size_t i = 0; i <= 16; ++i) {
        const int expected = i < 3 ? 0 : i < 5 ? 1 : i == 5 ? 2 : i == 6 ? 3 : i == 7 ? 5 : 11;
        CHECK(classicPoints(i) == expected);
    }
    CHECK(classicPoints(3,4) == 0); CHECK(classicPoints(4,4) == 1);
    DiceSet dice; dice.side = 4;
    for (int i = 0; i < 16; ++i) {
        std::array<std::string,6> die; die.fill(std::string(1,char('A'+i))); dice.dice.push_back(die);
    }
    Board first,second,third;
    CHECK(!Board::generate(dice,0x1997,first)); CHECK(!Board::generate(dice,0x1997,second));
    CHECK(first.faces() == second.faces()); CHECK(!Board::generate(dice,0x1998,third));
    CHECK(first.faces() != third.faces()); auto sorted = first.faces(); std::sort(sorted.begin(), sorted.end());
    for (size_t i=0;i<16;++i) CHECK(sorted[i] == std::string(1,char('A'+i)));
    std::string normalized; CHECK(englishWord("QuIt",normalized) && normalized == "QUIT");
    CHECK(!englishWord("\xc3\xa9",normalized)); CHECK(normalized.empty());
    CHECK(bool(first.set(6,{}))); CHECK(first.faces().empty());
}
}
static const test::Suite suite("boggle.unit.classic", {
    {"incremental keyboard paths, ambiguity, rejected cubes, bounds and Qu", [](const auto&) { boardKeyboard(); }},
    {"keyboard Qu, explicit U, whole-cube erasure and atomic bounds", [](const auto&) { keyboard(); }},
    {"shared approvals resolve later reviews without restoring declined words", [](const auto& tmp) { sharedReview(tmp); }},
    {"adjacency, multi-letter tiles, paths and visit limits", [](const auto&) { paths(); }},
    {"submission, duplicates, timeout boundary and replay", [](const auto&) { rounds(); }},
    {"pending words, all four decisions and persistence across rounds", [](const auto& tmp) { review(tmp); }},
    {"Master minimum, last cube, Qu, user words and all timer modes", [](const auto& tmp) { master(tmp); }},
    {"verified scoring and deterministic dice generation", [](const auto&) { scoringAndGeneration(); }},
});
