// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/walk_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::awe {

bool WalkGrid::load(const Ac2Record& camr) {
    size_ = 0;
    cells_.fill(0);
    const Ac2Value* grid = camr.field("grid");
    if (!grid || camr.i("23c") <= 0)
        return false;
    // The game reads size x size bytes into its 64-wide array as they come.
    const size_t n = std::min(grid->bytes.size(), cells_.size());
    std::copy(grid->bytes.begin(), grid->bytes.begin() + std::ptrdiff_t(n), cells_.begin());
    size_ = camr.i("23c");
    return true;
}

void WalkGrid::centre(Cell c, float& x, float& z) const {
    x = float((c.a - 32) * size_ + size_ / 2);
    z = float((32 - c.b) * size_ - size_ / 2);
}

WalkGrid::Cell WalkGrid::cellAt(float x, float z) const {
    Cell c;
    for (int i = 0; i < kSize; i++) {
        if (float((i - 32) * size_) <= x && x <= float((i - 31) * size_))
            c.a = i;
        if (float((31 - i) * size_) <= z && z <= float((32 - i) * size_))
            c.b = i;
    }
    if (!c.valid())
        return Cell();
    return c;
}

bool WalkGrid::blocked(Cell c, bool openThrees, bool openTwos) const {
    if (!inside(c.a, c.b))
        return true;
    const uint8_t v = at(c.a, c.b);
    return v == 1 || (v == 2 && !openTwos) || (v == 3 && !openThrees);
}

