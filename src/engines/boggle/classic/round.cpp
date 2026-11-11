// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/user_dictionary.h"
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <unordered_set>

namespace reluva::boggle {
bool englishWord(std::string_view input, std::string& out) {
    out.clear();
    if (input.empty() || input.size() > 64) return false;
    for (char c : input) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') { out.clear(); return false; }
        out.push_back(c);
    }
    return true;
}
bool appendEnglishCubes(std::string_view text,std::string& input,bool& followingU) {
    std::string normalized;if(!englishWord(text,normalized))return false;
    auto next=input;bool skip=followingU;
    for(char c:normalized) {
        const bool consume=skip && c=='U';skip=false;
        if(consume)continue;
        if(c=='Q'){next+="QU";skip=true;}else next+=c;
        if(next.size()>64)return false;
    }
    input=std::move(next);followingU=skip;return true;
}
void eraseEnglishCube(std::string& input) {
    if(input.ends_with("QU"))input.resize(input.size()-2);
    else if(!input.empty())input.pop_back();
}
int classicPoints(size_t length, size_t minimum) {
    if (length < minimum || length < 3) return 0;
    if (length <= 4) return 1;
    if (length == 5) return 2;
    if (length == 6) return 3;
    if (length == 7) return 5;
    return 11;
}
Error Board::set(size_t side, const std::vector<std::string>& faces) {
    side_ = 0; faces_.clear();
    if (!side || side > 5 || faces.size() != side * side) return Error::fail("Invalid square board");
    std::vector<std::string> result;
    for (const auto& face : faces) {
        std::string normalized;
        if (face.size() > 8 || !englishWord(face, normalized)) return Error::fail("Board needs U.S. English letter faces");
        result.push_back(std::move(normalized));
    }
    faces_ = std::move(result); side_ = side;
    return Error::success();
}
Error Board::generate(const DiceSet& dice, uint32_t seed, Board& out) {
    out = {};
    if ((dice.side != 4 && dice.side != 5) || dice.dice.size() != dice.side * dice.side)
        return Error::fail("Invalid dice set for a Classic board");
    // Independent deterministic LCG, with rejection sampling for bounded draws.
    // The original game's generator is intentionally not being emulated.
    auto bounded = [&](uint32_t bound) {
        const uint32_t threshold = (0u - bound) % bound;
        uint32_t n;
        do { seed = seed * 1664525u + 1013904223u; n = seed; } while (n < threshold);
        return n % bound;
    };
    std::vector<size_t> order(dice.dice.size()); std::iota(order.begin(), order.end(), size_t(0));
    for (size_t n = order.size(); n > 1; --n) std::swap(order[n - 1], order[bounded(uint32_t(n))]);
    std::vector<std::string> faces;
    for (size_t i : order) faces.push_back(dice.dice[i][bounded(6)]);
    return out.set(dice.side, faces);
}
bool Board::adjacent(size_t a, size_t b) const {
    if (a >= faces_.size() || b >= faces_.size() || a == b) return false;
    const auto ax = int(a % side_), ay = int(a / side_), bx = int(b % side_), by = int(b / side_);
    return std::abs(ax - bx) <= 1 && std::abs(ay - by) <= 1;
}
bool Board::matches(std::string_view word, const std::vector<size_t>& path) const {
    uint32_t used = 0; size_t at = 0;
    if (path.empty()) return false;
    for (size_t i = 0; i < path.size(); ++i) {
        const size_t cell = path[i];
        if (cell >= faces_.size() || (used & (1u << cell)) || (i && !adjacent(path[i - 1], cell)) ||
            !word.substr(at).starts_with(faces_[cell])) return false;
        at += faces_[cell].size(); used |= 1u << cell;
    }
    return at == word.size();
}
PathResult Board::find(std::string_view word, std::vector<size_t>& path, size_t limit) const {
    path.clear();
    if (word.empty() || word.size() > 64 || faces_.empty()) return PathResult::Missing;
    size_t visits = 0;
    bool exhausted = false;
    std::unordered_set<uint64_t> failed;
    auto visit = [&](auto&& self, size_t cell, size_t at, uint32_t used) -> bool {
        if (visits++ >= limit) { exhausted = true; return false; }
        if ((used & (1u << cell)) || !word.substr(at).starts_with(faces_[cell])) return false;
        at += faces_[cell].size(); used |= 1u << cell;
        const uint64_t key = (uint64_t(used) << 5) | cell;
        if (failed.contains(key)) return false;
        path.push_back(cell);
        if (at == word.size()) return true;
        for (size_t next = 0; next < faces_.size(); ++next)
            if (adjacent(cell, next) && self(self, next, at, used)) return true;
            else if (exhausted) break;
        path.pop_back(); failed.insert(key);
        return false;
    };
    for (size_t i = 0; i < faces_.size(); ++i) {
        if (visit(visit, i, 0, 0)) return PathResult::Found;
        if (exhausted) break;
    }
    path.clear();
    return exhausted ? PathResult::Limit : PathResult::Missing;
}
KeyboardResult enterBoardText(const Board& board,std::string_view text,std::string& input,
    std::vector<size_t>& path,bool& followingU,size_t limit) {
    std::string normalized;if(!englishWord(text,normalized))return KeyboardResult::InvalidLetters;
    auto result=KeyboardResult::Entered;
    for(char c:normalized) {
        auto next=input;bool guard=followingU;followingU=false;
        if(!appendEnglishCubes(std::string_view(&c,1),next,guard)){result=KeyboardResult::Limit;continue;}
        if(next==input){followingU=guard;continue;}
        std::vector<size_t> candidate;
        const auto found=board.find(next,candidate,limit);
        if(found==PathResult::Found) {input=std::move(next);path=std::move(candidate);followingU=guard;}
        else result=found==PathResult::Limit?KeyboardResult::Limit:KeyboardResult::NotOnBoard;
    }
    return result;
}
Error ClassicRound::board(Board board, uint64_t duration) {
    if (phase_ == RoundPhase::Playing) return Error::fail("Finish the current round before replacing its board");
    if (candidate()) return Error::fail("Resolve pending words before replacing the board");
    if (board.faces().empty() || duration > 3600000) return Error::fail("A board and valid duration are required");
    duration_ = duration;
    board_ = std::move(board); phase_ = RoundPhase::Ready; score_ = 0; accepted_.clear(); words_.clear();
    return Error::success();
}
Error ClassicRound::start(uint64_t now, uint64_t duration) {
    if (phase_ == RoundPhase::Playing) return Error::fail("Round is already running");
    if (candidate()) return Error::fail("Resolve pending words before replaying");
    if (board_.faces().empty() || dictionary_.words().empty() || duration > 3600000)
        return Error::fail("A board, dictionary and valid duration are required");
    started_ = now; duration_ = duration; phase_ = RoundPhase::Playing; score_ = 0; accepted_.clear(); words_.clear();
    return Error::success();
}
uint64_t ClassicRound::remaining(uint64_t now) const {
    if (phase_ == RoundPhase::Results || !timed()) return 0;
    if (phase_ == RoundPhase::Ready) return duration_;
    const uint64_t elapsed = now >= started_ ? now - started_ : 0;
    return elapsed >= duration_ ? 0 : duration_ - elapsed;
}
void ClassicRound::tick(uint64_t now) {
    if (phase_ == RoundPhase::Playing && timed() && !remaining(now)) phase_ = RoundPhase::Results;
}
Submission ClassicRound::submit(std::string_view input, uint64_t now, const std::vector<size_t>& selected) {
    tick(now);
    if (phase_ != RoundPhase::Playing) return Submission::NotPlaying;
    std::string word;
    if (!englishWord(input, word)) return Submission::InvalidLetters;
    if (word.size() < minimum()) return Submission::TooShort;
    for (const auto& prior : words_) if (prior.entry.word == word) return Submission::Duplicate;
    std::vector<size_t> path;
    if (!selected.empty()) {
        if (!board_.matches(word, selected)) return Submission::NotOnBoard;
        path = selected;
    } else {
        const auto result = board_.find(word, path);
        if (result == PathResult::Limit) return Submission::SelectPath;
        if (result == PathResult::Missing) return Submission::NotOnBoard;
    }
    const int points = classicPoints(word.size(), minimum());
    const bool known = dictionary_.contains(word) || (user_ && user_->contains(word));
    AcceptedWord entry{std::move(word), points, std::move(path)};
    score_ += points;
    if (known) accepted_.push_back(entry);
    words_.push_back({std::move(entry), known ? WordState::Accepted : WordState::Pending});
    return known ? Submission::Accepted : Submission::UnknownWord;
}
const RoundWord* ClassicRound::candidate() const {
    if (phase_ != RoundPhase::Results) return nullptr;
    for (const auto& word : words_) if (word.state == WordState::Pending) return &word;
    return nullptr;
}
void ClassicRound::acceptKnownPending() {
    if(phase_!=RoundPhase::Results)return;
    for(auto& word:words_)if(word.state==WordState::Pending &&
        (dictionary_.contains(word.entry.word) || (user_ && user_->contains(word.entry.word)))) {
        word.state=WordState::Added;accepted_.push_back(word.entry);
    }
}
Error ClassicRound::decide(DictionaryChoice choice) {
    if (!candidate()) return Error::fail("No word is awaiting a dictionary decision");
    const bool yes = choice == DictionaryChoice::Yes || choice == DictionaryChoice::YesAll;
    const bool all = choice == DictionaryChoice::YesAll || choice == DictionaryChoice::NoAll;
    std::vector<size_t> indexes; std::vector<std::string> additions;
    for (size_t i = 0; i < words_.size(); ++i) if (words_[i].state == WordState::Pending) {
        indexes.push_back(i); additions.push_back(words_[i].entry.word);
        if (!all) break;
    }
    if (yes) {
        if (!user_) return Error::fail("No save directory is available for user words. Choose No to continue.");
        if (auto err = user_->add(additions)) return err;
    }
    for (size_t i : indexes) {
        auto& word = words_[i];
        word.state = yes ? WordState::Added : WordState::Declined;
        if (yes) accepted_.push_back(word.entry);
        else score_ -= word.entry.points;
    }
    return Error::success();
}
} // namespace reluva::boggle
