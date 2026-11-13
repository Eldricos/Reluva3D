// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game/ai.h"

#include <algorithm>
#include <climits>
#include <deque>
#include <istream>
#include <ostream>

namespace reluva::clue {

namespace {

constexpr int kKindFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
constexpr int kKindCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};

} // namespace

// ---- The notebook --------------------------------------------------------------

void Notebook::reset(const Game &game, int me, bool countCards, bool useClues) {
    _countCards = countCards;
    _useClues = useClues;
    _contradiction = false;
    _oneOf.clear();
    _notAll.clear();
    for (auto &row : _marks)
        row.fill(kUnknown);
    for (int s = 0; s < kSuspectCount; ++s)
        _handSize[size_t(s)] = int(game.seat(s).hand.size());
    _handSize[kCaseFile] = 3;
    for (int s = 0; s < kSuspectCount; ++s)
        if (!game.seat(s).playing)
            for (int c = 0; c < kCardCount; ++c)
                _marks[size_t(c)][size_t(s)] = kNo;
    for (int c = 0; c < kCardCount; ++c)
        _marks[size_t(c)][size_t(me)] = kNo;
    for (int c : game.seat(me).hand)
        _marks[size_t(c)][size_t(me)] = kYes;
    settle();
}

int Notebook::holder(int card) const {
    for (int o = 0; o < kOwners; ++o)
        if (mark(card, o) == kYes)
            return o;
    return -1;
}

int Notebook::solved(int kind) const {
    for (int c = kKindFirst[kind]; c < kKindFirst[kind] + kKindCount[kind]; ++c)
        if (inCaseFile(c))
            return c;
    return -1;
}

int Notebook::unknownCount() const {
    int n = 0;
    for (int c = 0; c < kCardCount; ++c)
        n += holder(c) < 0;
    return n;
}

void Notebook::set(int card, int owner, Mark m) {
    Mark &at = _marks[size_t(card)][size_t(owner)];
    if (at == m)
        return;
    if (at != kUnknown)
        _contradiction = true; // seen wrong (it can't be, but nothing is changed for it)
    else
        at = m;
}

void Notebook::holds(int owner, int card) {
    set(card, owner, kYes);
    settle();
}

void Notebook::lacks(int owner, int card) {
    set(card, owner, kNo);
    settle();
}

void Notebook::holdsOneOf(int owner, const std::array<int, 3> &cards) {
    if (!_useClues)
        return;
    _oneOf.push_back({owner, cards});
    settle();
}

void Notebook::notAll(const std::array<int, 3> &cards) {
    _notAll.push_back(cards);
    settle();
}

void Notebook::settle() {
    for (bool changed = true; changed;) {
        changed = false;
        const auto put = [&](int card, int owner, Mark m) {
            if (mark(card, owner) == kUnknown) {
                _marks[size_t(card)][size_t(owner)] = m;
                changed = true;
            }
        };
        for (int c = 0; c < kCardCount; ++c) {
            // A card is in one place: held by one, lacked by the others;
            // lacked by all others, held by the last.
            const int h = holder(c);
            if (h >= 0)
                for (int o = 0; o < kOwners; ++o)
                    if (o != h)
                        put(c, o, kNo);
            int open = -1;
            int opens = 0;
            for (int o = 0; o < kOwners; ++o)
                if (mark(c, o) != kNo) {
                    open = o;
                    ++opens;
                }
            if (opens == 1)
                put(c, open, kYes);
        }
        // The case file has one card of each kind.
        for (int k = 0; k < 3; ++k) {
            int yes = -1;
            int open = -1;
            int opens = 0;
            for (int c = kKindFirst[k]; c < kKindFirst[k] + kKindCount[k]; ++c) {
                if (mark(c, kCaseFile) == kYes)
                    yes = c;
                if (mark(c, kCaseFile) != kNo) {
                    open = c;
                    ++opens;
                }
            }
            if (yes >= 0)
                for (int c = kKindFirst[k]; c < kKindFirst[k] + kKindCount[k]; ++c)
                    if (c != yes)
                        put(c, kCaseFile, kNo);
            if (opens == 1)
                put(open, kCaseFile, kYes);
        }
        if (_countCards) {
            // So many cards each: all found, the rest lacked; as many left
            // open as are still to find, all held.
            for (int o = 0; o < kCaseFile; ++o) {
                int yes = 0;
                int open = 0;
                for (int c = 0; c < kCardCount; ++c) {
                    yes += mark(c, o) == kYes;
                    open += mark(c, o) == kUnknown;
                }
                if (open == 0)
                    continue;
                if (yes == _handSize[size_t(o)])
                    for (int c = 0; c < kCardCount; ++c)
                        put(c, o, kNo);
                else if (yes + open == _handSize[size_t(o)])
                    for (int c = 0; c < kCardCount; ++c)
                        put(c, o, kYes);
            }
        }
        // A wrong accusation: with two of its cards in the case file, the
        // third isn't.
        for (const auto &cards : _notAll) {
            int in = 0;
            int other = -1;
            for (int c : cards) {
                if (inCaseFile(c))
                    ++in;
                else
                    other = c;
            }
            if (in == 2)
                put(other, kCaseFile, kNo);
        }
        // Who showed a card not seen holds one of the three: when two are
        // lacked, the third.
        for (const auto &[owner, cards] : _oneOf) {
            int open = -1;
            int opens = 0;
            bool held = false;
            for (int c : cards) {
                held = held || mark(c, owner) == kYes;
                if (mark(c, owner) == kUnknown) {
                    open = c;
                    ++opens;
                }
            }
            if (!held && opens == 1)
                put(open, owner, kYes);
        }
    }
}