namespace {

// The game's path-finder (motoe.exe 0x42e840..0x431f60), as the game sets
// it up (0x42e900): the search runs over cells without headings, and only
// its smoothing pass turns, along circles of radius 0.1 cell. The other
// paths of its code (searching with headings, the lattice of 48 moves and
// 8 headings, two-circle paths) are off and left out. Its arithmetic is
// the x87's at 53 bits, so doubles, in its order, give its results.

constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kTurnRadius = 0.1;  // 0x1008
constexpr double kWidth = 0.1;       // 0x1018: what's tested around each point
constexpr double kTestStep = 0.2;    // 0x1020: how far apart the points are
constexpr int kWindow = 192;         // the search's window, cells a side
constexpr int kMaxPoints = 127;      // the most a path can have (0x430471)

// x87 comparisons treat a NaN as less.
bool below(double a, double b) { return std::isnan(a) || std::isnan(b) || a < b; }

// An angle into [0, 2 pi), snapped to 0 within 1e-8 of either end (the
// code repeated after every angle sum).
double normalize(double v) {
    static const double almostTwoPi = kTwoPi - 1e-8; // 0x42e820
    if (!below(v, kTwoPi))
        do
            v -= kTwoPi;
        while (!below(v, kTwoPi));
    while (below(v, 0))
        v += kTwoPi;
    if (below(v, 1e-8) || !(below(v, almostTwoPi) || v == almostTwoPi))
        v = 0;
    return v;
}

// The C runtime's acos (0x473650), as it computes it.
double runtimeAcos(double x) { return x == 1.0 ? 0.0 : std::atan2(std::sqrt((1 + x) * (1 - x)), x); }

// sqrt(i^2 + j^2), times 128, truncated (0x42ea40): the distance estimate.
int distance(int i, int j) {
    static const std::vector<int> table = [] {
        std::vector<int> t(size_t(kWindow * kWindow));
        for (int a = 0; a < kWindow; a++)
            for (int b = 0; b < kWindow; b++)
                t[size_t(a * kWindow + b)] = int(std::sqrt(double(a * a + b * b)) * 128.0);
        return t;
    }();
    return table[size_t(i * kWindow + j)];
}

// The eight moves (0x42ea90), in the order they're tried; straight ones
// cost 128 and diagonal ones 181.
constexpr int kMoves[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};

// A turn then a straight line (0x431790's record: a part each).
struct Turn {
    bool clockwise = false;  // round the circle, in (a, b)
    double cx = 0, cy = 0;   // the circle's centre
    double start = 0;        // where on the circle it starts (an angle)
    double arc = 0;          // the turn's length
    double lineX = 0, lineY = 0, heading = 0, line = 0; // the straight part
};

// From (x0, y0) facing `heading`, round a circle on one side until
// facing (x1, y1), then straight there (0x431790). False if (x1, y1) is
// inside the circle.
bool turnTowards(double x0, double y0, double x1, double y1, bool clockwise, double heading, Turn& t) {
    t.clockwise = clockwise;
    const double h = normalize(clockwise ? heading + kHalfPi : heading - kHalfPi);
    t.start = h;
    t.cx = x0 - std::cos(h) * kTurnRadius;
    t.cy = y0 - std::sin(h) * kTurnRadius;
    const double dx = x1 - t.cx, dy = y1 - t.cy;
    const double d = std::sqrt(dy * dy + dx * dx);
    if (below(d, kTurnRadius))
        return false;
    t.line = std::sqrt(d * d - kTurnRadius * kTurnRadius);
    const double a = runtimeAcos(kTurnRadius / d);
    const double phi = std::atan2(dy, dx);
    const double end = clockwise ? a + phi : phi - a;
    t.arc = normalize(clockwise ? h - end : end - h) * kTurnRadius;
    t.lineX = std::cos(end) * kTurnRadius + t.cx;
    t.lineY = std::sin(end) * kTurnRadius + t.cy;
    t.heading = normalize(clockwise ? end - kHalfPi : end + kHalfPi);
    return true;
}

// The shorter turn (0x430df0), and the heading it ends with: its line's,
// or its circle's start angle when it has no line.
Turn bestTurn(double x0, double y0, double x1, double y1, double heading, double& endHeading) {
    Turn turns[2];
    double best = 9999999.0;
    int chosen = 0;
    for (int i = 0; i < 2; i++) {
        const double cost =
            turnTowards(x0, y0, x1, y1, i == 0, heading, turns[i]) ? turns[i].arc + turns[i].line : 99999999.0;
        if (below(cost, best)) {
            best = cost;
            chosen = i;
        }
    }
    const Turn& t = turns[chosen];
    endHeading = t.line > 0 ? t.heading : t.start;
    return t;
}

// Whether the cells under a turn are open: at every 0.2 along it, the
// cell under the point and those under the corners of a square 0.1 wide
// round it (0x430b60, with 0x431e30 for the points).
bool turnClear(const WalkGrid& grid, const Turn& t, bool openThrees, bool allowTwos) {
    const double length = t.line + t.arc;
    if (!below(0, length))
        return true;
    for (double s = 0;;) {
        double x, y;
        if (s < t.arc || s == t.arc) {
            const double angle = (t.clockwise ? -s : s) / kTurnRadius + t.start;
            x = std::cos(angle) * kTurnRadius + t.cx;
            y = std::sin(angle) * kTurnRadius + t.cy;
        } else {
            const double along = s - t.arc;
            x = std::cos(t.heading) * along + t.lineX;
            y = std::sin(t.heading) * along + t.lineY;
        }
        const double half = kWidth * 0.5;
        const int a1 = int(half + x), b1 = int(half + y), a0 = int(x - half), b0 = int(y - half);
        if (a0 < 0 || b0 < 0 || a1 >= WalkGrid::kSize || b1 >= WalkGrid::kSize)
            return false;
        for (const WalkGrid::Cell c : {WalkGrid::Cell{int(x), int(y)}, WalkGrid::Cell{a1, b1}, WalkGrid::Cell{a0, b1},
                                       WalkGrid::Cell{a1, b0}, WalkGrid::Cell{a0, b0}})
            if (grid.blocked(c, openThrees, allowTwos))
                return false;
        s += kTestStep;
        if (!below(s, length))
            return true;
    }
}

} // namespace

