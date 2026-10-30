// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ai.h"

#include <algorithm>
#include <climits>
#include <deque>

namespace reluva::clue {

void ComputerPlayer::reset(const Game& game, int me, uint32_t seed) {
    me_ = me;
    rng_.seed(seed);
    out_.fill(false);
    found_.fill(false);
    shownTo_.clear();
    for (int c : game.seat(me).hand)
        out_[size_t(c)] = true;
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

void ComputerPlayer::observe(const Game& game, int suggester, const Accusation& s, int disprover, int shown) {
    if (suggester == me_ && shown >= 0)
        out_[size_t(shown)] = true;
    // Nobody could answer: what the suggester named and doesn't hold is the
    // answer. Only the cards it holds itself are sure to be the suggester's.
    if (disprover < 0 && suggester == me_)
        for (int c : {suspectCard(s.suspect), weaponCard(s.weapon), roomCard(s.room)})
            if (!out_[size_t(c)])
                found_[size_t(c)] = true;
    (void)game;
}

int ComputerPlayer::pick(int first, int count) {
    // The one found, else one not known to be out, else any.
    for (int c = first; c < first + count; ++c)
        if (found_[size_t(c)])
            return c - first;
    std::vector<int> open;
    for (int c = first; c < first + count; ++c)
        if (!out_[size_t(c)])
            open.push_back(c - first);
    if (open.empty())
        return int(rng_() % uint32_t(count));
    return open[rng_() % open.size()];
}

bool ComputerPlayer::ready() const {
    const auto known = [&](int first, int count) {
        int open = 0;
        for (int c = first; c < first + count; ++c) {
            if (found_[size_t(c)])
                return true;
            open += !out_[size_t(c)];
        }
        return open == 1;
    };
    return known(suspectCard(0), kSuspectCount) && known(weaponCard(0), kWeaponCount) &&
           known(roomCard(0), kRoomCount);
}

Accusation ComputerPlayer::chooseAccusation() const {
    const auto only = [&](int first, int count) {
        for (int c = first; c < first + count; ++c)
            if (found_[size_t(c)])
                return c - first;
        for (int c = first; c < first + count; ++c)
            if (!out_[size_t(c)])
                return c - first;
        return 0;
    };
    return {only(suspectCard(0), kSuspectCount), only(weaponCard(0), kWeaponCount), only(roomCard(0), kRoomCount)};
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
    // The nearest room still in doubt, not the one it is in; failing that
    // (it knows the room) the nearest other room, to ask about the rest.
    const Place& at = game.place(me_);
    int best = kNoRoom, bestD = INT_MAX;
    for (int pass = 0; pass < 2 && best == kNoRoom; ++pass)
        for (int r = 0; r < kRoomCount; ++r) {
            if (at.inRoom() && at.room == r)
                continue;
            if (pass == 0 && out_[size_t(roomCard(r))] && !found_[size_t(roomCard(r))])
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
    if (game.canSuggest())
        return Action::Suggest;
    if (game.phase() == Game::Phase::Start) {
        if (game.canTakePassage()) {
            // Through the passage when the room beyond is one in doubt.
            const int to = game.board().passage(game.place(me_).room);
            if (!out_[size_t(roomCard(to))])
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
        // Any room in doubt reached now is as good as the one aimed for.
        int d = room == kNoRoom ? 0 : distance(end, room);
        if (end.inRoom())
            d = out_[size_t(roomCard(end.room))] && end.room != room ? d : -1;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return options[best];
}

Accusation ComputerPlayer::chooseSuggestion(const Game& game) {
    return {pick(suspectCard(0), kSuspectCount), pick(weaponCard(0), kWeaponCount), game.place(me_).room};
}

int ComputerPlayer::chooseShow(const Game& game, int suggester) {
    // A card that suggester has seen already gives nothing away.
    const auto cards = game.disproving();
    if (cards.empty())
        return -1;
    auto& shown = shownTo_[suggester];
    for (int c : cards)
        if (std::find(shown.begin(), shown.end(), c) != shown.end())
            return c;
    const int c = cards[rng_() % cards.size()];
    shown.push_back(c);
    return c;
}

} // namespace reluva::clue