// ---- The player ---------------------------------------------------------------

namespace {

constexpr uint32_t kSuspects = 0x3f, kWeapons = 0xfc0, kRooms = 0x1ff000, kAll = 0x1fffff;
constexpr uint32_t kKinds[3] = {kSuspects, kWeapons, kRooms};

int bits(uint32_t m) {
    int n = 0;
    for (; m; m &= m - 1)
        ++n;
    return n;
}

int firstBit(uint32_t m) {
    for (int i = 0; i < 32; ++i)
        if (m & (1u << i))
            return i;
    return -1;
}

uint32_t maskOf(const Accusation &a) {
    return (1u << suspectCard(a.suspect)) | (1u << weaponCard(a.weapon)) | (1u << roomCard(a.room));
}

} // namespace

void ComputerPlayer::reset(const Game &game, int me, uint32_t seed, int level) {
    _me = me;
    _level = level;
    _rng.seed(seed);
    _mind = {};
    _target = kNoRoom;
    // Its own hand known, and that it holds nothing else (0x42CC90).
    uint32_t hand = 0;
    for (int c : game.seat(me).hand)
        hand |= 1u << c;
    _mind.has[size_t(me)] = hand;
    _mind.known = hand;
    _mind.hasNot[size_t(me)] = ~hand & kAll;
    measure(game.board());
}

bool ComputerPlayer::takesIn() {
    // What others show each other, and what a wrong accusation tells: an
    // expert takes it in, a normal player half the time (Clue.exe's
    // rand() * 100 / 32767 > 50), a novice never (0x421C10, 0x421C80).
    if (_level == kNovice)
        return false;
    if (_level == kExpert)
        return true;
    return int((_rng() % 32768) * 100 / 32767) > 50;
}

uint32_t ComputerPlayer::pickBit(uint32_t mask) {
    // One of the bits by chance (0x42D050).
    const int n = bits(mask);
    if (n == 0)
        return 0;
    int k = int(_rng() % uint32_t(n));
    for (int i = 0; i < 32; ++i)
        if ((mask & (1u << i)) && k-- == 0)
            return 1u << i;
    return 0;
}

void ComputerPlayer::findSolution() {
    // Of each kind, the one card not known to be held (0x42D0B0).
    const uint32_t open = ~_mind.known;
    for (uint32_t kind : kKinds)
        if (bits(open & kind) == 1)
            _mind.solution |= open & kind;
}

void ComputerPlayer::settle() {
    // Each "holds one of these" pared down by what is known: what its
    // holder hasn't, what others have; held already, or down to one card
    // (then held), it's done with (0x42CCE0).
    bool changed = true;
    while (changed) {
        changed = false;
        for (int p = 0; p < kSuspectCount; ++p) {
            auto &list = _mind.oneOf[size_t(p)];
            for (size_t i = 0; i < list.size();) {
                uint32_t m = list[i] & ~_mind.hasNot[size_t(p)];
                bool held = false;
                for (int j = 0; j < kSuspectCount; ++j) {
                    if (j == p) {
                        if (_mind.has[size_t(p)] & m) {
                            held = true;
                            break;
                        }
                    } else
                        m &= ~_mind.has[size_t(j)];
                }
                list[i] = m;
                if (bits(m) == 1 && !held) {
                    list.erase(list.begin() + std::ptrdiff_t(i));
                    _mind.has[size_t(p)] |= m;
                    _mind.known |= m;
                    findSolution();
                    changed = true;
                    continue;
                }
                if (m == 0 || held) {
                    list.erase(list.begin() + std::ptrdiff_t(i));
                    continue;
                }
                ++i;
            }
        }
    }
    findSolution();
}

