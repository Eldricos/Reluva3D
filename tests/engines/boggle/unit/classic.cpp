// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/round.h"
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
    CHECK(round.submit("NOTAWORD",1400) == Submission::UnknownWord);
    CHECK(round.submit("C A T",1400) == Submission::InvalidLetters);
    CHECK(bool(round.board(b))); CHECK(round.remaining(1999) == 1);
    CHECK(round.submit("CATS",2000) == Submission::NotPlaying);
    CHECK(round.phase() == RoundPhase::Results && round.score() == 4);
    CHECK(!round.start(3000)); CHECK(round.remaining(3000) == 180000 && round.score() == 0);
    CHECK(round.submit("CATS",3000,{0,1,2,3}) == Submission::Accepted);
    round.finish(); CHECK(round.remaining(3000) == 0); CHECK(round.accepted().size() == 1);
    CHECK(!round.board(b)); CHECK(round.phase() == RoundPhase::Ready && round.accepted().empty());
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
    {"adjacency, multi-letter tiles, paths and visit limits", [](const auto&) { paths(); }},
    {"submission, duplicates, timeout boundary and replay", [](const auto&) { rounds(); }},
    {"verified scoring and deterministic dice generation", [](const auto&) { scoringAndGeneration(); }},
});
