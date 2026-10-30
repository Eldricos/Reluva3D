// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ai.h"

#include <algorithm>
#include <climits>
#include <deque>

namespace reluva::clue {

namespace {

constexpr int kKindFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
constexpr int kKindCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};

std::array<int, 3> cardsOf(const Accusation& a) {
    return {suspectCard(a.suspect), weaponCard(a.weapon), roomCard(a.room)};
}

} // namespace

// ---- The notebook --------------------------------------------------------------

void Notebook::reset(const Game& game, int me, bool countCards, bool useClues) {
    countCards_ = countCards;
    useClues_ = useClues;
    contradiction_ = false;
    oneOf_.clear();
    notAll_.clear();
    for (auto& row : marks_)
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
    Mark& at = marks_[size_t(card)][size_t(owner)];
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

void Notebook::holdsOneOf(int owner, const std::array<int, 3>& cards) {
    if (!useClues_)
        return;
    oneOf_.push_back({owner, cards});
    settle();
}

void Notebook::notAll(const std::array<int, 3>& cards) {
    if (!countCards_)
        return;
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
            int open = -1, opens = 0;
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
            int yes = -1, open = -1, opens = 0;
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
                int yes = 0, open = 0;
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
            // A wrong accusation: with two of its cards in the case file,
            // the third isn't.
            for (const auto& cards : notAll_) {
                int in = 0, other = -1;
                for (int c : cards) {
                    if (inCaseFile(c))
                        ++in;
                    else
                        other = c;
                }
                if (in == 2)
                    put(other, kCaseFile, No);
            }
        }
        // Who showed a card not seen holds one of the three: when two are
        // lacked, the third.
        for (const auto& [owner, cards] : oneOf_) {
            int open = -1, opens = 0;
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

void ComputerPlayer::reset(const Game& game, int me, uint32_t seed, int level) {
    me_ = me;
    level_ = level;
    rng_.seed(seed);
    shownTo_.clear();
    notes_.reset(game, me, false, level >= Expert);
    // Each room's distances: from the squares before its doors, outwards.
    const Board& board = game.board();
    for (int r = 0; r < kRoomCount; ++r) {
        auto& d = toRoom_[size_t(r)];
        d.clear();
        std::deque<Square> queue;
        for (const Board::Door& door : board.doors())
            if (door.room == r && !d.count(door.outside)) {
                d[door.outside] = 1;
                queue.push_back(door.outside);
            }
        while (!queue.empty()) {
            const Square s = queue.front();
            queue.pop_front();
            for (const Place& n : board.neighbours({s, kNoRoom}))
                if (!n.inRoom() && !d.count(n.square)) {
                    d[n.square] = d[s] + 1;
                    queue.push_back(n.square);
                }
        }
    }
}

void ComputerPlayer::observe(const Game& game) {
    const int suggester = game.turn(), disprover = game.disprover();
    const std::array<int, 3> cards = cardsOf(game.suggestion());
    const bool mine = suggester == me_;
    // Who couldn't answer lacks all three: below expert, noted only of its
    // own suggestions.
    if (mine || level_ >= Expert)
        for (int p : game.passed())
            for (int c : cards)
                notes_.lacks(p, c);
    if (disprover < 0) {
        // Nobody could: what the suggester doesn't hold is the answer (or
        // the suggester's, which only it knows).
        if (mine)
            for (int c : cards)
                if (notes_.mark(c, me_) != Notebook::Yes)
                    notes_.holds(Notebook::kCaseFile, c);
        return;
    }
    if (mine && game.shown() >= 0)
        notes_.holds(disprover, game.shown());
    else if (disprover != me_)
        notes_.holdsOneOf(disprover, cards);
}

void ComputerPlayer::observeAccusation(const Accusation& a) { notes_.notAll(cardsOf(a)); }

bool ComputerPlayer::roomWanted(int room) const {
    // A room worth suggesting in: one that may be the answer; or, the room
    // being known, any (a novice only those in doubt).
    const int card = roomCard(room);
    if (!notes_.outOfCaseFile(card))
        return true;
    return level_ >= Normal && notes_.solved(2) >= 0;
}

int ComputerPlayer::pick(int kind, const Game& game) {
    const int first = kKindFirst[kind], count = kKindCount[kind];
    (void)game;
    // Settled: the answer (nobody can show it, so what's shown is of the
    // rest), or a card of its own; a novice names what it likes.
    const int answer = notes_.solved(kind);
    if (answer >= 0 && level_ >= Expert)
        return answer - first;
    // A novice names anyone and anything, itself about a quarter of the
    // time, as the original's novices do (docs/engines/clue.md).
    if (level_ == Novice) {
        if (kind == 0 && rng_() % 8 == 0)
            return me_;
        return int(rng_() % uint32_t(count));
    }
    // In doubt: one whose holder isn't known; an expert the one fewest
    // players might hold, as that settles most.
    std::vector<int> open, mineToo;
    for (int c = first; c < first + count; ++c) {
        // An expert doesn't name itself while in doubt (the original's
        // experts never did).
        if (notes_.holder(c) < 0 && !(level_ >= Expert && kind == 0 && c == suspectCard(me_)))
            open.push_back(c);
        if (notes_.mark(c, me_) == Notebook::Yes)
            mineToo.push_back(c);
    }
    if (open.empty())
        return answer >= 0 ? answer - first : int(rng_() % uint32_t(count));
    if (level_ >= Expert) {
        std::stable_sort(open.begin(), open.end(), [&](int a, int b) {
            const auto maybe = [&](int c) {
                int n = 0;
                for (int o = 0; o < Notebook::kOwners; ++o)
                    n += notes_.mark(c, o) == Notebook::Unknown;
                return n;
            };
            return maybe(a) < maybe(b);
        });
        // One of the best two, by chance, so as not to be read.
        return open[rng_() % std::min<size_t>(open.size(), 2)] - first;
    }
    return open[rng_() % open.size()] - first;
}

Accusation ComputerPlayer::chooseAccusation() const {
    const auto only = [&](int kind) {
        const int c = notes_.solved(kind);
        return c >= 0 ? c - kKindFirst[kind] : 0;
    };
    return {only(0), only(1), only(2)};
}

int ComputerPlayer::distance(const Place& from, int room) const {
    if (from.inRoom()) {
        if (from.room == room)
            return 0;
        int best = INT_MAX / 2;
        for (const auto& [square, d] : toRoom_[size_t(from.room)])
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

int ComputerPlayer::target(const Game& game) const {
    // The nearest room worth suggesting in, not the one it is in.
    const Place& at = game.place(me_);
    int best = kNoRoom, bestD = INT_MAX;
    for (int pass = 0; pass < 2 && best == kNoRoom; ++pass)
        for (int r = 0; r < kRoomCount; ++r) {
            if (at.inRoom() && at.room == r)
                continue;
            if (pass == 0 && !roomWanted(r))
                continue;
            const int d = distance(at, r);
            if (d < bestD) {
                bestD = d;
                best = r;
            }
        }
    return best;
}

ComputerPlayer::Action ComputerPlayer::decide(const Game& game) {
    if (game.canAccuse() && ready())
        return Action::Accuse;
    if (game.canSuggest() && (roomWanted(game.place(me_).room) || game.phase() != Game::Phase::Start))
        return Action::Suggest;
    if (game.phase() == Game::Phase::Start) {
        if (game.canTakePassage()) {
            // Through the passage when the room beyond is worth it, and
            // nearer than anything the die might reach.
            const int to = game.board().passage(game.place(me_).room);
            if (roomWanted(to))
                return Action::Passage;
        }
        if (game.canRoll())
            return Action::Roll;
    }
    if (game.canMove() && !game.destinations().empty())
        return Action::Walk;
    if (!game.canEndTurn() && game.canTakePassage())
        return Action::Passage; // the only way on
    return Action::EndTurn;
}

std::vector<Place> ComputerPlayer::chooseWalk(const Game& game) {
    const auto options = game.destinations();
    if (options.empty())
        return {};
    const int room = target(game);
    size_t best = 0;
    int bestD = INT_MAX;
    for (size_t i = 0; i < options.size(); ++i) {
        const Place& end = options[i].back();
        // Any room worth it reached now is as good as the one aimed for;
        // without the die, only with three moves left to suggest there.
        int d = room == kNoRoom ? 0 : distance(end, room);
        const bool canAfford = !game.noDieRoll() || game.moves() - int(options[i].size()) >= Game::kActionCost;
        if (end.inRoom())
            d = (roomWanted(end.room) || end.room == room) && canAfford ? -1 : d + 2;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return options[best];
}

Accusation ComputerPlayer::chooseSuggestion(const Game& game) {
    const int s = pick(0, game), w = pick(1, game);
    return {s, w, game.place(me_).room};
}

int ComputerPlayer::chooseShow(const Game& game, int suggester) {
    const auto cards = game.disproving();
    if (cards.empty())
        return -1;
    auto& shown = shownTo_[suggester];
    if (level_ >= Normal) {
        // A card that suggester has seen already gives nothing away; an
        // expert then shows one already shown to others.
        for (int c : cards)
            if (std::find(shown.begin(), shown.end(), c) != shown.end())
                return c;
        if (level_ >= Expert)
            for (const auto& [who, theirs] : shownTo_)
                for (int c : cards)
                    if (std::find(theirs.begin(), theirs.end(), c) != theirs.end()) {
                        shown.push_back(c);
                        return c;
                    }
    }
    const int c = cards[rng_() % cards.size()];
    shown.push_back(c);
    return c;
}

} // namespace reluva::clue
