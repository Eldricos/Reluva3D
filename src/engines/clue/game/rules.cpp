// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game/rules.h"

#include <algorithm>
#include <functional>
#include <istream>
#include <ostream>
#include <set>

namespace reluva::clue {

namespace {

// Shuffles with the rng's own numbers, so that a seed deals the same cards
// whatever the standard library (std::shuffle's draws are its own).
template<typename T>
void shuffle(std::vector<T> &v, Rng &rng) {
    for (size_t i = v.size(); i > 1; --i)
        std::swap(v[i - 1], v[rng() % i]);
}

} // namespace

Error Game::start(const Board &board, const std::array<bool, kSuspectCount> &playing, bool noDieRoll, uint32_t seed) {
    if (std::count(playing.begin(), playing.end(), true) < 3)
        return Error::fail("Clue needs three players");
    _board = &board;
    _noDieRoll = noDieRoll;
    _rng.seed(seed);
    _seats = {};
    for (int s = 0; s < kSuspectCount; ++s) {
        _seats[size_t(s)].playing = playing[size_t(s)];
        _places[size_t(s)] = {board.start(s), kNoRoom};
    }
    _broughtIn.fill(false);
    _suggestedIn.fill(kNoRoom);
    _winner = -1;

    // One of each kind into the case file; the rest dealt round the table
    // from the first player, so some may have a card fewer.
    _solution = {int(_rng() % kSuspectCount), int(_rng() % kWeaponCount), int(_rng() % kRoomCount)};
    std::vector<int> deck;
    for (int c = 0; c < kCardCount; ++c)
        if (!_solution.has(c))
            deck.push_back(c);
    shuffle(deck, _rng);
    int s = 0;
    for (int card : deck) {
        while (!_seats[size_t(s)].playing)
            s = (s + 1) % kSuspectCount;
        _seats[size_t(s)].hand.push_back(card);
        s = (s + 1) % kSuspectCount;
    }
    for (Seat &seat : _seats)
        std::sort(seat.hand.begin(), seat.hand.end());

    // Miss Scarlet plays first, or the first suspect played after her.
    _turn = 0;
    while (!_seats[size_t(_turn)].playing)
        ++_turn;
    beginTurn();
    return Error::success();
}

void Game::beginTurn() {
    _phase = Phase::kStart;
    _roll = 0;
    _moves = _noDieRoll ? kMovesPerTurn : 0;
    _startRoom = _places[size_t(_turn)].room;
    _enteredRoom = false;
    _suggestedThisTurn = false;
    _disprover = _shown = -1;
    _passed.clear();
}

bool Game::occupied(const Square &s, int except) const {
    for (int k = 0; k < kSuspectCount; ++k)
        if (k != except && !_places[size_t(k)].inRoom() && _places[size_t(k)].square == s)
            return true;
    return false;
}

bool Game::trapped() const {
    const Place &p = _places[size_t(_turn)];
    if (!p.inRoom() || _board->passage(p.room) != kNoRoom)
        return false;
    for (const Board::Door &d : _board->doors())
        if (d.room == p.room && !occupied(d.outside, _turn))
            return false;
    return true;
}

bool Game::canMove() const {
    return _phase == Phase::kMoving || (_noDieRoll && _phase == Phase::kStart && _moves >= 1 && !trapped());
}

bool Game::canRoll() const {
    return !_noDieRoll && _phase == Phase::kStart && !trapped();
}

bool Game::canTakePassage() const {
    const Place &p = _places[size_t(_turn)];
    return _phase == Phase::kStart && p.inRoom() && _board->passage(p.room) != kNoRoom &&
           (!_noDieRoll || _moves >= kActionCost);
}

bool Game::canSuggest() const {
    const Place &p = _places[size_t(_turn)];
    if (!p.inRoom() || _suggestedThisTurn || (_noDieRoll && _moves < kActionCost))
        return false;
    if (_phase == Phase::kStart)
        return _broughtIn[size_t(_turn)];
    return _phase == Phase::kMoved && _enteredRoom;
}

bool Game::canAccuse() const {
    return (_phase == Phase::kStart || _phase == Phase::kMoved || _phase == Phase::kSuggested) &&
           (!_noDieRoll || _moves >= kActionCost);
}

bool Game::canEndTurn() const {
    if (_phase == Phase::kMoved || _phase == Phase::kSuggested)
        return true;
    // Nothing else to do: trapped, or no move the die allows.
    if (_phase == Phase::kStart)
        return trapped() || (_noDieRoll && destinations().empty() && !canTakePassage());
    return _phase == Phase::kMoving && destinations().empty();
}

int Game::rollDie() {
    if (!canRoll())
        return 0;
    _roll = int(_rng() % 6) + 1;
    _phase = Phase::kMoving;
    return _roll;
}

int Game::stepLimit() const {
    return _noDieRoll ? _moves : _roll;
}

bool Game::stepAllowed(const std::vector<Place> &path, const Place &next) const {
    if (int(path.size()) >= stepLimit())
        return false;
    if (!path.empty() && path.back().inRoom())
        return false; // a room ends the move
    if (next.inRoom())
        return next.room != _startRoom;
    if (occupied(next.square, _turn))
        return false;
    for (const Place &p : path)
        if (!p.inRoom() && p.square == next.square)
            return false;
    return _places[size_t(_turn)].inRoom() || _places[size_t(_turn)].square != next.square;
}

std::vector<Place> Game::nextSteps(const std::vector<Place> &path) const {
    std::vector<Place> out;
    if (!canMove())
        return out;
    const Place &from = path.empty() ? _places[size_t(_turn)] : path.back();
    for (const Place &n : _board->neighbours(from))
        if (stepAllowed(path, n))
            out.push_back(n);
    return out;
}

bool Game::canWalk(const std::vector<Place> &path) const {
    if (!canMove() || path.empty())
        return false;
    std::vector<Place> sofar;
    for (const Place &p : path) {
        const auto next = nextSteps(sofar);
        if (std::find(next.begin(), next.end(), p) == next.end())
            return false;
        sofar.push_back(p);
    }
    // The die's number exactly, unless a room is reached first; without
    // the die, any number of the moves left.
    return _noDieRoll || path.back().inRoom() || int(path.size()) == _roll;
}

std::vector<std::vector<Place>> Game::destinations() const {
    std::vector<std::vector<Place>> found;
    if (!canMove())
        return found;
    const Place from = _places[size_t(_turn)];
    const int limit = stepLimit();
    const auto allowed = [&](const Place &n, const std::set<Square> &visited) {
        if (n.inRoom())
            return n.room != _startRoom;
        return !occupied(n.square, _turn) && !visited.count(n.square) && (from.inRoom() || n.square != from.square);
    };
    if (_noDieRoll) {
        // Any number of moves: the shortest way to each place, breadth first.
        std::vector<std::vector<Place>> frontier{{}};
        std::set<Square> seen;
        std::set<int> rooms;
        for (int depth = 0; depth < limit && !frontier.empty(); ++depth) {
            std::vector<std::vector<Place>> next;
            for (const auto &path : frontier)
                for (const Place &n : _board->neighbours(path.empty() ? from : path.back())) {
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
        for (auto &f : found)
            if (f.back() == path.back()) {
                if (path.size() < f.size())
                    f = path;
                return;
            }
        found.push_back(path);
    };
    std::function<void()> visit = [&]() {
        for (const Place &n : _board->neighbours(path.empty() ? from : path.back())) {
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

Error Game::walk(const std::vector<Place> &path) {
    if (!canWalk(path))
        return Error::fail("not a move the rules allow");
    if (_noDieRoll)
        _moves -= int(path.size());
    Place &at = _places[size_t(_turn)];
    if (at.inRoom())
        _suggestedIn[size_t(_turn)] = kNoRoom; // left it
    at = path.back();
    _enteredRoom = at.inRoom();
    _phase = Phase::kMoved;
    return Error::success();
}

Error Game::takePassage() {
    if (!canTakePassage())
        return Error::fail("no secret passage to take");
    if (_noDieRoll)
        _moves -= kActionCost;
    Place &at = _places[size_t(_turn)];
    _suggestedIn[size_t(_turn)] = kNoRoom;
    at = {{}, _board->passage(at.room)};
    _enteredRoom = true;
    _phase = Phase::kMoved;
    return Error::success();
}

Error Game::suggest(int suspect, int weapon) {
    if (!canSuggest())
        return Error::fail("no suggestion can be made now");
    if (suspect < 0 || suspect >= kSuspectCount || weapon < 0 || weapon >= kWeaponCount)
        return Error::fail("no such suspect or weapon");
    if (_noDieRoll)
        _moves -= kActionCost;
    const int room = _places[size_t(_turn)].room;
    _suggestion = {suspect, weapon, room};
    _suggestedThisTurn = true;
    _suggestedIn[size_t(_turn)] = room;
    // The suspect named is brought to the room.
    if (suspect != _turn) {
        if (_places[size_t(suspect)].room != room)
            _suggestedIn[size_t(suspect)] = kNoRoom;
        _places[size_t(suspect)] = {{}, room};
        _broughtIn[size_t(suspect)] = true;
    }
    // Asked in turn from the player's left: the first who can, shows a card.
    _passed.clear();
    _disprover = _shown = -1;
    for (int k = 1; k < kSuspectCount; ++k) {
        const int s = (_turn + k) % kSuspectCount;
        if (!_seats[size_t(s)].playing)
            continue;
        const auto &hand = _seats[size_t(s)].hand;
        if (std::any_of(hand.begin(), hand.end(), [&](int c) { return _suggestion.has(c); })) {
            _disprover = s;
            break;
        }
        _passed.push_back(s);
    }
    _phase = _disprover >= 0 ? Phase::kDisprove : Phase::kSuggested;
    return Error::success();
}

std::vector<int> Game::disproving() const {
    std::vector<int> out;
    if (_phase != Phase::kDisprove)
        return out;
    for (int c : _seats[size_t(_disprover)].hand)
        if (_suggestion.has(c))
            out.push_back(c);
    return out;
}

Error Game::show(int card) {
    const auto cards = disproving();
    if (std::find(cards.begin(), cards.end(), card) == cards.end())
        return Error::fail("not a card that disproves the suggestion");
    _shown = card;
    _phase = Phase::kSuggested;
    return Error::success();
}

Error Game::accuse(const Accusation &a) {
    if (!canAccuse())
        return Error::fail("no accusation can be made now");
    if (_noDieRoll)
        _moves -= kActionCost;
    if (a == _solution) {
        _winner = _turn;
        _phase = Phase::kOver;
        return Error::success();
    }
    _seats[size_t(_turn)].out = true;
    // Off a doorway, into its room, so as not to block it.
    Place &at = _places[size_t(_turn)];
    if (!at.inRoom())
        for (const Board::Door &d : _board->doors())
            if (d.outside == at.square) {
                at = {{}, d.room};
                break;
            }
    if (std::none_of(_seats.begin(), _seats.end(), [](const Seat &s) { return s.playing && !s.out; })) {
        _phase = Phase::kOver;
        return Error::success();
    }
    _phase = Phase::kSuggested; // nothing left but to end the turn
    return endTurn();
}

Error Game::endTurn() {
    if (_phase != Phase::kSuggested && _phase != Phase::kMoved && !canEndTurn())
        return Error::fail("the turn can't end yet");
    _broughtIn[size_t(_turn)] = false;
    do
        _turn = (_turn + 1) % kSuspectCount;
    while (!_seats[size_t(_turn)].playing || _seats[size_t(_turn)].out);
    beginTurn();
    return Error::success();
}

Square Game::standingSquare(int suspect) const {
    const Place &p = _places[size_t(suspect)];
    if (!p.inRoom())
        return p.square;
    // The k-th suspect in the room (in the game's order) on its k-th square.
    size_t k = 0;
    for (int s = 0; s < suspect; ++s)
        k += _places[size_t(s)].inRoom() && _places[size_t(s)].room == p.room;
    const auto &squares = _board->standing(p.room);
    return squares.empty() ? Square{} : squares[std::min(k, squares.size() - 1)];
}

namespace saveio {

void putString(std::ostream &out, const std::string &s) {
    out << s.size() << ' ' << s << ' ';
}

bool getString(std::istream &in, std::string &s) {
    size_t n = 0;
    if (!(in >> n) || n > 4096 || in.get() != ' ')
        return false;
    s.assign(n, '\0');
    return bool(in.read(s.data(), std::streamsize(n)));
}

} // namespace saveio

void Rng::save(std::ostream &out) const {
    out << _seed << ' ' << _draws;
}

bool Rng::load(std::istream &in) {
    uint32_t s = 0;
    uint64_t n = 0;
    // A game draws a few hundred numbers; far more is a damaged file.
    if (!(in >> s >> n) || n > 100000000)
        return false;
    seed(s);
    _gen.discard(n);
    _draws = n;
    return true;
}

void Game::save(std::ostream &out) const {
    out << "game 2\n";
    for (const Seat &seat : _seats) {
        out << seat.playing << ' ' << seat.out << ' ' << seat.hand.size();
        for (int c : seat.hand)
            out << ' ' << c;
        out << '\n';
    }
    for (int s = 0; s < kSuspectCount; ++s)
        out << _places[size_t(s)].square.x << ' ' << _places[size_t(s)].square.y << ' ' << _places[size_t(s)].room
            << ' ' << _broughtIn[size_t(s)] << ' ' << _suggestedIn[size_t(s)] << '\n';
    out << _solution.suspect << ' ' << _solution.weapon << ' ' << _solution.room << ' ' << _noDieRoll << ' ' << _turn
        << ' ' << int(_phase) << ' ' << _roll << ' ' << _moves << ' ' << _startRoom << ' ' << _enteredRoom << ' '
        << _suggestedThisTurn << ' ' << _suggestion.suspect << ' ' << _suggestion.weapon << ' ' << _suggestion.room
        << ' ' << _disprover << ' ' << _shown << ' ' << _winner << ' ' << _passed.size();
    for (int p : _passed)
        out << ' ' << p;
    out << '\n';
    _rng.save(out);
    out << '\n';
}

Error Game::load(std::istream &in, const Board &board) {
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "game" || version != 2)
        return Error::fail("not a saved game of this kind");
    const auto valid = [](int v, int lo, int hi) { return v >= lo && v < hi; };
    for (Seat &seat : _seats) {
        size_t n = 0;
        if (!(in >> seat.playing >> seat.out >> n) || n > size_t(kCardCount))
            return Error::fail("a saved game's seats are damaged");
        seat.hand.assign(n, 0);
        for (int &c : seat.hand)
            if (!(in >> c) || !valid(c, 0, kCardCount))
                return Error::fail("a saved game's cards are damaged");
    }
    for (int s = 0; s < kSuspectCount; ++s) {
        Place &p = _places[size_t(s)];
        bool brought = false;
        if (!(in >> p.square.x >> p.square.y >> p.room >> brought >> _suggestedIn[size_t(s)]) ||
            !valid(p.room, kNoRoom, kRoomCount))
            return Error::fail("a saved game's places are damaged");
        _broughtIn[size_t(s)] = brought;
    }
    int phase = 0;
    size_t passed = 0;
    if (!(in >> _solution.suspect >> _solution.weapon >> _solution.room >> _noDieRoll >> _turn >> phase >> _roll >>
          _moves >> _startRoom >> _enteredRoom >> _suggestedThisTurn >> _suggestion.suspect >> _suggestion.weapon >>
          _suggestion.room >> _disprover >> _shown >> _winner >> passed) ||
        !valid(_turn, 0, kSuspectCount) || !valid(phase, 0, int(Phase::kOver) + 1) || passed > size_t(kSuspectCount) ||
        !valid(_solution.suspect, 0, kSuspectCount) || !valid(_solution.weapon, 0, kWeaponCount) ||
        !valid(_solution.room, 0, kRoomCount))
        return Error::fail("a saved game's turn is damaged");
    _phase = Phase(phase);
    _passed.assign(passed, 0);
    for (int &p : _passed)
        if (!(in >> p))
            return Error::fail("a saved game's turn is damaged");
    if (!_rng.load(in))
        return Error::fail("a saved game's die is damaged");
    _board = &board;
    return Error::success();
}

} // namespace reluva::clue
