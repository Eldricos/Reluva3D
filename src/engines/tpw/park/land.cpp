// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's land, inside its fence: the world's to start with, more as
// missions are done. See docs/engines/tpw/games/simcoaster/systems/land.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reluva::tpw {

namespace {

// hoardings.map: a byte for each of the world's 128 × 128 cells, 0 where
// it's the park's.
constexpr int kHoardingsSide = 128;

// The ways from a point where cells' corners meet, as bits.
enum Way : int { kEast = 1, kSouth = 2, kWest = 4, kNorth = 8 };
constexpr Way kWays[] = {kEast, kSouth, kWest, kNorth};

Park::Cell stepTo(const Park::Cell &p, int way) {
    return {p.x + (way == kEast) - (way == kWest), p.y + (way == kSouth) - (way == kNorth)};
}

int opposite(int way) {
    return way == kEast ? kWest : way == kWest ? kEast : way == kSouth ? kNorth : kSouth;
}

struct CellOrder {
    bool operator()(const Park::Cell &a, const Park::Cell &b) const { return a.x != b.x ? a.x < b.x : a.y < b.y; }
    bool operator()(const std::pair<Park::Cell, int> &a, const std::pair<Park::Cell, int> &b) const {
        if (a.first.x != b.first.x || a.first.y != b.first.y)
            return (*this)(a.first, b.first);
        return a.second < b.second;
    }
};

// Turned a quarter at a time, east towards south: an end's way from its
// point, and a diagonal's two.
int endAngle(int way) {
    return way == kSouth ? 0 : way == kWest ? 90 : way == kNorth ? 180 : 270;
}

int diagonalAngle(int ways) {
    return ways == (kSouth | kEast) ? 0 : ways == (kWest | kSouth) ? 90 : ways == (kNorth | kWest) ? 180 : 270;
}

// Which of the four looks a piece has, varied by its place (guessed).
int lookAt(const Park::Cell &p) {
    return (((p.x * 7 + p.y * 13) % 4) + 4) % 4 + 1;
}

} // namespace

void Park::loadLand(const SearchSet &files) {
    _owned.assign(_ground.size(), 1);
    std::vector<uint8_t> data;
    if (files.read(_folder + "/hoardings.map", data) || data.size() != size_t(kHoardingsSide) * kHoardingsSide)
        return;
    // Laid out as the attribute map is: row x, column y.
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y)
            _owned[size_t(y) * size_t(_mapWidth) + size_t(x)] =
                x < kHoardingsSide && y < kHoardingsSide && data[size_t(x) * kHoardingsSide + size_t(y)] == 0;
}

// The clicks are the fence's corners, from a point by the entrance: x 0
// four cells past the entrance's second cell, the eight-cell gap the
// fence leaves (x −8 to 0) about the entrance; y 0 the cell past it. So
// every mission's fence lies along the worlds' walls (inferred).
void Park::growLand(const std::vector<Cell> &clicks) {
    if (clicks.size() < 3 || _fixed.entrance.size() < 2 || _owned.empty())
        return;
    const Cell origin{_fixed.entrance[1].x + 4, _fixed.entrance[0].y + 1};
    _fence.clear();
    for (const Cell &c : clicks)
        _fence.push_back({origin.x + c.x, origin.y + c.y});
    const size_t n = clicks.size();
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y) {
            // A cell's the park's if its middle's inside the fence.
            const float px = float(x - origin.x) + 0.5f;
            const float py = float(y - origin.y) + 0.5f;
            bool inside = false;
            for (size_t i = 0; i < n; ++i) {
                const Cell &a = clicks[i];
                const Cell &b = clicks[(i + 1) % n];
                if ((float(a.y) > py) != (float(b.y) > py) &&
                    px < float(a.x) + (py - float(a.y)) * float(b.x - a.x) / float(b.y - a.y))
                    inside = !inside;
            }
            if (inside)
                _owned[size_t(y) * size_t(_mapWidth) + size_t(x)] = 1;
        }
}