bool ComputerPlayer::ready() const {
    return bits(_mind.solution) == 3;
}

void ComputerPlayer::observe(const Game &game) {
    const int suggester = game.turn();
    const int disprover = game.disprover();
    _mind.current = maskOf(game.suggestion());
    // Who couldn't answer holds none of the three (0x418935, all levels).
    for (int p : game.passed())
        if (p != _me) {
            _mind.hasNot[size_t(p)] |= _mind.current;
            settle();
        }
    if (disprover < 0) {
        // Nobody could answer its own suggestion: what of it isn't in its
        // hand is the answer (0x42D1B0, from 0x418804).
        if (_me == suggester)
            _mind.solution |= _mind.current & ~_mind.has[size_t(_me)];
        return;
    }
    // Others: the one who answered holds one of them, as a level takes it
    // in (0x418D00).
    if (_me != disprover && _me != suggester && takesIn()) {
        _mind.oneOf[size_t(disprover)].push_back(_mind.current);
        settle();
    }
    // Its own suggestion: the card shown (0x42CE90).
    if (_me == suggester && game.shown() >= 0) {
        _mind.has[size_t(disprover)] |= 1u << game.shown();
        _mind.known |= 1u << game.shown();
        findSolution();
        settle();
    }
}

void ComputerPlayer::observeAccusation(const Accusation &a, int accuser) {
    // A wrong accusation: Clue.exe takes it that the accuser holds none of
    // the three (0x41B26E, as a level takes it in), which needn't be so.
    if (takesIn()) {
        _mind.hasNot[size_t(accuser)] |= maskOf(a);
        settle();
    }
}

uint32_t ComputerPlayer::fallback(uint32_t kind, const Game &game) {
    (void)game;
    // Nothing of a kind in doubt (0x42D800): one of its own, else one
    // another is known to hold, from the seat before it backwards.
    if (const uint32_t mine = _mind.has[size_t(_me)] & kind)
        return pickBit(mine);
    for (int j = _me - 1; j >= 0; --j)
        if (const uint32_t m = _mind.has[size_t(j)] & kind)
            return pickBit(m);
    for (int j = kSuspectCount - 1; j > _me; --j)
        if (const uint32_t m = _mind.has[size_t(j)] & kind)
            return pickBit(m);
    return pickBit(kind);
}

uint32_t ComputerPlayer::pickCard(uint32_t kind, const Game &game) {
    // A suspect or weapon to name (0x42D8A0): one not known to be held, or
    // the answer; above a novice, not one a "holds one of" is about if it
    // can. By chance among them. (Itself, if it hasn't its own card.)
    uint32_t open = (~_mind.known | _mind.solution) & kind;
    if (!open)
        return fallback(kind, game);
    if (_level != kNovice) {
        uint32_t talked = 0;
        for (const auto &list : _mind.oneOf)
            for (uint32_t m : list)
                talked |= m;
        if (const uint32_t fresh = open & ~talked)
            open = fresh;
    }
    return pickBit(open);
}

int ComputerPlayer::pickRoom(const Game &game) {
    // The room to head for (0x42D900): one neither known held nor found the
    // answer; any in its first three moves; not the one it is in.
    uint32_t open = ~(_mind.solution | _mind.known) & kRooms;
    if (_mind.moves < 3)
        open = kRooms;
    const Place &at = game.place(_me);
    if (at.inRoom() && !game.canSuggest())
        open &= ~(1u << roomCard(at.room));
    const uint32_t bit = open ? pickBit(open) : fallback(kRooms, game);
    return firstBit(bit) - roomCard(0);
}

void ComputerPlayer::measure(const Board &board) {
    // Each room's distances: from the squares before its doors, outwards.
    for (int r = 0; r < kRoomCount; ++r) {
        auto &d = _toRoom[size_t(r)];
        d.clear();
        std::deque<Square> queue;
        for (const Board::Door &door : board.doors())
            if (door.room == r && !d.count(door.outside)) {
                d[door.outside] = 1;
                queue.push_back(door.outside);
            }
        while (!queue.empty()) {
            const Square s = queue.front();
            queue.pop_front();
            for (const Place &n : board.neighbours({s, kNoRoom}))
                if (!n.inRoom() && !d.count(n.square)) {
                    d[n.square] = d[s] + 1;
                    queue.push_back(n.square);
                }
        }
    }
}

