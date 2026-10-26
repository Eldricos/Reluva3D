// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/walk_grid.h"

#include <algorithm>
#include <cstdlib>
#include <queue>

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

// Every cell a straight line from one centre to the other passes through
// is open (the ends may be, when `openEnds`).
bool WalkGrid::clearLine(Cell from, Cell to, bool openThrees, Cell s, Cell g, bool openEnds) const {
    const int steps = std::max(std::abs(to.a - from.a), std::abs(to.b - from.b)) * 4;
    for (int k = 0; k <= steps; k++) {
        const float t = steps ? float(k) / float(steps) : 0;
        const float fa = float(from.a) + (float(to.a) - float(from.a)) * t + 0.5f;
        const float fb = float(from.b) + (float(to.b) - float(from.b)) * t + 0.5f;
        // The cells under a line about a third of a cell wide, so it
        // doesn't graze a blocked corner.
        for (const float da : {-0.3f, 0.3f})
            for (const float db : {-0.3f, 0.3f}) {
                const Cell c{int(fa + da), int(fb + db)};
                if (openEnds && (c == s || c == g))
                    continue;
                if (blocked(c, openThrees))
                    return false;
            }
    }
    return true;
}

std::vector<WalkGrid::Cell> WalkGrid::search(Cell from, Cell to, bool openThrees, bool openEnds) const {
    auto open = [&](Cell c) {
        if (openEnds && (c == from || c == to))
            return inside(c.a, c.b);
        return !blocked(c, openThrees);
    };
    if (!open(from) || !open(to))
        return {};
    constexpr int kN = kSize * kSize;
    std::vector<int> cost(kN, -1), parent(kN, -1);
    auto index = [](Cell c) { return c.a * kSize + c.b; };
    auto guess = [&](Cell c) {
        const int da = std::abs(c.a - to.a), db = std::abs(c.b - to.b);
        return 10 * std::max(da, db) + 4 * std::min(da, db);
    };
    using Item = std::pair<int, int>; // estimate, index
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    cost[size_t(index(from))] = 0;
    queue.push({guess(from), index(from)});
    while (!queue.empty()) {
        const int i = queue.top().second;
        queue.pop();
        const Cell c{i / kSize, i % kSize};
        if (c == to)
            break;
        for (int da = -1; da <= 1; da++)
            for (int db = -1; db <= 1; db++) {
                const Cell n{c.a + da, c.b + db};
                if ((da == 0 && db == 0) || !inside(n.a, n.b) || !open(n))
                    continue;
                // Diagonally only between two open cells.
                if (da != 0 && db != 0 && (!open({c.a + da, c.b}) || !open({c.a, c.b + db})))
                    continue;
                const int next = cost[size_t(i)] + (da != 0 && db != 0 ? 14 : 10);
                int& known = cost[size_t(index(n))];
                if (known >= 0 && known <= next)
                    continue;
                known = next;
                parent[size_t(index(n))] = i;
                queue.push({next + guess(n), index(n)});
            }
    }
    if (cost[size_t(index(to))] < 0)
        return {};
    std::vector<Cell> cells;
    for (int i = index(to); i >= 0; i = parent[size_t(i)])
        cells.push_back({i / kSize, i % kSize});
    std::reverse(cells.begin(), cells.end());
    // Only the turns: from each point, straight to the furthest one in
    // sight.
    std::vector<Cell> path{cells.front()};
    for (size_t k = 0; k + 1 < cells.size();) {
        size_t far = k + 1;
        for (size_t j = cells.size() - 1; j > k + 1; j--)
            if (clearLine(cells[k], cells[j], openThrees, from, to, openEnds)) {
                far = j;
                break;
            }
        path.push_back(cells[far]);
        k = far;
    }
    return path;
}

std::vector<WalkGrid::Cell> WalkGrid::findPath(Cell from, Cell to) const {
    if (empty() || !from.valid() || !to.valid() || std::abs(from.a - to.a) + std::abs(from.b - to.b) < 3)
        return {};
    std::vector<Cell> path = search(from, to, false, false);
    if (path.empty())
        path = search(from, to, true, false);
    if (path.empty())
        path = search(from, to, true, true);
    return path;
}

} // namespace reluva::awe
