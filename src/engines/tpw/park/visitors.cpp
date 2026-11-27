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
// What a costume shop sells (UsageInfo.AppearanceEffect).
constexpr int kCostume = 2;
// An opinion of a price, 0 to 100, against happiness: this much a point.
constexpr float kOpinionPerHappiness = 10;
// Who comes of each generation, by the settings' numbering of people
// (PersonSpriteUmbrellaModifiers, whose comments say who they are): the
// boy, the girl with pigtails, the girl with a cap, the boy with a cap;
// the fat bloke, the tall woman; the old biddy, the old bloke.
constexpr std::array<std::array<int, 4>, 3> kOfGeneration{{{0, 1, 2, 3}, {4, 5, -1, -1}, {8, 9, -1, -1}}};
constexpr std::array<bool, 10> kFemale{false, true, true, false, false, true, false, false, true, false};
// How long the weather holds before it's rolled again, in days (guessed).
constexpr uint32_t kWeatherSpellDays = 12;
// How far apart thoughts come, about, and how far a visitor's mood
// carries: litter or a stink bomb within it, in cells (guessed).
constexpr uint64_t kThoughtEveryMillis = 20000;
constexpr float kNearCells = 1.5f;
// How a stink bomb lasts, and how far it reaches, in cells (guessed).
constexpr uint64_t kStinkBombMillis = 10000;
constexpr float kStinkBombCells = 2;
// How far a bin is reached, in cells (guessed).
constexpr int kBinCells = 2;
// Sickness wears off a day, and litter carried is dropped on a path
// cell one time in this many over its LitterEffect (guessed).
constexpr float kSicknessPerDay = 0.1f;
constexpr int kLitterChance = 400;
// The litter sheet's frames: litter, and vomit (guessed by their look).
constexpr int kLitterFrames = 10;
constexpr std::array<int, 2> kVomitFrames{10, 11};
// A prank of litter drops this much.
constexpr int kPrankLitter = 3;

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

Park::PriceOpinion Park::priceOpinion(int price, int fair) const {
    const float f = float(std::max(1, fair));
    if (float(price) <= f * _rules.cheapPrice)
        return PriceOpinion::kCheap;
    if (float(price) <= f * _rules.averagePrice)
        return PriceOpinion::kAboutRight;
    if (float(price) < f * _rules.expensivePrice)
        return PriceOpinion::kTooHigh;
    return PriceOpinion::kMuchTooHigh;
}

int Park::fairEntryFee() const {
    int excitement = 0;
    for (const PlacedObject &o : _objects)
        if (const Attractions::Attraction *a = _attractions.find(o.id))
            excitement += std::max(0, a->excitement);
    return std::max(_rules.minimumEntryFee, excitement / _rules.excitementToCost);
}

void Park::setFare(int queue, int fare) {
    if (queue >= 0 && size_t(queue) < _queues.size())
        _queues[size_t(queue)].fare = std::max(0, fare);
}