int ComputerPlayer::distance(const Place &from, int room) const {
    if (from.inRoom()) {
        if (from.room == room)
            return 0;
        int best = INT_MAX / 2;
        for (const auto &[square, d] : _toRoom[size_t(from.room)])
            if (d == 1) { // a square before one of its doors
                auto it = _toRoom[size_t(room)].find(square);
                if (it != _toRoom[size_t(room)].end())
                    best = std::min(best, it->second + 1);
            }
        return best;
    }
    auto it = _toRoom[size_t(room)].find(from.square);
    return it == _toRoom[size_t(room)].end() ? INT_MAX / 2 : it->second;
}

int ComputerPlayer::closestRoom(const Game &game) const {
    // GetClosestRoom with every room but its own (0x426710): through the
    // passage if it has one, else the nearest by its doors, the first in
    // the rooms' order of those as near.
    const Place &at = game.place(_me);
    if (at.inRoom() && game.board().passage(at.room) != kNoRoom)
        return game.board().passage(at.room);
    int best = kNoRoom;
    int bestD = INT_MAX;
    for (int r = 0; r < kRoomCount; ++r)
        if (!(at.inRoom() && at.room == r)) {
            const int d = distance(at, r);
            if (d < bestD) {
                bestD = d;
                best = r;
            }
        }
    return best;
}

ComputerPlayer::Action ComputerPlayer::decide(const Game &game) {
    // As soon as it has the answer (0x42CE90, 0x422220).
    if (game.canAccuse() && ready())
        return Action::kAccuse;
    // In a room it may suggest in (brought there, or walked in): it does
    // (0x42C740, 0x4227C6).
    if (game.canSuggest())
        return Action::kSuggest;
    if (game.phase() == Game::Phase::kStart) {
        // The room it heads for (0x422570): if it is in it, the nearest
        // other; through the passage when that is the way there.
        _target = pickRoom(game);
        const Place &at = game.place(_me);
        if (at.inRoom() && at.room == _target)
            _target = closestRoom(game);
        if (game.canTakePassage() && game.board().passage(at.room) == _target)
            return Action::kPassage;
        if (game.canRoll())
            return Action::kRoll;
    }
    if (game.canMove() && !game.destinations().empty())
        return Action::kWalk;
    if (!game.canEndTurn() && game.canTakePassage())
        return Action::kPassage; // the only way on
    return Action::kEndTurn;
}