int WalkGrid::search(Cell from, Cell to, bool openThrees, bool allowTwos, std::vector<Point>& out) const {
    out.clear();
    // 0x42ef40: neither end blocked.
    if (blocked(to, openThrees, allowTwos) || blocked(from, openThrees, allowTwos))
        return 0;

    // 0x42f1f0: A* over a window of 192 x 192 cells round both ends.
    const int da = to.a - from.a, db = to.b - from.b;
    if (std::abs(da) > 128 || std::abs(db) > 128)
        return -2;
    const int offA = 96 - (std::abs(da) / 2 + std::min(from.a, to.a));
    const int offB = 96 - (std::abs(db) / 2 + std::min(from.b, to.b));
    const int startA = from.a + offA, startB = from.b + offB, goalA = to.a + offA, goalB = to.b + offB;
    struct Node {
        uint32_t g = 0, f = 0;
        bool open = false, seen = false;
        int parent = -1;           // a window cell
        int prev = -1, next = -1;  // the open list's order
    };
    std::vector<Node> nodes(size_t(kWindow * kWindow));
    auto at = [](int a, int b) { return a * kWindow + b; };
    // The open list: nodes join at its end, and the first with the least
    // estimate leaves it.
    int head = at(startA, startB), tail = head;
    Node& start = nodes[size_t(head)];
    start.f = uint32_t(distance(std::abs(to.a - from.a), std::abs(to.b - from.b)));
    start.open = start.seen = true;
    int found = -1;
    while (head >= 0) {
        int best = head;
        for (int n = nodes[size_t(head)].next; n >= 0; n = nodes[size_t(n)].next)
            if (nodes[size_t(n)].f < nodes[size_t(best)].f)
                best = n;
        if (best == at(goalA, goalB)) {
            found = best;
            break;
        }
        Node& b = nodes[size_t(best)];
        (b.prev >= 0 ? nodes[size_t(b.prev)].next : head) = b.next;
        (b.next >= 0 ? nodes[size_t(b.next)].prev : tail) = b.prev;
        b.open = false;
        const int a = best / kWindow, bb = best % kWindow;
        for (int m = 0; m < 8; m++) {
            const int na = a + kMoves[m][0], nb = bb + kMoves[m][1];
            if (na < 0 || na >= kWindow || nb < 0 || nb >= kWindow || blocked({na - offA, nb - offB}, openThrees, allowTwos))
                continue;
            if (na == startA && nb == startB)
                continue;
            const uint32_t g = b.g + (m >= 4 ? 181u : 128u);
            const int i = at(na, nb);
            Node& n = nodes[size_t(i)];
            const bool seen = n.seen;
            if (seen && n.g <= g)
                continue;
            n.seen = true;
            n.parent = best;
            n.g = g;
            n.f = (g + uint32_t(distance(std::abs(goalA - na), std::abs(goalB - nb)))) & 0xffffff;
            if (seen && n.open)
                continue;
            n.open = true;
            n.prev = tail;
            n.next = -1;
            if (tail >= 0)
                nodes[size_t(tail)].next = i;
            tail = i;
            if (head < 0)
                head = i;
        }
    }
    if (found < 0)
        return -1;

    // 0x430420: the cells from the goal back, at their centres; then the
    // start, as given.
    std::vector<Point> points;
    for (int n = found; nodes[size_t(n)].parent >= 0; n = nodes[size_t(n)].parent) {
        if (int(points.size()) >= kMaxPoints)
            return -5;
        points.push_back({double(n / kWindow - offA) + 0.5, double(n % kWindow - offB) + 0.5, -1.0});
    }
    if (points.empty())
        return 0;

    // 0x430690: from the start, each point is dropped when the turn
    // from the last one kept straight to the one after it is clear; each
    // kept point gets the heading the turn into it ends with.
    std::vector<Point> path(points.rbegin(), points.rend());
    Point kept{double(from.a), double(from.b), 0.0};
    bestTurn(kept.a, kept.b, path[0].a, path[0].b, kept.heading, path[0].heading);
    for (size_t i = 0; i + 1 < path.size();) {
        Point& next = path[i + 1];
        double heading;
        const Turn turn = bestTurn(kept.a, kept.b, next.a, next.b, kept.heading, heading);
        if (turnClear(*this, turn, openThrees, allowTwos)) {
            path.erase(path.begin() + std::ptrdiff_t(i));
            path[i].heading = heading;
        } else {
            bestTurn(path[i].a, path[i].b, next.a, next.b, path[i].heading, next.heading);
            kept = path[i];
            i++;
        }
    }
    out.assign(path.rbegin(), path.rend());
    return int(out.size());
}

std::vector<WalkGrid::Cell> WalkGrid::findPath(Cell from, Cell to, bool allowTwos) const {
    if (empty() || !from.valid() || !to.valid() || std::abs(from.a - to.a) + std::abs(from.b - to.b) < 3)
        return {};
    // As 0x440ce0 tries: the grid as it is, accepting at most 20 points;
    // with 3s open, fewer than 32; then with the start and goal cells
    // open too, fewer than 32.
    WalkGrid open = *this;
    open.set(from, 0);
    open.set(to, 0);
    std::vector<Point> points;
    if ((search(from, to, false, allowTwos, points) <= 0 || points.size() > 20) &&
        (search(from, to, true, allowTwos, points) <= 0 || points.size() >= 32) &&
        (open.search(from, to, true, allowTwos, points) <= 0 || points.size() >= 32))
        return {};
    // Goal first, without the start; each a cell, as 0x441089 takes them
    // (truncated). Walking order is from the start.
    std::vector<Cell> path{from};
    for (auto it = points.rbegin(); it != points.rend(); ++it)
        path.push_back({int(it->a), int(it->b)});
    return path;
}

} // namespace reluva::awe