// A price's opinion as a change of happiness: about right none, cheaper
// more, dearer less, by its OpinionFor... value's distance from about
// right's, a tenth of it (guessed).
float Park::priceFeeling(PriceOpinion opinion) const {
    return float(_rules.priceOpinion[size_t(opinion)] - _rules.priceOpinion[size_t(PriceOpinion::kAboutRight)]) /
           kOpinionPerHappiness;
}

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
    // Who they are, of their generation; a missing slot counts again.
    const auto &people = kOfGeneration[size_t(std::clamp(v.generation, 0, 2))];
    const int count = people[2] < 0 ? 2 : 4;
    v.person = people[size_t((r >> 20) % uint32_t(count))];
    v.female = kFemale[size_t(v.person)];
    v.prankster = int((r >> 3) % 100) < _rules.prankersPercent;
    v.nextThought = _millis + kThoughtEveryMillis / 2 + (r >> 6) % kThoughtEveryMillis;
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
    // None get on a ride broken, closed, or being upgraded.
    if (q.object >= 0 && (_objects[size_t(q.object)].broken || !_objects[size_t(q.object)].open ||
                          _objects[size_t(q.object)].upgrading > 0))
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
            v.spent += fare;
            const Attractions::Attraction *taker =
                q.object >= 0 ? _attractions.find(_objects[size_t(q.object)].id) : nullptr;
            const Attractions::Kind kind = taker ? taker->kind : Attractions::Kind::kRide;
            // Sideshows' prizes aren't played for yet: none are won.
            if (kind == Attractions::Kind::kRide)
                ++v.ridden;
            else if (kind == Attractions::Kind::kShop)
                ++v.purchases;
            else if (kind == Attractions::Kind::kSideshow)
                ++v.sideshowsPlayed;
            book(kind == Attractions::Kind::kShop       ? Ledger::kShops
                 : kind == Attractions::Kind::kSideshow ? Ledger::kSideshows
                 : kind == Attractions::Kind::kRide     ? Ledger::kRides
                                                        : Ledger::kFacilities,
                 fare);
            if (taker && taker->shopType >= 0 && size_t(taker->shopType) < _sold.size()) {
                ++_sold[size_t(taker->shopType)];
                _shopProfit[size_t(taker->shopType)] += fare - taker->costOfGoods;
            }
            if (q.object >= 0) {
                PlacedObject &o = _objects[size_t(q.object)];
                ++_usesById[o.id];
                ++o.users;
                o.takings += fare;
                if (const Attractions::Attraction *a = _attractions.find(o.id)) {
                    o.goods += a->costOfGoods;
                    book(Ledger::kRunning, a->costOfGoods);
                }
                wear(size_t(q.object));
            }
            if (const Attractions::Attraction *a =
                    q.object >= 0 ? _attractions.find(_objects[size_t(q.object)].id) : nullptr)
                v.happiness =
                    std::clamp(v.happiness + priceFeeling(priceOpinion(q.fare, a->expectedPrice)), 0.0f, 100.0f);
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
                bool liked = false;
                for (size_t m = 0; m < _rules.rideMatch.size(); ++m)
                    if (miss <= _rules.rideMatch[m]) {
                        v.happiness += float(_rules.rideHappiness[m]);
                        liked = true;
                        break;
                    }
                think(v, liked ? Thought::kLiked : Thought::kDisliked);
                // Excitement makes them queasy: RideVomitDivisor's comment.
                v.sickness += float(a->excitement / _rules.vomitDivisor);
            }
            v.sickness += float(a->vomitEffect);
            v.carrying = std::max(v.carrying, a->litterEffect);
            if (a->appearance)
                v.appearance = a->appearance;
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
    v.sickness = std::max(0.0f, v.sickness - kSicknessPerDay * days);
    if (v.doing != Doing::kRiding && _millis >= v.nextThought) {
        think(v, howTheyAre(v));
        v.nextThought = _millis + kThoughtEveryMillis;
    }
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
        if (_objects[size_t(q.object)].broken || !_objects[size_t(q.object)].open ||
            _objects[size_t(q.object)].upgrading > 0)
            continue;
        // Not the other's toilet, nor what they already carry; costumes are
        // children's, the only costumed sprites being of a child (guessed).
        if ((a->gender == 1 && v.female) || (a->gender == 2 && !v.female) ||
            (a->appearance && a->appearance == v.appearance) || (a->appearance == kCostume && v.generation != 0))
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
        // Much too dear isn't thought of; dearer or cheaper, it's less or
        // more wanted, by their opinion of it against about right's.
        const PriceOpinion opinion = priceOpinion(q.fare, a->expectedPrice);
        if (opinion == PriceOpinion::kMuchTooHigh && q.fare > 0)
            continue;
        if (score > 0 && q.fare > 0)
            score = score * float(_rules.priceOpinion[size_t(opinion)] + 50) / 100;
        // The weather and their generation make it more or less wanted
        // (its WeatherModifier and GenerationModifier percentages).
        if (score > 0)
            score = score * float(a->weatherPercent[size_t(_weather)]) / 100 *
                    float(a->generationPercent[size_t(std::clamp(v.generation, 0, 2))]) / 100;
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
                // Below MinimumEntryFee they pay happily; much too dear,
                // they turn back; otherwise they pay, thinking of it.
                const PriceOpinion opinion = _admissionFee < _rules.minimumEntryFee
                                                 ? PriceOpinion::kAboutRight
                                                 : priceOpinion(_admissionFee, fairEntryFee());
                if (opinion == PriceOpinion::kMuchTooHigh) {
                    think(v, Thought::kAngry);
                    v.doing = Doing::kGoingHome;
                    v.leg = kBoothLeg;
                    break;
                }
                v.paid = true;
                const int fee = std::min(_admissionFee, std::max(0, v.cash));
                v.cash -= fee;
                v.spent += fee;
                v.arrived = _millis;
                book(Ledger::kGate, fee);
                ++_totalVisitors;
                if (v.generation >= 0 && v.generation < 3)
                    ++_generationVisitors[size_t(v.generation)];
                ++_yearVisitors;
                v.happiness = std::clamp(v.happiness + priceFeeling(opinion), 0.0f, 100.0f);
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
            dropLitter(v);
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