std::vector<Place> ComputerPlayer::chooseWalk(const Game &game) {
    const auto options = game.destinations();
    if (options.empty())
        return {};
    // Along the shortest way to the room it heads for, as far as the die
    // goes (0x42C1F0, 0x42C180); into it if it can be reached. Without the
    // die, into it only with three moves left to suggest with.
    int room = _target == kNoRoom ? closestRoom(game) : _target;
    size_t best = 0;
    int bestD = INT_MAX;
    for (size_t i = 0; i < options.size(); ++i) {
        const Place &end = options[i].back();
        const bool canAfford = !game.noDieRoll() || game.moves() - int(options[i].size()) >= Game::kActionCost;
        int d = distance(end, room);
        if (end.inRoom())
            d = end.room == room && canAfford ? -1 : INT_MAX / 4;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    ++_mind.moves;
    return options[best];
}

Accusation ComputerPlayer::chooseSuggestion(const Game &game) {
    // A suspect and a weapon (0x42D7B0), in the room it is in (0x4227E4).
    const int s = firstBit(pickCard(kSuspects, game)) - suspectCard(0);
    const int w = firstBit(pickCard(kWeapons, game)) - weaponCard(0);
    return {s, w, game.place(_me).room};
}

Accusation ComputerPlayer::chooseAccusation() const {
    return {firstBit(_mind.solution & kSuspects) - suspectCard(0), firstBit(_mind.solution & kWeapons) - weaponCard(0),
            firstBit(_mind.solution & kRooms) - roomCard(0)};
}

int ComputerPlayer::chooseShow(const Game &game, int suggester) {
    // Which of its cards to show (0x42D680): above a novice, one that
    // suggester has been shown before, else one shown to anyone; else any.
    uint32_t cards = 0;
    for (int c : game.disproving())
        cards |= 1u << c;
    if (!cards)
        return -1;
    uint32_t from = cards;
    if (_level != kNovice) {
        if (_mind.shownTo[size_t(suggester)] & cards)
            from = _mind.shownTo[size_t(suggester)] & cards;
        else if (_mind.shownAny & cards)
            from = _mind.shownAny & cards;
    }
    const uint32_t bit = pickBit(from);
    _mind.shownTo[size_t(suggester)] |= bit;
    _mind.shownAny |= bit;
    return firstBit(bit);
}

void Notebook::save(std::ostream &out) const {
    out << "notebook 1 " << _countCards << ' ' << _useClues << '\n';
    for (const auto &row : _marks) {
        for (Mark m : row)
            out << int(m) << ' ';
        out << '\n';
    }
    for (int n : _handSize)
        out << n << ' ';
    out << '\n' << _oneOf.size();
    for (const auto &[owner, cards] : _oneOf)
        out << ' ' << owner << ' ' << cards[0] << ' ' << cards[1] << ' ' << cards[2];
    out << '\n' << _notAll.size();
    for (const auto &cards : _notAll)
        out << ' ' << cards[0] << ' ' << cards[1] << ' ' << cards[2];
    out << '\n';
}

Error Notebook::load(std::istream &in) {
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version >> _countCards >> _useClues) || tag != "notebook" || version != 1)
        return Error::fail("a saved notebook is damaged");
    for (auto &row : _marks)
        for (Mark &m : row) {
            int v = 0;
            if (!(in >> v) || v < kUnknown || v > kNo)
                return Error::fail("a saved notebook is damaged");
            m = Mark(v);
        }
    for (int &n : _handSize)
        if (!(in >> n))
            return Error::fail("a saved notebook is damaged");
    const auto card = [](int c) { return c >= 0 && c < kCardCount; };
    size_t n = 0;
    if (!(in >> n) || n > 1000)
        return Error::fail("a saved notebook is damaged");
    _oneOf.assign(n, {});
    for (auto &[owner, cards] : _oneOf)
        if (!(in >> owner >> cards[0] >> cards[1] >> cards[2]) || owner < 0 || owner >= kOwners || !card(cards[0]) ||
            !card(cards[1]) || !card(cards[2]))
            return Error::fail("a saved notebook is damaged");
    if (!(in >> n) || n > 100)
        return Error::fail("a saved notebook is damaged");
    _notAll.assign(n, {});
    for (auto &cards : _notAll)
        if (!(in >> cards[0] >> cards[1] >> cards[2]) || !card(cards[0]) || !card(cards[1]) || !card(cards[2]))
            return Error::fail("a saved notebook is damaged");
    _contradiction = false;
    return Error::success();
}

void ComputerPlayer::save(std::ostream &out) const {
    out << "player 3 " << _me << ' ' << _level << ' ' << _target << '\n';
    _rng.save(out);
    out << '\n';
    const Mind &m = _mind;
    out << m.current << ' ' << m.known << ' ' << m.solution << ' ' << m.shownAny << ' ' << m.moves << '\n';
    for (int p = 0; p < kSuspectCount; ++p) {
        out << m.has[size_t(p)] << ' ' << m.hasNot[size_t(p)] << ' ' << m.shownTo[size_t(p)] << ' '
            << m.oneOf[size_t(p)].size();
        for (uint32_t x : m.oneOf[size_t(p)])
            out << ' ' << x;
        out << '\n';
    }
}

Error ComputerPlayer::load(std::istream &in, const Game &game) {
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version >> _me >> _level >> _target) || !_rng.load(in) || tag != "player" || version != 3 ||
        _me < 0 || _me >= kSuspectCount || _level < kNovice || _level > kExpert || _target < kNoRoom ||
        _target >= kRoomCount)
        return Error::fail("a saved computer player is damaged");
    Mind &m = _mind;
    m = {};
    if (!(in >> m.current >> m.known >> m.solution >> m.shownAny >> m.moves))
        return Error::fail("a saved computer player is damaged");
    for (int p = 0; p < kSuspectCount; ++p) {
        size_t n = 0;
        if (!(in >> m.has[size_t(p)] >> m.hasNot[size_t(p)] >> m.shownTo[size_t(p)] >> n) || n > 1000)
            return Error::fail("a saved computer player is damaged");
        m.oneOf[size_t(p)].assign(n, 0);
        for (uint32_t &x : m.oneOf[size_t(p)])
            if (!(in >> x))
                return Error::fail("a saved computer player is damaged");
    }
    measure(game.board());
    return Error::success();
}

} // namespace reluva::clue
