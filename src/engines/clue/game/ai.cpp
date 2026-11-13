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
    countCards_ = countCards;
    useClues_ = useClues;
    contradiction_ = false;
    oneOf_.clear();
    notAll_.clear();
    for (auto &row : marks_)
        row.fill(Unknown);
    for (int s = 0; s < kSuspectCount; ++s)
        handSize_[size_t(s)] = int(game.seat(s).hand.size());
    handSize_[kCaseFile] = 3;
    for (int s = 0; s < kSuspectCount; ++s)
        if (!game.seat(s).playing)
            for (int c = 0; c < kCardCount; ++c)
                marks_[size_t(c)][size_t(s)] = No;
    for (int c = 0; c < kCardCount; ++c)
        marks_[size_t(c)][size_t(me)] = No;
    for (int c : game.seat(me).hand)
        marks_[size_t(c)][size_t(me)] = Yes;
    settle();
}

int Notebook::holder(int card) const {
    for (int o = 0; o < kOwners; ++o)
        if (mark(card, o) == Yes)
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
    Mark &at = marks_[size_t(card)][size_t(owner)];
    if (at == m)
        return;
    if (at != Unknown)
        contradiction_ = true; // seen wrong (it can't be, but nothing is changed for it)
    else
        at = m;
}

void Notebook::holds(int owner, int card) {
    set(card, owner, Yes);
    settle();
}

void Notebook::lacks(int owner, int card) {
    set(card, owner, No);
    settle();
}

void Notebook::holdsOneOf(int owner, const std::array<int, 3> &cards) {
    if (!useClues_)
        return;
    oneOf_.push_back({owner, cards});
    settle();
}

void Notebook::notAll(const std::array<int, 3> &cards) {
    notAll_.push_back(cards);
    settle();
}

