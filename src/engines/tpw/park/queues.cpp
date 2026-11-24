// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The queue's models' shapes read from their own bounds; their angles as
// the saved park's: docs/engines/tpw/games/simcoaster/systems/paths.md#queues.

#include "engines/tpw/park/park.h"

#include <array>
#include <cstdlib>

namespace reluva::tpw {

namespace {

// The four sides of a cell, in the order an angle turns them: east
// towards south (+y), and on.
enum Side { kNorth, kEast, kSouth, kWest };
constexpr int kQuarter = 90;
constexpr std::array<std::array<int, 2>, 4> kStep{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

int turned(int side, int angle) {
    return (side + angle / kQuarter) % 4;
}

// The side of `from` that `to` lies on, or −1 if it isn't beside it.
int sideTowards(const Park::Cell &from, const Park::Cell &to) {
    for (int s = 0; s < 4; ++s)
        if (from.x + kStep[size_t(s)][0] == to.x && from.y + kStep[size_t(s)][1] == to.y)
            return s;
    return -1;
}

// The angle that turns a model's opening on `a` and `b`, unturned, onto
// `x` and `y`; −1 if none does.
int angleFor(int a, int b, int x, int y) {
    for (int angle = 0; angle < 4 * kQuarter; angle += kQuarter)
        if ((turned(a, angle) == x && turned(b, angle) == y) || (turned(a, angle) == y && turned(b, angle) == x))
            return angle;
    return -1;
}

} // namespace

std::vector<Park::QueuePiece> Park::queuePieces(const Queue &queue) {
    std::vector<QueuePiece> out;
    for (size_t i = 0; i < queue.cells.size(); ++i) {
        const Cell &c = queue.cells[i];
        const int inward = sideTowards(c, i == 0 ? queue.entrance : queue.cells[i - 1]);
        const bool last = i + 1 == queue.cells.size();
        const int outward = !last          ? sideTowards(c, queue.cells[i + 1])
                            : queue.atPath ? sideTowards(c, queue.pathEnd)
                                           : -1;
        QueuePiece piece{c, QueueModel::kStraight, 0};
        if (inward < 0) {
            out.push_back(piece);
            continue;
        }
        if (outward < 0) {
            piece.model = QueueModel::kDeadEnd;
            piece.angle = angleFor(kSouth, kSouth, inward, inward);
        } else if ((inward + 2) % 4 == outward) {
            // A straight: the one by the path its posts towards the path.
            piece.model = last ? QueueModel::kJoin : QueueModel::kStraight;
            piece.angle = last ? angleFor(kNorth, kNorth, outward, outward) : (inward % 2 == 0 ? 0 : kQuarter);
        } else {
            piece.angle = angleFor(kEast, kSouth, inward, outward);
            const bool eastOut = turned(kEast, piece.angle) == outward;
            if (last)
                piece.model = eastOut ? QueueModel::kJoinBendEast : QueueModel::kJoinBendSouth;
            else
                piece.model = eastOut ? QueueModel::kBend : QueueModel::kBendOther;
        }
        out.push_back(piece);
    }
    return out;
}

std::vector<Park::QueuePiece> Park::queuePieces() const {
    std::vector<QueuePiece> out;
    for (const Queue &q : _queues) {
        const std::vector<QueuePiece> pieces = queuePieces(q);
        out.insert(out.end(), pieces.begin(), pieces.end());
    }
    return out;
}

// From the entrance, queue cell to queue cell, to the path.
Park::Queue Park::followQueue(const Cell &entrance) const {
    Queue q;
    q.entrance = entrance;
    Cell at = entrance;
    for (;;) {
        bool on = false;
        for (const auto &d : kStep) {
            const Cell next{at.x + d[0], at.y + d[1]};
            if (ground(next) != Ground::kQueue)
                continue;
            bool again = false;
            for (const Cell &c : q.cells)
                again = again || c == next;
            if (again)
                continue;
            q.cells.push_back(next);
            at = next;
            on = true;
            break;
        }
        if (!on)
            break;
    }
    if (!q.cells.empty())
        for (const auto &d : kStep) {
            const Cell next{at.x + d[0], at.y + d[1]};
            if (isPath(next)) {
                q.atPath = true;
                q.pathEnd = next;
                break;
            }
        }
    return q;
}

int Park::queueFrom(const Cell &cell) const {
    for (size_t i = 0; i < _queues.size(); ++i) {
        const Queue &q = _queues[i];
        if (q.atPath)
            continue;
        if ((q.cells.empty() && q.entrance == cell) || (!q.cells.empty() && q.cells.back() == cell))
            return int(i);
    }
    return -1;
}

Park::Queue Park::queueWith(int index, const std::vector<Cell> &run) const {
    Queue q = _queues[size_t(index)];
    for (size_t i = 1; i < run.size() && !q.atPath; ++i) {
        const Cell &c = run[i];
        if (isPath(c)) {
            q.atPath = true;
            q.pathEnd = c;
        } else if (ground(c) == Ground::kOpen) {
            q.cells.push_back(c);
        } else {
            break;
        }
    }
    return q;
}

int Park::layQueue(int index, const std::vector<Cell> &run) {
    if (index < 0 || size_t(index) >= _queues.size())
        return 0;
    const Queue wanted = queueWith(index, run);
    Queue &q = _queues[size_t(index)];
    int laid = 0;
    for (size_t i = q.cells.size(); i < wanted.cells.size(); ++i) {
        if (_cash < _queueCost)
            return laid;
        const Cell &c = wanted.cells[i];
        _ground[size_t(c.y) * size_t(_mapWidth) + size_t(c.x)] = Ground::kQueue;
        _cash -= _queueCost;
        q.cells.push_back(c);
        ++laid;
    }
    q.atPath = wanted.atPath;
    q.pathEnd = wanted.pathEnd;
    return laid;
}

} // namespace reluva::tpw