void Park::think(Visitor &v, Thought thought) const {
    if (thought == Thought::kNone)
        return;
    v.thought = thought;
    v.thoughtUntil = _millis + kThoughtMillis;
}

// What weighs on them most, first: sickness, needs, the weather, mess,
// boredom; then how happy they are. THOUGHTS.str's order of them is
// kept where it can be (guessed: the game's own isn't known).
Park::Thought Park::howTheyAre(const Visitor &v) const {
    const float needy = float(_rules.needy);
    const bool outside = v.doing != Doing::kRiding;
    const bool badWeather = _weather == Weather::kRain || _weather == Weather::kStorm || _weather == Weather::kSnow;
    const int boredom = v.type < int(_visitorTypes.size()) ? _visitorTypes[size_t(v.type)].boredom : 50;
    if (v.sickness >= float(_rules.sick))
        return Thought::kSick;
    if (v.toilet >= needy)
        return Thought::kToilet;
    if (v.hunger >= needy && v.thirst >= needy)
        return Thought::kHungryAndThirsty;
    if (v.hunger >= needy)
        return Thought::kHungry;
    if (v.thirst >= needy)
        return Thought::kThirsty;
    if (outside && badWeather && v.appearance != 3)
        return Thought::kDislikeWeather;
    if (litterNear(v.x, v.z, kNearCells) > 0)
        return Thought::kMessy;
    if (v.happiness <= float(_rules.unhappy))
        return v.generation > 0 ? Thought::kWhining : Thought::kUnhappy;
    if (v.bored > float(boredom) / 2)
        return Thought::kBored;
    if (v.doing == Doing::kQueueing && v.queue >= 0 &&
        _queues[size_t(v.queue)].line.size() > size_t(2 * kVisitorsPerQueueCell))
        return Thought::kLineTooLong;
    if (outside && _weather == Weather::kFine && v.happiness >= float(_rules.happy))
        return Thought::kLikeWeather;
    if (v.happiness >= float(_rules.happy) + (100 - float(_rules.happy)) / 2)
        return Thought::kVeryHappy;
    if (v.happiness >= float(_rules.happy))
        return Thought::kHappy;
    return Thought::kOkay;
}

int Park::litterNear(float x, float z, float cells) const {
    const float reach = cells * kCellUnits;
    int count = 0;
    for (const Litter &l : _litter)
        count += std::hypot(l.x - x, l.z - z) <= reach;
    return count;
}

bool Park::objectNear(const Visitor &v, bool (*what)(const Attractions::Attraction &), int cells) const {
    for (const PlacedObject &o : _objects) {
        const Attractions::Attraction *a = _attractions.find(o.id);
        if (a && what(*a) && std::abs(o.at.x - v.cell.x) + std::abs(o.at.y - v.cell.y) <= cells)
            return true;
    }
    return false;
}

// On a path cell's middle: sick, if they've had too much, on the path;
// their litter into a bin near enough, or now and then onto the path.
// Those nearby like neither.
void Park::dropLitter(Visitor &v) {
    const auto drop = [&](int frame, bool vomit, float spread) {
        Litter l;
        const uint32_t r = roll(v);
        l.x = v.x + (float(r % 100) / 100 - 0.5f) * spread;
        l.z = v.z + (float(r / 100 % 100) / 100 - 0.5f) * spread;
        l.frame = frame;
        l.vomit = vomit;
        if (_litter.size() >= kMostLitter)
            _litter.erase(_litter.begin());
        _litter.push_back(l);
    };
    if (v.sickness >= float(_rules.vomitCapacity)) {
        drop(kVomitFrames[roll(v) % kVomitFrames.size()], true, 2);
        v.sickness = 0;
        v.happiness = std::max(0.0f, v.happiness - float(_rules.mediumChange));
        think(v, Thought::kSick);
        for (Visitor &other : _people)
            if (other.id != v.id && other.inPark &&
                std::hypot(other.x - v.x, other.z - v.z) <= kNearCells * kCellUnits) {
                other.happiness = std::max(0.0f, other.happiness - float(_rules.smallChange));
                think(other, Thought::kMessy);
            }
    }
    if (v.carrying <= 0)
        return;
    if (objectNear(v, [](const Attractions::Attraction &a) { return a.holdsLitter; }, kBinCells)) {
        v.carrying = 0;
        return;
    }
    if (int(roll(v) % uint32_t(kLitterChance)) < v.carrying) {
        drop(int(roll(v) % kLitterFrames), false, kCellUnits / 2);
        v.carrying = 0;
    }
}

