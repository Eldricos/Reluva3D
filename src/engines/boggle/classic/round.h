// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/formats/dictionary.h"
#include "engines/boggle/formats/text_resources.h"
#include <string_view>

namespace reluva::boggle {
// U.S. English normalization only; never silently reinterpret UTF-8/high bytes.
bool englishWord(std::string_view input, std::string& out);
// Qu is one keyboard cube. An explicitly typed following U is consumed once.
bool appendEnglishCubes(std::string_view text,std::string& input,bool& followingU);
void eraseEnglishCube(std::string& input);
int classicPoints(size_t length, size_t minimum = 3);
enum class PathResult { Found, Missing, Limit };
class Board {
public:
    Error set(size_t side, const std::vector<std::string>& faces);
    static Error generate(const DiceSet& dice, uint32_t seed, Board& out);
    size_t side() const { return side_; }
    const std::vector<std::string>& faces() const { return faces_; }
    bool adjacent(size_t a, size_t b) const;
    bool matches(std::string_view word, const std::vector<size_t>& path) const;
    PathResult find(std::string_view word, std::vector<size_t>& path, size_t limit = 500000) const;
private:
    size_t side_ = 0;
    std::vector<std::string> faces_;
};
// Each keyboard cube must leave a traceable prefix. Rejected letters are
// skipped; valid letters in the same text event still enter in order.
enum class KeyboardResult { Entered, InvalidLetters, NotOnBoard, Limit };
KeyboardResult enterBoardText(const Board& board,std::string_view text,std::string& input,
    std::vector<size_t>& path,bool& followingU,size_t limit=500000);
enum class RoundPhase { Ready, Playing, Results };
enum class Submission { Accepted, NotPlaying, InvalidLetters, TooShort, UnknownWord, NotOnBoard, Duplicate, SelectPath };
class UserDictionary;
enum class WordState { Accepted, Pending, Added, Declined };
enum class DictionaryChoice { Yes, No, YesAll, NoAll };
struct AcceptedWord { std::string word; int points = 0; std::vector<size_t> path; };
struct RoundWord { AcceptedWord entry; WordState state = WordState::Accepted; };
class ClassicRound {
public:
    explicit ClassicRound(const Dictionary& dictionary, UserDictionary* user = nullptr) : dictionary_(dictionary), user_(user) {}
    Error board(Board board, uint64_t duration = 180000); // only between rounds; clears previous result
    Error start(uint64_t now, uint64_t duration = 180000);
    void tick(uint64_t now);
    void finish() { if (phase_ == RoundPhase::Playing) phase_ = RoundPhase::Results; }
    Submission submit(std::string_view input, uint64_t now, const std::vector<size_t>& selected = {});
    uint64_t remaining(uint64_t now) const;
    RoundPhase phase() const { return phase_; }
    const Board& board() const { return board_; }
    const std::vector<AcceptedWord>& accepted() const { return accepted_; }
    const std::vector<RoundWord>& words() const { return words_; }
    const RoundWord* candidate() const;
    Error decide(DictionaryChoice choice);
    void acceptKnownPending();
    size_t minimum() const { return board_.side() == 5 ? 4 : 3; }
    bool timed() const { return duration_ != 0; }
    int score() const { return score_; }
private:
    const Dictionary& dictionary_;
    UserDictionary* user_ = nullptr;
    std::vector<RoundWord> words_;
    Board board_;
    RoundPhase phase_ = RoundPhase::Ready;
    uint64_t started_ = 0, duration_ = 180000;
    int score_ = 0;
    std::vector<AcceptedWord> accepted_;
};
} // namespace reluva::boggle
