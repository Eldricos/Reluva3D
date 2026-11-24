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
#include <utility>

namespace reluva::tpw {

namespace {

// How far apart visitors stand in a queue, in units: a cell holds kVisitorsPerQueueCell.
constexpr float kQueueSpacing = float(Park::kCellUnits) / float(Park::kVisitorsPerQueueCell);
// One in this many of a visitor's turns, the attraction they last used may be chosen again.
constexpr uint32_t kForgetOneIn = 8;
// How fast a need grows, a day: from nothing to its most over a stay of
// ExitLevel's 400 days (guessed); and how much a need past
// VisitorRules::needy takes off their happiness a day.
constexpr float kNeedPerDay = 0.25f;
constexpr float kUnhappyPerDay = 1;
// What a ride or sideshow is worth choosing for itself, before its
// excitement's match, and what a cell's walk takes off (guessed).
constexpr float kRideWorth = 20;
constexpr float kCellCost = 0.5f;
// What a need counts for in choosing what meets it: half its level past
// this, so that it isn't met over and over.
constexpr float kNeedWorth = 0.5f;
constexpr float kNeedFloor = 30;
// Making for home, not for an attraction.
constexpr int kHome = -2;

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

// A queue's attraction is the object whose entrance is its; one without
// a queue gets a kiosk, if visitors can choose it.
void Park::linkQueue(size_t object, const Footprint &footprint) {
    if (!footprint.hasEntrance)
        return;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    Queue *q = nullptr;
    for (Queue &each : _queues)
        if (each.entrance == footprint.entrance)
            q = &each;
    if (!q) {
        if (!a || a->hasQueue || !a->choosable)
            return;
        _queues.emplace_back();
        q = &_queues.back();
        q->entrance = footprint.entrance;
    }
    q->object = int(object);
    q->hasExit = footprint.hasExit;
    q->exit = footprint.exit;
    if (a) {
        q->fare = a->fare;
        q->kiosk = !a->hasQueue;
    }
    findKioskPaths();
}

// A kiosk's end at the path: a path cell beside its entrance.
void Park::findKioskPaths() {
    for (Queue &q : _queues) {
        if (!q.kiosk)
            continue;
        q.atPath = false;
        for (const Cell &step : kSteps) {
            const Cell next{q.entrance.x + step.x, q.entrance.y + step.y};
            if (isPath(next)) {
                q.atPath = true;
                q.pathEnd = next;
                break;
            }
        }
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

// Each comes as one of a generation by the settings' shares, a type of it
// by chance; their money their type's, give or take StartingCashVarPc.
void Park::addVisitor() {
    Visitor v;
    v.id = _nextId++;
    const uint32_t r = uint32_t(v.id) * 2654435761u;
    const int share = int((r >> 8) % 100);
    int generation = 2;
    for (int g = 0, sum = 0; g < 3; ++g) {
        sum += _rules.generationPercent[size_t(g)];
        if (share < sum) {
            generation = g;
            break;
        }
    }
    std::vector<int> ofGeneration;
    for (size_t i = 0; i < _visitorTypes.size(); ++i)
        if (_visitorTypes[i].generation == generation)
            ofGeneration.push_back(int(i));
    if (ofGeneration.empty())
        for (size_t i = 0; i < _visitorTypes.size(); ++i)
            ofGeneration.push_back(int(i));
    if (!ofGeneration.empty()) {
        v.type = ofGeneration[(r >> 16) % ofGeneration.size()];
        const VisitorType &type = _visitorTypes[size_t(v.type)];
        v.generation = type.generation;
        v.speed = type.speed;
        const int spread = std::max(0, _rules.cashSpreadPercent);
        const int percent = 100 - spread + int((r >> 4) % uint32_t(2 * spread + 1));
        v.cash = type.cash * percent / 100;
    }
    v.look = r >> 20;
    const int days = std::max(0, _rules.exitSpread);
    v.daysLeft = float(_rules.exitDays - days + int((r >> 12) % uint32_t(2 * days + 1)));
    v.x = _wayIn[0][0];
    v.z = _wayIn[0][2];
    v.leg = 1;
    _people.push_back(std::move(v));
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
            const int fare = std::min(q.fare, std::max(0, v.cash));
            v.cash -= fare;
            _cash += fare;
        }
    return true;
}

// Off at the exit, or the entrance for one without, then to the path's
// nearest cell (guessed: the game's visitors may want a path from the
// exit). What it did to them is done as they come off: a ride's
// happiness by how near its excitement was to what they like, a shop's
// effects.
bool Park::letOff(int queue, int id) {
    if (queue < 0 || size_t(queue) >= _queues.size())
        return false;
    const Queue &q = _queues[size_t(queue)];
    for (Visitor &v : _people) {
        if (v.id != id || v.doing != Doing::kRiding || v.queue != queue)
            continue;
        const Attractions::Attraction *a = q.object >= 0 ? _attractions.find(_objects[size_t(q.object)].id) : nullptr;
        if (a) {
            if (a->excitement >= 0 && v.type < int(_visitorTypes.size())) {
                const int miss = std::abs(_visitorTypes[size_t(v.type)].excitement - a->excitement);
                for (size_t m = 0; m < _rules.rideMatch.size(); ++m)
                    if (miss <= _rules.rideMatch[m]) {
                        v.happiness += float(_rules.rideHappiness[m]);
                        break;
                    }
            }
            v.happiness += float(a->happinessEffect);
            v.hunger -= float(a->hungerEffect);
            v.thirst -= float(a->thirstEffect);
            v.toilet = a->relief ? 0 : v.toilet + float(a->toiletEffect);
        }
        v.happiness = std::clamp(v.happiness, 0.0f, 100.0f);
        v.hunger = std::clamp(v.hunger, 0.0f, 100.0f);
        v.thirst = std::clamp(v.thirst, 0.0f, 100.0f);
        v.toilet = std::clamp(v.toilet, 0.0f, 100.0f);
        v.bored = 0;
        ++v.rides;
        const Cell off = q.hasExit ? q.exit : q.entrance;
        const gfx::Vec3 at = cellCentre(off);
        v.x = at[0];
        v.z = at[2];
        v.doing = Doing::kLeaving;
        v.lastQueue = queue;
        v.queue = -1;
        v.target = -1;
        v.route.clear();
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

// Needs grow, and those past bearing make them unhappier; time passes
// towards their going home. Bored, unhappy or out of time, they go,
// once they're about the paths.
void Park::useUp(Visitor &v, float days) {
    if (!v.inPark || v.doing == Doing::kGoingHome)
        return;
    v.hunger = std::min(100.0f, v.hunger + kNeedPerDay * days);
    v.thirst = std::min(100.0f, v.thirst + kNeedPerDay * days);
    v.toilet = std::min(100.0f, v.toilet + kNeedPerDay * days);
    const float needy = float(_rules.needy);
    const int needs = int(v.hunger >= needy) + int(v.thirst >= needy) + int(v.toilet >= needy);
    v.happiness = std::max(0.0f, v.happiness - float(needs) * kUnhappyPerDay * days);
    v.daysLeft -= days;
    if (v.doing == Doing::kWalking || v.doing == Doing::kLeaving)
        v.bored += days;
    // Out of time or happiness in a queue, they leave it, back to the path.
    if (v.doing == Doing::kQueueing && (v.daysLeft <= 0 || v.happiness <= 0)) {
        std::vector<int> &line = _queues[size_t(v.queue)].line;
        line.erase(std::remove(line.begin(), line.end(), v.id), line.end());
        v.doing = Doing::kLeaving;
        v.cell = v.from = _queues[size_t(v.queue)].pathEnd;
        v.queue = -1;
    }
    const int boredom = v.type < int(_visitorTypes.size()) ? _visitorTypes[size_t(v.type)].boredom : 50;
    if (v.doing == Doing::kWalking && v.target != kHome &&
        (v.daysLeft <= 0 || v.happiness <= 0 || v.bored > float(boredom)))
        goHome(v);
}

// Back along the path to where the way in ends, then out along it.
void Park::goHome(Visitor &v) {
    v.target = kHome;
    const gfx::Vec3 &end = _wayIn.back();
    v.route = routeTo(v.cell, {int(std::floor(end[0] / kCellUnits)), int(std::floor(end[2] / kCellUnits))});
}

// The path cells from one to another, the next last, the first left out;
// empty if they're the same or the path doesn't join them.
std::vector<Park::Cell> Park::routeTo(const Cell &from, const Cell &to) const {
    if (from == to || !isPath(from) || !isPath(to))
        return {};
    const auto index = [this](const Cell &c) { return size_t(c.y) * size_t(_mapWidth) + size_t(c.x); };
    std::vector<int> cameFrom(size_t(_mapWidth) * size_t(_mapHeight), -1);
    std::vector<Cell> frontier{from};
    cameFrom[index(from)] = int(index(from));
    for (size_t i = 0; i < frontier.size() && cameFrom[index(to)] < 0; ++i)
        for (const Cell &step : kSteps) {
            const Cell next{frontier[i].x + step.x, frontier[i].y + step.y};
            if (isPath(next) && cameFrom[index(next)] < 0) {
                cameFrom[index(next)] = int(index(frontier[i]));
                frontier.push_back(next);
            }
        }
    if (cameFrom[index(to)] < 0)
        return {};
    std::vector<Cell> route;
    for (Cell c = to; !(c == from);) {
        route.push_back(c);
        const int back = cameFrom[index(c)];
        c = {back % _mapWidth, back / _mapWidth};
    }
    return route;
}

// What to make for: of the attractions they can choose, can reach by the
// path, can pay for and have room, the one scoring best, if any scores
// above nothing. A ride or sideshow scores for itself and for how near
// its excitement is to what they like; a shop or toilet for the needs it
// meets, or else for its happiness; each less for the walk and the
// queue, by the settings' weights.
void Park::chooseTarget(Visitor &v) {
    v.target = -1;
    v.route.clear();
    if (!isPath(v.cell))
        return;
    const auto index = [this](const Cell &c) { return size_t(c.y) * size_t(_mapWidth) + size_t(c.x); };
    std::vector<int> distance(size_t(_mapWidth) * size_t(_mapHeight), -1);
    std::vector<Cell> frontier{v.cell};
    distance[index(v.cell)] = 0;
    for (size_t i = 0; i < frontier.size(); ++i)
        for (const Cell &step : kSteps) {
            const Cell next{frontier[i].x + step.x, frontier[i].y + step.y};
            if (isPath(next) && distance[index(next)] < 0) {
                distance[index(next)] = distance[index(frontier[i])] + 1;
                frontier.push_back(next);
            }
        }
    const VisitorType *type = v.type < int(_visitorTypes.size()) ? &_visitorTypes[size_t(v.type)] : nullptr;
    float best = 0;
    for (size_t i = 0; i < _queues.size(); ++i) {
        const Queue &q = _queues[i];
        if (!q.atPath || q.object < 0 || int(i) == v.lastQueue || q.fare > v.cash || !isPath(q.pathEnd))
            continue;
        const int d = distance[index(q.pathEnd)];
        const size_t room = (q.cells.size() + 1) * size_t(kVisitorsPerQueueCell);
        const Attractions::Attraction *a = _attractions.find(_objects[size_t(q.object)].id);
        if (d < 0 || q.line.size() >= room || !a || !a->choosable)
            continue;
        float score = 0;
        if (a->excitement >= 0 && type)
            score += kRideWorth + float(_rules.excitementWeight) *
                                      float(_rules.rideMatch.back() - std::abs(type->excitement - a->excitement));
        const auto need = [](float level) { return std::max(0.0f, level - kNeedFloor) * kNeedWorth; };
        const bool meetsNeed = a->thirstEffect > 0 || a->hungerEffect > 0 || a->relief;
        if (a->thirstEffect > 0)
            score += float(_rules.thirstWeight) * need(v.thirst);
        if (a->hungerEffect > 0)
            score += float(_rules.hungerWeight) * need(v.hunger);
        if (a->relief)
            score += float(_rules.toiletWeight) * need(v.toilet);
        // A shop meeting no need (balloons, gifts) for the happiness it gives.
        if (!meetsNeed)
            score += float(a->happinessEffect);
        score -= float(_rules.distanceWeight) * float(d) * kCellCost + float(_rules.queueWeight) * float(q.line.size());
        if (score > best) {
            best = score;
            v.target = int(i);
        }
    }
    if (v.target >= 0)
        v.route = routeTo(v.cell, _queues[size_t(v.target)].pathEnd);
}

// At a path cell's middle: home, or into the queue of the attraction they
// make for once at its end, or on towards it; else on along the path by
// chance, not back the way they came unless it's the only way.
void Park::chooseWay(Visitor &v) {
    if (v.target == kHome) {
        if (v.route.empty()) {
            v.doing = Doing::kGoingHome;
            v.leg = _wayIn.size() - 1;
            return;
        }
        v.from = v.cell;
        v.cell = v.route.back();
        v.route.pop_back();
        return;
    }
    if (v.target < 0)
        chooseTarget(v);
    if (v.target >= 0) {
        Queue &q = _queues[size_t(v.target)];
        const size_t room = (q.cells.size() + 1) * size_t(kVisitorsPerQueueCell);
        if (q.atPath && q.pathEnd == v.cell) {
            const int target = std::exchange(v.target, -1);
            if (q.line.size() < room && v.cash >= q.fare) {
                q.line.push_back(v.id);
                v.doing = Doing::kQueueing;
                v.queue = target;
                v.along = 0;
                return;
            }
        } else if (!v.route.empty()) {
            v.from = v.cell;
            v.cell = v.route.back();
            v.route.pop_back();
            return;
        } else {
            v.target = -1;
        }
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
    // What they last used may be wanted again after a while: a chance at each turn.
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
                const int fee = std::min(_admissionFee, std::max(0, v.cash));
                v.cash -= fee;
                _cash += fee;
            }
            if (v.leg == kEntranceLeg && !v.inPark) {
                v.inPark = true;
                ++_visitors;
            }
            ++v.leg;
            break;
        }
        case Doing::kGoingHome: {
            if (!walkTo(v, _wayIn[v.leg][0], _wayIn[v.leg][2], left))
                return;
            if (v.leg == kEntranceLeg && v.inPark) {
                v.inPark = false;
                --_visitors;
            }
            if (v.leg == 0) {
                v.gone = true;
                return;
            }
            --v.leg;
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
