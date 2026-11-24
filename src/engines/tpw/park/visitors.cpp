// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Visitors about the park: in, along its paths, into its rides' queues,
// on and off: docs/engines/tpw/games/simcoaster/systems/visitors.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace reluva::tpw {

namespace {

// How far apart visitors stand in a queue, in units: a cell holds kVisitorsPerQueueCell.
constexpr float kQueueSpacing = float(Park::kCellUnits) / float(Park::kVisitorsPerQueueCell);
// One in this many visitors passing a queue's end joins it.
constexpr uint32_t kJoinOneIn = 2;
// One in this many of a visitor's turns, the ride they last rode may be joined again.
constexpr uint32_t kForgetOneIn = 8;

constexpr std::array<Park::Cell, 4> kSteps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

// A number for a visitor's next choice, the same each run.
uint32_t roll(Park::Visitor &v) {
    const uint32_t r = (uint32_t(v.id) * 2654435761u) ^ (++v.choices * 40503u);
    return (r ^ r >> 15) * 2246822519u >> 13;
}

// Walks a visitor towards a point; whether they got there, `left` what's left of their walk.
bool walkTo(Park::Visitor &v, float x, float z, float &left) {
    const float dx = x - v.x;
    const float dz = z - v.z;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d > 0) {
        v.headingX = dx / d;
        v.headingZ = dz / d;
    }
    if (d > left) {
        v.x += dx / d * left;
        v.z += dz / d * left;
        v.walked += left;
        left = 0;
        return false;
    }
    v.x = x;
    v.z = z;
    v.walked += d;
    left -= d;
    return true;
}

// A point `s` units along a route of points.
gfx::Vec3 pointAlong(const std::vector<gfx::Vec3> &route, float s) {
    for (size_t i = 1; i < route.size(); ++i) {
        const float dx = route[i][0] - route[i - 1][0];
        const float dz = route[i][2] - route[i - 1][2];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (s <= d && d > 0)
            return {route[i - 1][0] + dx * s / d, 0, route[i - 1][2] + dz * s / d};
        s -= d;
    }
    return route.back();
}

float lengthOf(const std::vector<gfx::Vec3> &route) {
    float total = 0;
    for (size_t i = 1; i < route.size(); ++i)
        total += std::hypot(route[i][0] - route[i - 1][0], route[i][2] - route[i - 1][2]);
    return total;
}

} // namespace

// A queue's attraction is the object whose entrance is its.
void Park::linkQueue(size_t object, const Footprint &footprint) {
    if (!footprint.hasEntrance)
        return;
    for (Queue &q : _queues) {
        if (!(q.entrance == footprint.entrance))
            continue;
        q.object = int(object);
        q.hasExit = footprint.hasExit;
        q.exit = footprint.exit;
        if (const Attractions::Attraction *a = _attractions.find(_objects[object].id))
            q.fare = a->fare;
        return;
    }
}

int Park::queueOf(size_t object) const {
    for (size_t i = 0; i < _queues.size(); ++i)
        if (_queues[i].object == int(object))
            return int(i);
    return -1;
}

const Park::Visitor *Park::visitor(int id) const {
    for (const Visitor &v : _people)
        if (v.id == id)
            return &v;
    return nullptr;
}

// From the path to the entrance: the path's cell, the queue's from its
// far end, the entrance.
std::vector<gfx::Vec3> Park::queueRoute(const Queue &queue) const {
    std::vector<gfx::Vec3> route{cellCentre(queue.pathEnd)};
    for (auto c = queue.cells.rbegin(); c != queue.cells.rend(); ++c)
        route.push_back(cellCentre(*c));
    route.push_back(cellCentre(queue.entrance));
    return route;
}

int Park::waitingAt(int queue) const {
    if (queue < 0 || size_t(queue) >= _queues.size() || _queues[size_t(queue)].line.empty())
        return 0;
    const Queue &q = _queues[size_t(queue)];
    const Visitor *v = visitor(q.line.front());
    if (!v || v->doing != Doing::kQueueing || v->along < lengthOf(queueRoute(q)))
        return 0;
    return v->id;
}

bool Park::board(int queue, int id) {
    if (waitingAt(queue) != id || id == 0)
        return false;
    Queue &q = _queues[size_t(queue)];
    q.line.erase(q.line.begin());
    for (Visitor &v : _people)
        if (v.id == id) {
            v.doing = Doing::kRiding;
            v.headingX = v.headingZ = 0;
            ++v.rides;
            _cash += q.fare;
        }
    return true;
}