void Notebook::settle() {
    for (bool changed = true; changed;) {
        changed = false;
        const auto put = [&](int card, int owner, Mark m) {
            if (mark(card, owner) == Unknown) {
                marks_[size_t(card)][size_t(owner)] = m;
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
                        put(c, o, No);
            int open = -1;
            int opens = 0;
            for (int o = 0; o < kOwners; ++o)
                if (mark(c, o) != No) {
                    open = o;
                    ++opens;
                }
            if (opens == 1)
                put(c, open, Yes);
        }
        // The case file has one card of each kind.
        for (int k = 0; k < 3; ++k) {
            int yes = -1;
            int open = -1;
            int opens = 0;
            for (int c = kKindFirst[k]; c < kKindFirst[k] + kKindCount[k]; ++c) {
                if (mark(c, kCaseFile) == Yes)
                    yes = c;
                if (mark(c, kCaseFile) != No) {
                    open = c;
                    ++opens;
                }
            }
            if (yes >= 0)
                for (int c = kKindFirst[k]; c < kKindFirst[k] + kKindCount[k]; ++c)
                    if (c != yes)
                        put(c, kCaseFile, No);
            if (opens == 1)
                put(open, kCaseFile, Yes);
        }
        if (countCards_) {
            // So many cards each: all found, the rest lacked; as many left
            // open as are still to find, all held.
            for (int o = 0; o < kCaseFile; ++o) {
                int yes = 0;
                int open = 0;
                for (int c = 0; c < kCardCount; ++c) {
                    yes += mark(c, o) == Yes;
                    open += mark(c, o) == Unknown;
                }
                if (open == 0)
                    continue;
                if (yes == handSize_[size_t(o)])
                    for (int c = 0; c < kCardCount; ++c)
                        put(c, o, No);
                else if (yes + open == handSize_[size_t(o)])
                    for (int c = 0; c < kCardCount; ++c)
                        put(c, o, Yes);
            }
        }
        // A wrong accusation: with two of its cards in the case file, the
        // third isn't.
        for (const auto &cards : notAll_) {
            int in = 0;
            int other = -1;
            for (int c : cards) {
                if (inCaseFile(c))
                    ++in;
                else
                    other = c;
            }
            if (in == 2)
                put(other, kCaseFile, No);
        }
        // Who showed a card not seen holds one of the three: when two are
        // lacked, the third.
        for (const auto &[owner, cards] : oneOf_) {
            int open = -1;
            int opens = 0;
            bool held = false;
            for (int c : cards) {
                held = held || mark(c, owner) == Yes;
                if (mark(c, owner) == Unknown) {
                    open = c;
                    ++opens;
                }
            }
            if (!held && opens == 1)
                put(open, owner, Yes);
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
    me_ = me;
    level_ = level;
    rng_.seed(seed);
    mind_ = {};
    target_ = kNoRoom;
    // Its own hand known, and that it holds nothing else (0x42CC90).
    uint32_t hand = 0;
    for (int c : game.seat(me).hand)
        hand |= 1u << c;
    mind_.has[size_t(me)] = hand;
    mind_.known = hand;
    mind_.hasNot[size_t(me)] = ~hand & kAll;
    measure(game.board());
}

bool ComputerPlayer::takesIn() {
    // What others show each other, and what a wrong accusation tells: an
    // expert takes it in, a normal player half the time (Clue.exe's
    // rand() * 100 / 32767 > 50), a novice never (0x421C10, 0x421C80).
    if (level_ == Novice)
        return false;
    if (level_ == Expert)
        return true;
    return int((rng_() % 32768) * 100 / 32767) > 50;
}

uint32_t ComputerPlayer::pickBit(uint32_t mask) {
    // One of the bits by chance (0x42D050).
    const int n = bits(mask);
    if (n == 0)
        return 0;
    int k = int(rng_() % uint32_t(n));
    for (int i = 0; i < 32; ++i)
        if ((mask & (1u << i)) && k-- == 0)
            return 1u << i;
    return 0;
}

void ComputerPlayer::findSolution() {
    // Of each kind, the one card not known to be held (0x42D0B0).
    const uint32_t open = ~mind_.known;
    for (uint32_t kind : kKinds)
        if (bits(open & kind) == 1)
            mind_.solution |= open & kind;
}

void ComputerPlayer::settle() {
    // Each "holds one of these" pared down by what is known: what its
    // holder hasn't, what others have; held already, or down to one card
    // (then held), it's done with (0x42CCE0).
    bool changed = true;
    while (changed) {
        changed = false;
        for (int p = 0; p < kSuspectCount; ++p) {
            auto &list = mind_.oneOf[size_t(p)];
            for (size_t i = 0; i < list.size();) {
                uint32_t m = list[i] & ~mind_.hasNot[size_t(p)];
                bool held = false;
                for (int j = 0; j < kSuspectCount; ++j) {
                    if (j == p) {
                        if (mind_.has[size_t(p)] & m) {
                            held = true;
                            break;
                        }
                    } else
                        m &= ~mind_.has[size_t(j)];
                }
                list[i] = m;
                if (bits(m) == 1 && !held) {
                    list.erase(list.begin() + std::ptrdiff_t(i));
                    mind_.has[size_t(p)] |= m;
                    mind_.known |= m;
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
    return bits(mind_.solution) == 3;
}

void ComputerPlayer::observe(const Game &game) {
    const int suggester = game.turn();
    const int disprover = game.disprover();
    mind_.current = maskOf(game.suggestion());
    // Who couldn't answer holds none of the three (0x418935, all levels).
    for (int p : game.passed())
        if (p != me_) {
            mind_.hasNot[size_t(p)] |= mind_.current;
            settle();
        }
    if (disprover < 0) {
        // Nobody could answer its own suggestion: what of it isn't in its
        // hand is the answer (0x42D1B0, from 0x418804).
        if (me_ == suggester)
            mind_.solution |= mind_.current & ~mind_.has[size_t(me_)];
        return;
    }
    // Others: the one who answered holds one of them, as a level takes it
    // in (0x418D00).
    if (me_ != disprover && me_ != suggester && takesIn()) {
        mind_.oneOf[size_t(disprover)].push_back(mind_.current);
        settle();
    }
    // Its own suggestion: the card shown (0x42CE90).
    if (me_ == suggester && game.shown() >= 0) {
        mind_.has[size_t(disprover)] |= 1u << game.shown();
        mind_.known |= 1u << game.shown();
        findSolution();
        settle();
    }
}

void ComputerPlayer::observeAccusation(const Accusation &a, int accuser) {
    // A wrong accusation: Clue.exe takes it that the accuser holds none of
    // the three (0x41B26E, as a level takes it in), which needn't be so.
    if (takesIn()) {
        mind_.hasNot[size_t(accuser)] |= maskOf(a);
        settle();
    }
}

uint32_t ComputerPlayer::fallback(uint32_t kind, const Game &game) {
    (void)game;
    // Nothing of a kind in doubt (0x42D800): one of its own, else one
    // another is known to hold, from the seat before it backwards.
    if (const uint32_t mine = mind_.has[size_t(me_)] & kind)
        return pickBit(mine);
    for (int j = me_ - 1; j >= 0; --j)
        if (const uint32_t m = mind_.has[size_t(j)] & kind)
            return pickBit(m);
    for (int j = kSuspectCount - 1; j > me_; --j)
        if (const uint32_t m = mind_.has[size_t(j)] & kind)
            return pickBit(m);
    return pickBit(kind);
}

uint32_t ComputerPlayer::pickCard(uint32_t kind, const Game &game) {
    // A suspect or weapon to name (0x42D8A0): one not known to be held, or
    // the answer; above a novice, not one a "holds one of" is about if it
    // can. By chance among them. (Itself, if it hasn't its own card.)
    uint32_t open = (~mind_.known | mind_.solution) & kind;
    if (!open)
        return fallback(kind, game);
    if (level_ != Novice) {
        uint32_t talked = 0;
        for (const auto &list : mind_.oneOf)
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
    uint32_t open = ~(mind_.solution | mind_.known) & kRooms;
    if (mind_.moves < 3)
        open = kRooms;
    const Place &at = game.place(me_);
    if (at.inRoom() && !game.canSuggest())
        open &= ~(1u << roomCard(at.room));
    const uint32_t bit = open ? pickBit(open) : fallback(kRooms, game);
    return firstBit(bit) - roomCard(0);
}

void ComputerPlayer::measure(const Board &board) {
    // Each room's distances: from the squares before its doors, outwards.
    for (int r = 0; r < kRoomCount; ++r) {
        auto &d = toRoom_[size_t(r)];
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
        for (const auto &[square, d] : toRoom_[size_t(from.room)])
            if (d == 1) { // a square before one of its doors
                auto it = toRoom_[size_t(room)].find(square);
                if (it != toRoom_[size_t(room)].end())
                    best = std::min(best, it->second + 1);
            }
        return best;
    }
    auto it = toRoom_[size_t(room)].find(from.square);
    return it == toRoom_[size_t(room)].end() ? INT_MAX / 2 : it->second;
}

int ComputerPlayer::closestRoom(const Game &game) const {
    // GetClosestRoom with every room but its own (0x426710): through the
    // passage if it has one, else the nearest by its doors, the first in
    // the rooms' order of those as near.
    const Place &at = game.place(me_);
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
        return Action::Accuse;
    // In a room it may suggest in (brought there, or walked in): it does
    // (0x42C740, 0x4227C6).
    if (game.canSuggest())
        return Action::Suggest;
    if (game.phase() == Game::Phase::Start) {
        // The room it heads for (0x422570): if it is in it, the nearest
        // other; through the passage when that is the way there.
        target_ = pickRoom(game);
        const Place &at = game.place(me_);
        if (at.inRoom() && at.room == target_)
            target_ = closestRoom(game);
        if (game.canTakePassage() && game.board().passage(at.room) == target_)
            return Action::Passage;
        if (game.canRoll())
            return Action::Roll;
    }
    if (game.canMove() && !game.destinations().empty())
        return Action::Walk;
    if (!game.canEndTurn() && game.canTakePassage())
        return Action::Passage; // the only way on
    return Action::EndTurn;
}

std::vector<Place> ComputerPlayer::chooseWalk(const Game &game) {
    const auto options = game.destinations();
    if (options.empty())
        return {};
    // Along the shortest way to the room it heads for, as far as the die
    // goes (0x42C1F0, 0x42C180); into it if it can be reached. Without the
    // die, into it only with three moves left to suggest with.
    int room = target_ == kNoRoom ? closestRoom(game) : target_;
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
    ++mind_.moves;
    return options[best];
}

Accusation ComputerPlayer::chooseSuggestion(const Game &game) {
    // A suspect and a weapon (0x42D7B0), in the room it is in (0x4227E4).
    const int s = firstBit(pickCard(kSuspects, game)) - suspectCard(0);
    const int w = firstBit(pickCard(kWeapons, game)) - weaponCard(0);
    return {s, w, game.place(me_).room};
}

Accusation ComputerPlayer::chooseAccusation() const {
    return {firstBit(mind_.solution & kSuspects) - suspectCard(0), firstBit(mind_.solution & kWeapons) - weaponCard(0),
            firstBit(mind_.solution & kRooms) - roomCard(0)};
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
    if (level_ != Novice) {
        if (mind_.shownTo[size_t(suggester)] & cards)
            from = mind_.shownTo[size_t(suggester)] & cards;
        else if (mind_.shownAny & cards)
            from = mind_.shownAny & cards;
    }
    const uint32_t bit = pickBit(from);
    mind_.shownTo[size_t(suggester)] |= bit;
    mind_.shownAny |= bit;
    return firstBit(bit);
}

void Notebook::save(std::ostream &out) const {
    out << "notebook 1 " << countCards_ << ' ' << useClues_ << '\n';
    for (const auto &row : marks_) {
        for (Mark m : row)
            out << int(m) << ' ';
        out << '\n';
    }
    for (int n : handSize_)
        out << n << ' ';
    out << '\n' << oneOf_.size();
    for (const auto &[owner, cards] : oneOf_)
        out << ' ' << owner << ' ' << cards[0] << ' ' << cards[1] << ' ' << cards[2];
    out << '\n' << notAll_.size();
    for (const auto &cards : notAll_)
        out << ' ' << cards[0] << ' ' << cards[1] << ' ' << cards[2];
    out << '\n';
}

Error Notebook::load(std::istream &in) {
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version >> countCards_ >> useClues_) || tag != "notebook" || version != 1)
        return Error::fail("a saved notebook is damaged");
    for (auto &row : marks_)
        for (Mark &m : row) {
            int v = 0;
            if (!(in >> v) || v < Unknown || v > No)
                return Error::fail("a saved notebook is damaged");
            m = Mark(v);
        }
    for (int &n : handSize_)
        if (!(in >> n))
            return Error::fail("a saved notebook is damaged");
    const auto card = [](int c) { return c >= 0 && c < kCardCount; };
    size_t n = 0;
    if (!(in >> n) || n > 1000)
        return Error::fail("a saved notebook is damaged");
    oneOf_.assign(n, {});
    for (auto &[owner, cards] : oneOf_)
        if (!(in >> owner >> cards[0] >> cards[1] >> cards[2]) || owner < 0 || owner >= kOwners || !card(cards[0]) ||
            !card(cards[1]) || !card(cards[2]))
            return Error::fail("a saved notebook is damaged");
    if (!(in >> n) || n > 100)
        return Error::fail("a saved notebook is damaged");
    notAll_.assign(n, {});
    for (auto &cards : notAll_)
        if (!(in >> cards[0] >> cards[1] >> cards[2]) || !card(cards[0]) || !card(cards[1]) || !card(cards[2]))
            return Error::fail("a saved notebook is damaged");
    contradiction_ = false;
    return Error::success();
}

void ComputerPlayer::save(std::ostream &out) const {
    out << "player 3 " << me_ << ' ' << level_ << ' ' << target_ << '\n';
    rng_.save(out);
    out << '\n';
    const Mind &m = mind_;
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
    if (!(in >> tag >> version >> me_ >> level_ >> target_) || !rng_.load(in) || tag != "player" || version != 3 ||
        me_ < 0 || me_ >= kSuspectCount || level_ < Novice || level_ > Expert || target_ < kNoRoom ||
        target_ >= kRoomCount)
        return Error::fail("a saved computer player is damaged");
    Mind &m = mind_;
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
