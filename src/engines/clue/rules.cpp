// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/rules.h"

#include <algorithm>
#include <functional>
#include <istream>
#include <ostream>
#include <set>

namespace reluva::clue {

namespace {

// Shuffles with the rng's own numbers, so that a seed deals the same cards
// whatever the standard library (std::shuffle's draws are its own).
template <typename T>
void shuffle(std::vector<T>& v, std::mt19937& rng) {
    for (size_t i = v.size(); i > 1; --i)
        std::swap(v[i - 1], v[rng() % i]);
}

} // namespace

Error Game::start(const Board& board, const std::array<bool, kSuspectCount>& playing, bool noDieRoll, uint32_t seed) {
    if (std::count(playing.begin(), playing.end(), true) < 3)
        return Error::fail("Clue needs three players");
    board_ = &board;
    noDieRoll_ = noDieRoll;
    rng_.seed(seed);
    seats_ = {};
    for (int s = 0; s < kSuspectCount; ++s) {
        seats_[size_t(s)].playing = playing[size_t(s)];
        places_[size_t(s)] = {board.start(s), kNoRoom};
    }
    broughtIn_.fill(false);
    suggestedIn_.fill(kNoRoom);
    winner_ = -1;

    // One of each kind into the case file; the rest dealt round the table
    // from the first player, so some may have a card fewer.
    solution_ = {int(rng_() % kSuspectCount), int(rng_() % kWeaponCount), int(rng_() % kRoomCount)};
    std::vector<int> deck;
    for (int c = 0; c < kCardCount; ++c)
        if (!solution_.has(c))
            deck.push_back(c);
    shuffle(deck, rng_);
    int s = 0;
    for (int card : deck) {
        while (!seats_[size_t(s)].playing)
            s = (s + 1) % kSuspectCount;
        seats_[size_t(s)].hand.push_back(card);
        s = (s + 1) % kSuspectCount;
    }
    for (Seat& seat : seats_)
        std::sort(seat.hand.begin(), seat.hand.end());

    // Miss Scarlet plays first, or the first suspect played after her.
    turn_ = 0;
    while (!seats_[size_t(turn_)].playing)
        ++turn_;
    beginTurn();
    return Error::success();
}

void Game::beginTurn() {
    phase_ = Phase::Start;
    roll_ = 0;
    moves_ = noDieRoll_ ? kMovesPerTurn : 0;
    startRoom_ = places_[size_t(turn_)].room;
    enteredRoom_ = false;
    suggestedThisTurn_ = false;
    disprover_ = shown_ = -1;
    passed_.clear();
}

bool Game::occupied(const Square& s, int except) const {
    for (int k = 0; k < kSuspectCount; ++k)
        if (k != except && !places_[size_t(k)].inRoom() && places_[size_t(k)].square == s)
            return true;
    return false;
}

bool Game::trapped() const {
    const Place& p = places_[size_t(turn_)];
    if (!p.inRoom() || board_->passage(p.room) != kNoRoom)
        return false;
    for (const Board::Door& d : board_->doors())
        if (d.room == p.room && !occupied(d.outside, turn_))
            return false;
    return true;
}

bool Game::canMove() const {
    return phase_ == Phase::Moving || (noDieRoll_ && phase_ == Phase::Start && moves_ >= 1 && !trapped());
}

bool Game::canRoll() const { return !noDieRoll_ && phase_ == Phase::Start && !trapped(); }

bool Game::canTakePassage() const {
    const Place& p = places_[size_t(turn_)];
    return phase_ == Phase::Start && p.inRoom() && board_->passage(p.room) != kNoRoom &&
           (!noDieRoll_ || moves_ >= kActionCost);
}

bool Game::canSuggest() const {
    const Place& p = places_[size_t(turn_)];
    if (!p.inRoom() || suggestedThisTurn_ || (noDieRoll_ && moves_ < kActionCost))
        return false;
    if (phase_ == Phase::Start)
        return broughtIn_[size_t(turn_)];
    return phase_ == Phase::Moved && enteredRoom_;
}

bool Game::canAccuse() const {
    return (phase_ == Phase::Start || phase_ == Phase::Moved || phase_ == Phase::Suggested) &&
           (!noDieRoll_ || moves_ >= kActionCost);
}

bool Game::canEndTurn() const {
    if (phase_ == Phase::Moved || phase_ == Phase::Suggested)
        return true;
    // Nothing else to do: trapped, or no move the die allows.
    if (phase_ == Phase::Start)
        return trapped() || (noDieRoll_ && destinations().empty() && !canTakePassage());
    return phase_ == Phase::Moving && destinations().empty();
}

int Game::rollDie() {
    if (!canRoll())
        return 0;
    roll_ = int(rng_() % 6) + 1;
    phase_ = Phase::Moving;
    return roll_;
}

int Game::stepLimit() const { return noDieRoll_ ? moves_ : roll_; }

bool Game::stepAllowed(const std::vector<Place>& path, const Place& next) const {
    if (int(path.size()) >= stepLimit())
        return false;
    if (!path.empty() && path.back().inRoom())
        return false; // a room ends the move
    if (next.inRoom())
        return next.room != startRoom_;
    if (occupied(next.square, turn_))
        return false;
    for (const Place& p : path)
        if (!p.inRoom() && p.square == next.square)
            return false;
    return places_[size_t(turn_)].inRoom() || places_[size_t(turn_)].square != next.square;
}

std::vector<Place> Game::nextSteps(const std::vector<Place>& path) const {
    std::vector<Place> out;
    if (!canMove())
        return out;
    const Place& from = path.empty() ? places_[size_t(turn_)] : path.back();
    for (const Place& n : board_->neighbours(from))
        if (stepAllowed(path, n))
            out.push_back(n);
    return out;
}

bool Game::canWalk(const std::vector<Place>& path) const {
    if (!canMove() || path.empty())
        return false;
    std::vector<Place> sofar;
    for (const Place& p : path) {
        const auto next = nextSteps(sofar);
        if (std::find(next.begin(), next.end(), p) == next.end())
            return false;
        sofar.push_back(p);
    }
    // The die's number exactly, unless a room is reached first; without
    // the die, any number of the moves left.
    return noDieRoll_ || path.back().inRoom() || int(path.size()) == roll_;
}

std::vector<std::vector<Place>> Game::destinations() const {
    std::vector<std::vector<Place>> found;
    if (!canMove())
        return found;
    const Place from = places_[size_t(turn_)];
    const int limit = stepLimit();
    const auto allowed = [&](const Place& n, const std::set<Square>& visited) {
        if (n.inRoom())
            return n.room != startRoom_;
        return !occupied(n.square, turn_) && !visited.count(n.square) && (from.inRoom() || n.square != from.square);
    };
    if (noDieRoll_) {
        // Any number of moves: the shortest way to each place, breadth first.
        std::vector<std::vector<Place>> frontier{{}};
        std::set<Square> seen;
        std::set<int> rooms;
        for (int depth = 0; depth < limit && !frontier.empty(); ++depth) {
            std::vector<std::vector<Place>> next;
            for (const auto& path : frontier)
                for (const Place& n : board_->neighbours(path.empty() ? from : path.back())) {
                    if (!allowed(n, seen) || (n.inRoom() && rooms.count(n.room)))
                        continue;
                    auto longer = path;
                    longer.push_back(n);
                    found.push_back(longer);
                    if (n.inRoom())
                        rooms.insert(n.room);
                    else {
                        seen.insert(n.square);
                        next.push_back(std::move(longer));
                    }
                }
            frontier = std::move(next);
        }
        return found;
    }
    // The die's number exactly (or a room sooner), no square twice: every
    // such path, depth first; the shortest kept for each place.
    std::vector<Place> path;
    std::set<Square> visited;
    const auto keep = [&]() {
        for (auto& f : found)
            if (f.back() == path.back()) {
                if (path.size() < f.size())
                    f = path;
                return;
            }
        found.push_back(path);
    };
    std::function<void()> visit = [&]() {
        for (const Place& n : board_->neighbours(path.empty() ? from : path.back())) {
            if (!allowed(n, visited))
                continue;
            path.push_back(n);
            if (n.inRoom() || int(path.size()) == limit)
                keep();
            else {
                visited.insert(n.square);
                visit();
                visited.erase(n.square);
            }
            path.pop_back();
        }
    };
    visit();
    return found;
}

Error Game::walk(const std::vector<Place>& path) {
    if (!canWalk(path))
        return Error::fail("not a move the rules allow");
    if (noDieRoll_)
        moves_ -= int(path.size());
    Place& at = places_[size_t(turn_)];
    if (at.inRoom())
        suggestedIn_[size_t(turn_)] = kNoRoom; // left it
    at = path.back();
    enteredRoom_ = at.inRoom();
    phase_ = Phase::Moved;
    return Error::success();
}

Error Game::takePassage() {
    if (!canTakePassage())
        return Error::fail("no secret passage to take");
    if (noDieRoll_)
        moves_ -= kActionCost;
    Place& at = places_[size_t(turn_)];
    suggestedIn_[size_t(turn_)] = kNoRoom;
    at = {{}, board_->passage(at.room)};
    enteredRoom_ = true;
    phase_ = Phase::Moved;
    return Error::success();
}

Error Game::suggest(int suspect, int weapon) {
    if (!canSuggest())
        return Error::fail("no suggestion can be made now");
    if (suspect < 0 || suspect >= kSuspectCount || weapon < 0 || weapon >= kWeaponCount)
        return Error::fail("no such suspect or weapon");
    if (noDieRoll_)
        moves_ -= kActionCost;
    const int room = places_[size_t(turn_)].room;
    suggestion_ = {suspect, weapon, room};
    suggestedThisTurn_ = true;
    suggestedIn_[size_t(turn_)] = room;
    // The suspect named is brought to the room.
    if (suspect != turn_) {
        if (places_[size_t(suspect)].room != room)
            suggestedIn_[size_t(suspect)] = kNoRoom;
        places_[size_t(suspect)] = {{}, room};
        broughtIn_[size_t(suspect)] = true;
    }
    // Asked in turn from the player's left: the first who can, shows a card.
    passed_.clear();
    disprover_ = shown_ = -1;
    for (int k = 1; k < kSuspectCount; ++k) {
        const int s = (turn_ + k) % kSuspectCount;
        if (!seats_[size_t(s)].playing)
            continue;
        const auto& hand = seats_[size_t(s)].hand;
        if (std::any_of(hand.begin(), hand.end(), [&](int c) { return suggestion_.has(c); })) {
            disprover_ = s;
            break;
        }
        passed_.push_back(s);
    }
    phase_ = disprover_ >= 0 ? Phase::Disprove : Phase::Suggested;
    return Error::success();
}

std::vector<int> Game::disproving() const {
    std::vector<int> out;
    if (phase_ != Phase::Disprove)
        return out;
    for (int c : seats_[size_t(disprover_)].hand)
        if (suggestion_.has(c))
            out.push_back(c);
    return out;
}

Error Game::show(int card) {
    const auto cards = disproving();
    if (std::find(cards.begin(), cards.end(), card) == cards.end())
        return Error::fail("not a card that disproves the suggestion");
    shown_ = card;
    phase_ = Phase::Suggested;
    return Error::success();
}

Error Game::accuse(const Accusation& a) {
    if (!canAccuse())
        return Error::fail("no accusation can be made now");
    if (noDieRoll_)
        moves_ -= kActionCost;
    if (a == solution_) {
        winner_ = turn_;
        phase_ = Phase::Over;
        return Error::success();
    }
    seats_[size_t(turn_)].out = true;
    // Off a doorway, into its room, so as not to block it.
    Place& at = places_[size_t(turn_)];
    if (!at.inRoom())
        for (const Board::Door& d : board_->doors())
            if (d.outside == at.square) {
                at = {{}, d.room};
                break;
            }
    if (std::none_of(seats_.begin(), seats_.end(), [](const Seat& s) { return s.playing && !s.out; })) {
        phase_ = Phase::Over;
        return Error::success();
    }
    phase_ = Phase::Suggested; // nothing left but to end the turn
    return endTurn();
}

Error Game::endTurn() {
    if (phase_ != Phase::Suggested && phase_ != Phase::Moved && !canEndTurn())
        return Error::fail("the turn can't end yet");
    broughtIn_[size_t(turn_)] = false;
    do
        turn_ = (turn_ + 1) % kSuspectCount;
    while (!seats_[size_t(turn_)].playing || seats_[size_t(turn_)].out);
    beginTurn();
    return Error::success();
}

Square Game::standingSquare(int suspect) const {
    const Place& p = places_[size_t(suspect)];
    if (!p.inRoom())
        return p.square;
    // The k-th suspect in the room (in the game's order) on its k-th square.
    size_t k = 0;
    for (int s = 0; s < suspect; ++s)
        k += places_[size_t(s)].inRoom() && places_[size_t(s)].room == p.room;
    const auto& squares = board_->standing(p.room);
    return squares.empty() ? Square{} : squares[std::min(k, squares.size() - 1)];
}

namespace saveio {

void putString(std::ostream& out, const std::string& s) { out << s.size() << ' ' << s << ' '; }

bool getString(std::istream& in, std::string& s) {
    size_t n = 0;
    if (!(in >> n) || n > 4096 || in.get() != ' ')
        return false;
    s.assign(n, '\0');
    return bool(in.read(s.data(), std::streamsize(n)));
}

} // namespace saveio

void Game::save(std::ostream& out) const {
    out << "game 1\n";
    for (const Seat& seat : seats_) {
        out << seat.playing << ' ' << seat.out << ' ' << seat.hand.size();
        for (int c : seat.hand)
            out << ' ' << c;
        out << '\n';
    }
    for (int s = 0; s < kSuspectCount; ++s)
        out << places_[size_t(s)].square.x << ' ' << places_[size_t(s)].square.y << ' ' << places_[size_t(s)].room
            << ' ' << broughtIn_[size_t(s)] << ' ' << suggestedIn_[size_t(s)] << '\n';
    out << solution_.suspect << ' ' << solution_.weapon << ' ' << solution_.room << ' ' << noDieRoll_ << ' ' << turn_
        << ' ' << int(phase_) << ' ' << roll_ << ' ' << moves_ << ' ' << startRoom_ << ' ' << enteredRoom_ << ' '
        << suggestedThisTurn_ << ' ' << suggestion_.suspect << ' ' << suggestion_.weapon << ' ' << suggestion_.room
        << ' ' << disprover_ << ' ' << shown_ << ' ' << winner_ << ' ' << passed_.size();
    for (int p : passed_)
        out << ' ' << p;
    out << '\n' << rng_ << '\n';
}

Error Game::load(std::istream& in, const Board& board) {
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "game" || version != 1)
        return Error::fail("not a saved game of this kind");
    const auto valid = [](int v, int lo, int hi) { return v >= lo && v < hi; };
    for (Seat& seat : seats_) {
        size_t n = 0;
        if (!(in >> seat.playing >> seat.out >> n) || n > size_t(kCardCount))
            return Error::fail("a saved game's seats are damaged");
        seat.hand.assign(n, 0);
        for (int& c : seat.hand)
            if (!(in >> c) || !valid(c, 0, kCardCount))
                return Error::fail("a saved game's cards are damaged");
    }
    for (int s = 0; s < kSuspectCount; ++s) {
        Place& p = places_[size_t(s)];
        bool brought = false;
        if (!(in >> p.square.x >> p.square.y >> p.room >> brought >> suggestedIn_[size_t(s)]) ||
            !valid(p.room, kNoRoom, kRoomCount))
            return Error::fail("a saved game's places are damaged");
        broughtIn_[size_t(s)] = brought;
    }
    int phase = 0;
    size_t passed = 0;
    if (!(in >> solution_.suspect >> solution_.weapon >> solution_.room >> noDieRoll_ >> turn_ >> phase >> roll_ >>
          moves_ >> startRoom_ >> enteredRoom_ >> suggestedThisTurn_ >> suggestion_.suspect >> suggestion_.weapon >>
          suggestion_.room >> disprover_ >> shown_ >> winner_ >> passed) ||
        !valid(turn_, 0, kSuspectCount) || !valid(phase, 0, int(Phase::Over) + 1) || passed > size_t(kSuspectCount) ||
        !valid(solution_.suspect, 0, kSuspectCount) || !valid(solution_.weapon, 0, kWeaponCount) ||
        !valid(solution_.room, 0, kRoomCount))
        return Error::fail("a saved game's turn is damaged");
    phase_ = Phase(phase);
    passed_.assign(passed, 0);
    for (int& p : passed_)
        if (!(in >> p))
            return Error::fail("a saved game's turn is damaged");
    if (!(in >> rng_))
        return Error::fail("a saved game's die is damaged");
    board_ = &board;
    return Error::success();
}

} // namespace reluva::clue