// Off at the exit, or the entrance for a ride without one, then to the
// path's nearest cell (guessed: the game's visitors may want a path from
// the exit).
bool Park::letOff(int queue, int id) {
    if (queue < 0 || size_t(queue) >= _queues.size())
        return false;
    const Queue &q = _queues[size_t(queue)];
    for (Visitor &v : _people) {
        if (v.id != id || v.doing != Doing::kRiding || v.queue != queue)
            continue;
        const Cell off = q.hasExit ? q.exit : q.entrance;
        const gfx::Vec3 at = cellCentre(off);
        v.x = at[0];
        v.z = at[2];
        v.doing = Doing::kLeaving;
        v.lastQueue = queue;
        v.queue = -1;
        v.cell = off;
        int best = INT_MAX;
        for (int y = 0; y < _mapHeight; ++y)
            for (int x = 0; x < _mapWidth; ++x) {
                const int d = std::abs(x - off.x) + std::abs(y - off.y);
                if (d < best && isPath({x, y})) {
                    best = d;
                    v.cell = {x, y};
                }
            }
        v.from = v.cell;
        return true;
    }
    return false;
}

// At a path cell's middle: into a ride's queue that ends by it, sometimes,
// if there's room and they didn't just ride it; else on along the path,
// not back the way they came unless it's the only way.
void Park::chooseWay(Visitor &v) {
    for (size_t i = 0; i < _queues.size(); ++i) {
        Queue &q = _queues[i];
        if (!q.atPath || !(q.pathEnd == v.cell) || q.object < 0 || int(i) == v.lastQueue)
            continue;
        const size_t room = (q.cells.size() + 1) * size_t(kVisitorsPerQueueCell);
        if (q.line.size() >= room || roll(v) % kJoinOneIn != 0)
            continue;
        q.line.push_back(v.id);
        v.doing = Doing::kQueueing;
        v.queue = int(i);
        v.along = 0;
        return;
    }
    std::array<Cell, 4> ways{};
    size_t count = 0;
    for (const Cell &step : kSteps) {
        const Cell next{v.cell.x + step.x, v.cell.y + step.y};
        if (isPath(next) && !(next == v.from))
            ways[count++] = next;
    }
    if (count == 0 && isPath(v.from) && !(v.from == v.cell))
        ways[count++] = v.from;
    if (count == 0)
        return;
    v.from = v.cell;
    v.cell = ways[roll(v) % count];
    // The ride just ridden may be wanted again after a while: a chance at each turn.
    if (v.lastQueue >= 0 && roll(v) % kForgetOneIn == 0)
        v.lastQueue = -1;
}

void Park::moveVisitor(Visitor &v, float left) {
    // Each pass moves them on, or stops: a few at most for a step's walk.
    for (int pass = 0; pass < 16 && left > 0; ++pass) {
        switch (v.doing) {
        case Doing::kComing: {
            if (v.leg >= _wayIn.size()) {
                // On the path inside the entrance, as far as the way in goes.
                v.doing = Doing::kWalking;
                v.cell = {int(std::floor(v.x / kCellUnits)), int(std::floor(v.z / kCellUnits))};
                v.from = v.cell;
                if (!isPath(v.cell)) {
                    v.headingX = v.headingZ = 0;
                    return;
                }
                chooseWay(v);
                break;
            }
            if (!walkTo(v, _wayIn[v.leg][0], _wayIn[v.leg][2], left))
                return;
            if (v.leg == kBoothLeg && !v.paid) {
                v.paid = true;
                _cash += _admissionFee;
            }
            if (v.leg == kEntranceLeg && !v.inPark) {
                v.inPark = true;
                ++_visitors;
            }
            ++v.leg;
            break;
        }
        case Doing::kWalking:
        case Doing::kLeaving: {
            const gfx::Vec3 to = cellCentre(v.cell);
            if (!walkTo(v, to[0], to[2], left))
                return;
            v.doing = Doing::kWalking;
            const Cell was = v.cell;
            chooseWay(v);
            if (v.doing == Doing::kWalking && v.cell == was) {
                v.headingX = v.headingZ = 0;
                return;
            }
            break;
        }
        case Doing::kQueueing: {
            // Up the queue to behind the one in front, or to its entrance.
            const Queue &q = _queues[size_t(v.queue)];
            const std::vector<gfx::Vec3> route = queueRoute(q);
            const size_t place = size_t(std::find(q.line.begin(), q.line.end(), v.id) - q.line.begin());
            const float stop = std::max(0.0f, lengthOf(route) - float(place) * kQueueSpacing);
            if (v.along >= stop) {
                v.headingX = v.headingZ = 0;
                return;
            }
            const float step = std::min(left, stop - v.along);
            const gfx::Vec3 from = pointAlong(route, v.along);
            v.along += step;
            const gfx::Vec3 to = pointAlong(route, v.along);
            const float dx = to[0] - from[0];
            const float dz = to[2] - from[2];
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d > 0) {
                v.headingX = dx / d;
                v.headingZ = dz / d;
            }
            v.x = to[0];
            v.z = to[2];
            v.walked += step;
            left -= step;
            break;
        }
        case Doing::kRiding:
            return;
        }
    }
}

} // namespace reluva::tpw
