// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The go-karts' and water rides' tracks, laid by the player a block at a
// time: docs/engines/tpw/games/simcoaster/systems/tracks.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <vector>

namespace reluva::tpw {

namespace {

// Bumper.WhichTrackType's: 1 a car track, 2 a water track.
constexpr int kKartTrack = 1;
constexpr int kWaterTrack = 2;

// How far a bridge lifts the track's floor over its ground: a ramp's and
// a level span's (gk_trckH_U's wood02, 5 to 9.4), a hump's (gk_trckH's, 5 to 10).
constexpr float kBridgeRise = 4.4f;
constexpr float kHumpRise = 5;

// "Coaster Problem", "Re: {0} ... haven't built any track" (TAG_SYSTEM).
constexpr uint32_t kCoasterProblemTitle = 256;
constexpr uint32_t kNoTrackLetter = 257;

// The ways out of a block, as bits, as the fences' are.
enum Way : int { kEast = 1, kSouth = 2, kWest = 4, kNorth = 8 };

int sign(int v) {
    return (v > 0) - (v < 0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

bool same(const Park::Cell &a, const Park::Cell &b) {
    return a.x == b.x && a.y == b.y;
}

int wayOf(int dx, int dy) {
    return dx > 0 ? kEast : dx < 0 ? kWest : dy > 0 ? kSouth : kNorth;
}

int opposite(int way) {
    return way == kEast ? kWest : way == kWest ? kEast : way == kSouth ? kNorth : kSouth;
}

// A bend's turn by the two sides it joins: unturned, east and south.
int bendAngle(int ways) {
    return ways == (kSouth | kEast) ? 0 : ways == (kWest | kSouth) ? 90 : ways == (kNorth | kWest) ? 180 : 270;
}

// A ramp's turn by the way the track goes up it: unturned, north.
int rampAngle(int way) {
    return way == kNorth ? 0 : way == kEast ? 90 : way == kSouth ? 180 : 270;
}

// An end's turn by the side it's open to: unturned, south.
int openAngle(int way) {
    return way == kSouth ? 0 : way == kWest ? 90 : way == kNorth ? 180 : 270;
}

size_t timesIn(const std::vector<Park::Cell> &track, const Park::Cell &block) {
    return size_t(std::count_if(track.begin(), track.end(), [&](const Park::Cell &b) { return same(b, block); }));
}

// The way from one block to the next.
int wayBetween(const Park::Cell &from, const Park::Cell &to) {
    return wayOf(sign(to.x - from.x), sign(to.y - from.y));
}

} // namespace

// The prebuilt park's (Land of Invention's waterwheel) have theirs in the
// game's save, which isn't read: they run as they are.
bool Park::hasTrack(size_t object) const {
    if (object >= _objects.size() || _objects[object].prebuilt)
        return false;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    return a && (a->trackType == kKartTrack || a->trackType == kWaterTrack);
}

// The block's cells turned about the ride's first cell as its shape's
// are (footprint()), its first cell the least of them.
Park::Cell Park::trackStart(size_t object) const {
    const PlacedObject &o = _objects[object];
    const Attractions::Attraction *a = _attractions.find(o.id);
    const int quarters = ((o.angle / 90) % 4 + 4) % 4;
    int least[2] = {INT_MAX, INT_MAX};
    for (int dy = 0; dy < kTrackBlock; ++dy)
        for (int dx = 0; dx < kTrackBlock; ++dx) {
            int x = (a ? a->trackJoinX : 0) + dx;
            int y = (a ? a->trackJoinY : 0) + dy;
            for (int t = 0; t < quarters; ++t) {
                const int was = x;
                x = -y;
                y = was;
            }
            least[0] = std::min(least[0], x);
            least[1] = std::min(least[1], y);
        }
    return {o.at.x + least[0], o.at.y + least[1]};
}

// Whether the first block lies east or west of the ride, so the track
// leaves it north or south.
bool Park::trackAlongY(size_t object) const {
    const PlacedObject &o = _objects[object];
    const Attractions::Attraction *a = _attractions.find(o.id);
    if (!a)
        return true;
    const Cell start = trackStart(object);
    for (const Cell &c : footprint(a->shape, o.at, o.angle).cells)
        if (c.y >= start.y && c.y < start.y + kTrackBlock && (c.x == start.x - 1 || c.x == start.x + kTrackBlock))
            return true;
    return false;
}

bool Park::trackComplete(size_t object) const {
    return hasTrack(object) && _objects[object].trackClosed;
}

// Over bare land, or over path and lines, which stay, the track a bridge
// over them ("drag over paths and lines to build bridges", TOTD 1); the
// first block over bare land only.
bool Park::layTrackBlock(PlacedObject &o, const Cell &block, int cost) {
    if (_cash < cost)
        return false;
    for (int dy = 0; dy < kTrackBlock; ++dy)
        for (int dx = 0; dx < kTrackBlock; ++dx) {
            const Ground g = ground({block.x + dx, block.y + dy});
            if (g != Ground::kOpen && (o.track.empty() || (g != Ground::kPath && g != Ground::kQueue)))
                return false;
        }
    for (int dy = 0; dy < kTrackBlock; ++dy)
        for (int dx = 0; dx < kTrackBlock; ++dx)
            if (ground({block.x + dx, block.y + dy}) == Ground::kOpen)
                _ground[size_t(block.y + dy) * size_t(_mapWidth) + size_t(block.x + dx)] = Ground::kTrack;
    book(Ledger::kBuilding, cost);
    o.track.push_back(block);
    return true;
}

// A run goes straight on from the track's end, never back the way it
// came, and never turns where the track crosses itself. It crosses the
// track only straight across a straight, and goes on past.
Park::TrackLaying Park::layTrack(size_t object, const Cell &to) {
    if (!hasTrack(object))
        return TrackLaying::kBlocked;
    PlacedObject &o = _objects[object];
    if (o.trackClosed)
        return TrackLaying::kClosed;
    const Attractions::Attraction *a = _attractions.find(o.id);
    const int cost = a->trackType == kKartTrack ? _kartTrackCost : _waterTrackCost;
    const Cell start = trackStart(object);
    if (o.track.empty() && !layTrackBlock(o, start, cost))
        return _cash < cost ? TrackLaying::kNoMoney : TrackLaying::kBlocked;
    const Cell end = o.track.back();
    const Cell target{start.x + floorDiv(to.x - start.x, kTrackBlock) * kTrackBlock,
                      start.y + floorDiv(to.y - start.y, kTrackBlock) * kTrackBlock};
    const int dx = sign(target.x - end.x);
    const int dy = sign(target.y - end.y);
    if ((dx != 0) == (dy != 0))
        return TrackLaying::kNotInLine;
    if (o.track.size() == 1) {
        if (trackAlongY(object) ? dx != 0 : dy != 0)
            return TrackLaying::kNotInLine;
    } else {
        const Cell &before = o.track[o.track.size() - 2];
        const int way = wayBetween(before, end);
        if (wayOf(dx, dy) == opposite(way))
            return TrackLaying::kNotInLine;
        if ((timesIn(o.track, end) > 1 || overWay(end)) && wayOf(dx, dy) != way)
            return TrackLaying::kNotInLine;
    }
    Cell at = end;
    while (!same(at, target)) {
        const Cell next{at.x + dx * kTrackBlock, at.y + dy * kTrackBlock};
        if (same(next, start)) {
            // Back into the first block the way the track left it.
            if (o.track.size() >= 3 && wayBetween(start, o.track[1]) == wayOf(dx, dy)) {
                o.trackClosed = true;
                return TrackLaying::kClosed;
            }
            return TrackLaying::kBlocked;
        }
        const size_t seen = timesIn(o.track, next);
        if (seen == 1 && !same(next, target)) {
            const auto it = std::find_if(o.track.begin(), o.track.end(), [&](const Cell &b) { return same(b, next); });
            const size_t i = size_t(it - o.track.begin());
            const int earlier = i > 0 && i + 1 < o.track.size() ? wayBetween(o.track[i - 1], o.track[i]) : 0;
            const bool straight = earlier && earlier == wayBetween(o.track[i], o.track[i + 1]);
            const bool across = (earlier & (kEast | kWest)) ? dx == 0 : dy == 0;
            if (!straight || !across)
                return TrackLaying::kBlocked;
            o.track.push_back(next);
            at = next;
            continue;
        }
        if (seen > 0)
            return TrackLaying::kBlocked;
        // A bridge goes on past: a run doesn't stop on one.
        if (overWay(next) && same(next, target))
            return TrackLaying::kBlocked;
        if (!layTrackBlock(o, next, cost))
            return _cash < cost ? TrackLaying::kNoMoney : TrackLaying::kBlocked;
        at = next;
    }
    return TrackLaying::kLaid;
}

bool Park::undoTrack(size_t object) {
    if (!hasTrack(object) || _objects[object].track.empty())
        return false;
    PlacedObject &o = _objects[object];
    const Cell last = o.track.back();
    o.track.pop_back();
    o.trackClosed = false;
    if (timesIn(o.track, last) == 0)
        freeTrackBlock(last);
    return true;
}

bool Park::overWay(const Cell &block) const {
    for (int dy = 0; dy < kTrackBlock; ++dy)
        for (int dx = 0; dx < kTrackBlock; ++dx) {
            const Ground g = ground({block.x + dx, block.y + dy});
            if (g == Ground::kPath || g == Ground::kQueue)
                return true;
        }
    return false;
}

std::optional<std::pair<size_t, size_t>> Park::trackOver(const Cell &cell) const {
    for (size_t i = 0; i < _objects.size(); ++i) {
        if (!hasTrack(i))
            continue;
        const std::vector<Cell> &track = _objects[i].track;
        for (size_t k = 0; k < track.size(); ++k)
            if (cell.x >= track[k].x && cell.x < track[k].x + kTrackBlock && cell.y >= track[k].y &&
                cell.y < track[k].y + kTrackBlock)
                return std::make_pair(i, k);
    }
    return std::nullopt;
}

// A straight block, once in its track, with blocks on both sides of it
// going the same way.
bool Park::bridgeable(const Cell &cell) const {
    const auto over = trackOver(cell);
    if (!over)
        return false;
    const PlacedObject &o = _objects[over->first];
    const std::vector<Cell> &track = o.track;
    const size_t k = over->second;
    if (k == 0 || timesIn(track, track[k]) > 1 || (k + 1 >= track.size() && !o.trackClosed))
        return false;
    return wayBetween(track[k - 1], track[k]) == wayBetween(track[k], track[(k + 1) % track.size()]);
}

Park::Ground Park::bare(const Cell &cell) const {
    return trackOver(cell) ? Ground::kTrack : Ground::kOpen;
}

void Park::freeTrackBlock(const Cell &block) {
    for (int dy = 0; dy < kTrackBlock; ++dy)
        for (int dx = 0; dx < kTrackBlock; ++dx)
            if (ground({block.x + dx, block.y + dy}) == Ground::kTrack)
                _ground[size_t(block.y + dy) * size_t(_mapWidth) + size_t(block.x + dx)] = Ground::kOpen;
}

std::vector<Park::TrackPiece> Park::trackPieces(size_t object) const {
    std::vector<TrackPiece> out;
    if (!hasTrack(object))
        return out;
    const PlacedObject &o = _objects[object];
    const std::vector<Cell> &track = o.track;
    const size_t n = track.size();
    const int along = trackAlongY(object) ? 0 : 90;
    for (size_t i = 0; i < n; ++i) {
        const Cell &b = track[i];
        if (timesIn(track, b) > 1) {
            if (std::find_if(track.begin(), track.end(), [&](const Cell &c) { return same(c, b); }) ==
                track.begin() + long(i))
                out.push_back({b, TrackModel::kCrossing, 0});
            continue;
        }
        if (i == 0) {
            out.push_back({b, TrackModel::kStart, along});
            continue;
        }
        const int in = wayBetween(track[i - 1], b);
        if (i + 1 == n && !o.trackClosed) {
            out.push_back({b, TrackModel::kEnd, openAngle(opposite(in))});
            continue;
        }
        const bool hasOut = i + 1 < n || o.trackClosed;
        const int out2 = hasOut ? wayBetween(b, track[(i + 1) % n]) : in;
        if (in != out2) {
            out.push_back({b, TrackModel::kBend, bendAngle(opposite(in) | out2)});
            continue;
        }
        if (!overWay(b)) {
            out.push_back({b, TrackModel::kStraight, (in & (kNorth | kSouth)) ? 0 : 90});
            continue;
        }
        // Its place in the run of bridge blocks it's in, the ramps
        // turned to rise the way the track goes.
        const auto bridged = [&](size_t k) {
            return k > 0 && k < n && overWay(track[k]) && timesIn(track, track[k]) == 1;
        };
        const bool before = bridged(i - 1) && wayBetween(track[i - 1], b) == in;
        const bool after = i + 1 < n && bridged(i + 1) && wayBetween(b, track[i + 1]) == in;
        const TrackModel model = before && after ? TrackModel::kBridgeMiddle
                                 : after         ? TrackModel::kRampUp
                                 : before        ? TrackModel::kRampDown
                                                 : TrackModel::kBridge;
        out.push_back({b, model, rampAngle(in)});
    }
    return out;
}

std::vector<gfx::Vec3> Park::trackCircuit(size_t object) const {
    std::vector<gfx::Vec3> out;
    if (!trackComplete(object))
        return out;
    const std::vector<Cell> &track = _objects[object].track;
    const size_t n = track.size();
    const auto bridged = [&](size_t k) { return k > 0 && timesIn(track, track[k]) == 1 && overWay(track[k]); };
    for (size_t i = 0; i < n; ++i) {
        const Cell &a = track[i];
        const Cell &b = track[(i + 1) % n];
        // A bridge of one block humps up in its middle.
        if (bridged(i) && !bridged(i - 1) && !bridged((i + 1) % n))
            out.push_back({(float(a.x) + 1) * kCellUnits, kHumpRise, (float(a.y) + 1) * kCellUnits});
        const float x = float(a.x) + 1 + float(sign(b.x - a.x));
        const float y = float(a.y) + 1 + float(sign(b.y - a.y));
        out.push_back({x * kCellUnits, bridged(i) && bridged((i + 1) % n) ? kBridgeRise : 0, y * kCellUnits});
    }
    return out;
}

// A day after a track ride's built without a whole track, the letter
// saying so, once (guessed).
void Park::trackDay() {
    for (size_t i = 0; i < _objects.size(); ++i) {
        PlacedObject &o = _objects[i];
        if (!hasTrack(i) || o.trackClosed || o.toldNoTrack || day() <= o.placedDay)
            continue;
        o.toldNoTrack = true;
        const Attractions::Attraction *a = _attractions.find(o.id);
        post(kCoasterProblemTitle, kNoTrackLetter, {{0, o.name.empty() && a ? a->name : o.name}});
    }
}

int Park::longestKartStraight() const {
    int best = 0;
    for (size_t i = 0; i < _objects.size(); ++i) {
        const Attractions::Attraction *a = _attractions.find(_objects[i].id);
        if (!a || a->trackType != kKartTrack || !_objects[i].trackClosed)
            continue;
        const std::vector<Cell> &track = _objects[i].track;
        const size_t n = track.size();
        // Blocks in a row going the same way, round the circuit's end.
        int run = 1;
        for (size_t k = 1; k < 2 * n; ++k) {
            const int before = wayBetween(track[(k - 1) % n], track[k % n]);
            const int after = wayBetween(track[k % n], track[(k + 1) % n]);
            run = before == after ? run + 1 : 1;
            best = std::max(best, std::min(run, int(n)));
        }
    }
    return best;
}

int Park::longestWaterTrack() const {
    int best = 0;
    for (size_t i = 0; i < _objects.size(); ++i) {
        const Attractions::Attraction *a = _attractions.find(_objects[i].id);
        if (a && a->trackType == kWaterTrack && _objects[i].trackClosed)
            best = std::max(best, int(_objects[i].track.size()) * kTrackBlock);
    }
    return best;
}

} // namespace reluva::tpw