// A fence's runs between where it turns or stops: a piece at each end of
// a run reaches its first edge; between, a straight every other point
// reaches two. A run of an odd number of edges ends with an end piece
// against the turn's, where the game's pieces would overlap.
std::vector<Park::FencePiece> Park::fencePieces(const std::vector<Edge> &edges,
                                                const std::function<bool(const Cell &)> &facing) {
    std::map<Cell, int, CellOrder> ways;
    for (const Edge &e : edges) {
        const int dx = e.second.x - e.first.x;
        const int dy = e.second.y - e.first.y;
        if (std::abs(dx) + std::abs(dy) != 1)
            continue;
        const int way = dx > 0 ? kEast : dx < 0 ? kWest : dy > 0 ? kSouth : kNorth;
        ways[e.first] |= way;
        ways[e.second] |= opposite(way);
    }
    auto turns = [&](const Cell &p) {
        const auto it = ways.find(p);
        const int w = it == ways.end() ? 0 : it->second;
        return w != (kEast | kWest) && w != (kNorth | kSouth);
    };
    std::vector<FencePiece> pieces;
    auto straight = [&](const Cell &p, int way) {
        int angle = 0;
        if (way == kSouth || way == kNorth)
            angle = facing({p.x, p.y}) || facing({p.x, p.y - 1}) ? 0 : 180;
        else
            angle = facing({p.x, p.y}) || facing({p.x - 1, p.y}) ? 90 : 270;
        pieces.push_back({p, FenceModel::kStraight, lookAt(p), angle});
    };
    std::set<std::pair<Cell, int>, CellOrder> walked;
    for (const auto &[p, w] : ways) {
        if (!turns(p))
            continue;
        if (w == (kSouth | kEast) || w == (kWest | kSouth) || w == (kNorth | kWest) || w == (kEast | kNorth))
            pieces.push_back({p, FenceModel::kDiagonal, lookAt(p), diagonalAngle(w)});
        else
            for (int way : kWays)
                if (w & way)
                    pieces.push_back({p, FenceModel::kEnd, 1, endAngle(way)});
        for (int way : kWays) {
            if (!(w & way) || walked.count({p, way}))
                continue;
            std::vector<Cell> run{p};
            Cell q = p;
            do {
                walked.insert({q, way});
                q = stepTo(q, way);
                walked.insert({q, opposite(way)});
                run.push_back(q);
            } while (!turns(q) && !(q.x == p.x && q.y == p.y));
            const int k = int(run.size()) - 1;
            const int last = k % 2 == 0 ? k - 2 : k - 3; // the last straight's point
            for (int i = 2; i <= last; i += 2)
                straight(run[size_t(i)], way);
            if (k % 2 == 1 && k >= 3)
                pieces.push_back({run[size_t(k - 1)], FenceModel::kEnd, 1, endAngle(opposite(way))});
        }
    }
    return pieces;
}

std::vector<Park::FencePiece> Park::fencePieces() const {
    std::vector<Edge> edges;
    // From the first corner round to the last: the gap back to the first is the way in.
    for (size_t i = 0; i + 1 < _fence.size(); ++i) {
        Cell p = _fence[i];
        const Cell &to = _fence[i + 1];
        while (p.x != to.x || p.y != to.y) {
            const Cell next{p.x + (to.x > p.x) - (to.x < p.x), p.y + (to.y > p.y) - (to.y < p.y)};
            edges.emplace_back(p, next);
            p = next;
        }
    }
    return fencePieces(edges, [&](const Cell &c) { return owns(c); });
}

// Each mark's edges, in the shape's own cells (its last row at the first
// cell, a row up y + 1), then turned as the ride is.
std::vector<Park::FencePiece> Park::hoardingPieces(size_t object) const {
    if (object >= _objects.size() || !awaitsActivation(object))
        return {};
    const PlacedObject &o = _objects[object];
    const Attractions::Attraction *a = _attractions.find(o.id);
    if (!a || a->hoarding.empty())
        return {};
    const int rows = int(a->hoarding.size());
    const int quarters = ((o.angle / 90) % 4 + 4) % 4;
    auto place = [&](int px, int py) {
        for (int t = 0; t < quarters; ++t) {
            const int was = px;
            px = 1 - py;
            py = was;
        }
        return Cell{o.at.x + px, o.at.y + py};
    };
    std::vector<Edge> edges;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < int(a->hoarding[size_t(r)].size()); ++c) {
            const char mark = a->hoarding[size_t(r)][size_t(c)];
            const int x = c;
            const int y = rows - 1 - r;
            const bool top = mark == 'F' || mark == '7' || mark == '^';
            const bool bottom = mark == 'L' || mark == 'J' || mark == '_';
            const bool left = mark == 'F' || mark == 'L' || mark == '[';
            const bool right = mark == '7' || mark == 'J' || mark == ']';
            if (top)
                edges.emplace_back(place(x, y + 1), place(x + 1, y + 1));
            if (bottom)
                edges.emplace_back(place(x, y), place(x + 1, y));
            if (left)
                edges.emplace_back(place(x, y), place(x, y + 1));
            if (right)
                edges.emplace_back(place(x + 1, y), place(x + 1, y + 1));
        }
    const std::vector<Cell> own = footprint(a->shape, o.at, o.angle).cells;
    return fencePieces(edges, [&](const Cell &c) {
        return std::none_of(own.begin(), own.end(), [&](const Cell &f) { return f.x == c.x && f.y == c.y; });
    });
}

bool Park::pathLaid(const Cell &cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= _mapWidth || cell.y >= _mapHeight)
        return false;
    return _ground[size_t(cell.y) * size_t(_mapWidth) + size_t(cell.x)] == Ground::kPath;
}

bool Park::owns(const Cell &cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= _mapWidth || cell.y >= _mapHeight)
        return false;
    return _owned.empty() || _owned[size_t(cell.y) * size_t(_mapWidth) + size_t(cell.x)] != 0;
}

} // namespace reluva::tpw