// Once a day: the weather, now and then; its effects on those out in it;
// pranksters' pranks; unhappy grown-ups' whining heard around them; mess.
// The season's weather: fine, or one of its chances.
void Park::rollWeather() {
    const size_t season = size_t(dateOf(_lastDay).month / 3) % _seasons.size();
    const int r = int((uint32_t(_lastDay + 1) * 2654435761u >> 8) % 100);
    _weather = Weather::kFine;
    for (int w = 0, sum = 0; w < 4; ++w) {
        sum += _seasons[season][size_t(w)];
        if (r < sum) {
            _weather = Weather(w + 1);
            _heatwaves += _weather == Weather::kHeatwave;
            break;
        }
    }
}

void Park::newDay() {
    if (_lastDay % kWeatherSpellDays == 0)
        rollWeather();
    objectsDay();
    staffDay();
    researchDay();
    financeDay();
    missionDay();
    clearingDay();
    challengeDay();
    const bool weatherDay = _lastDay % uint32_t(_rules.weatherDays) == 0;
    const bool wet = _weather == Weather::kRain || _weather == Weather::kStorm || _weather == Weather::kSnow;
    for (Visitor &v : _people) {
        if (!v.inPark || v.doing == Doing::kGoingHome)
            continue;
        if (weatherDay && v.doing != Doing::kRiding) {
            const size_t g = size_t(std::clamp(v.generation, 0, 2));
            float change = _rules.weatherHappiness[g][size_t(_weather)];
            if (wet && change < 0 && v.appearance == 3)
                change = std::min(0.0f, change + float(_rules.umbrellaShelter));
            v.happiness = std::clamp(v.happiness + change, 0.0f, 100.0f);
            v.thirst = std::clamp(v.thirst + _rules.weatherThirst[g][size_t(_weather)], 0.0f, 100.0f);
            v.hunger = std::clamp(v.hunger + _rules.weatherHunger[g][size_t(_weather)], 0.0f, 100.0f);
        }
        if (v.doing == Doing::kWalking && litterNear(v.x, v.z, kNearCells) > 0)
            v.happiness = std::max(0.0f, v.happiness - 1);
        if (v.prankster && v.doing == Doing::kWalking && v.happiness < float(_rules.prankUnhappy) &&
            int(roll(v) % 100) < _rules.prankPercent &&
            !objectNear(
                v, [](const Attractions::Attraction &a) { return a.security; }, kCameraCells) &&
            !guarded(v.x, v.z)) {
            think(v, Thought::kNaughty);
            ++(v.generation == 0 ? _kidPranks : _adultPranks);
            chase(v);
            if (int(roll(v) % 100) < _rules.stinkBombPercent) {
                _stinkBombs.push_back({v.x, v.z, _millis, _millis + kStinkBombMillis});
                for (Visitor &other : _people)
                    if (other.id != v.id && other.inPark &&
                        std::hypot(other.x - v.x, other.z - v.z) <= kStinkBombCells * kCellUnits) {
                        other.happiness = std::max(0.0f, other.happiness - float(_rules.mediumChange));
                        think(other, Thought::kAngry);
                    }
            } else {
                for (int i = 0; i < kPrankLitter; ++i) {
                    v.carrying = kLitterChance;
                    dropLitter(v);
                }
            }
        }
        // Whining, heard about them: a grown-up's prank, as challenges count them.
        if (v.generation > 0 && v.happiness <= float(_rules.unhappy) && v.doing == Doing::kWalking) {
            ++_adultPranks;
            for (Visitor &other : _people)
                if (other.id != v.id && other.inPark &&
                    std::abs(other.x - v.x) <= float(_rules.rumourCells) * kCellUnits &&
                    std::abs(other.z - v.z) <= float(_rules.rumourCells) * kCellUnits)
                    other.happiness = std::max(0.0f, other.happiness - 1);
        }
    }
}

} // namespace reluva::tpw
