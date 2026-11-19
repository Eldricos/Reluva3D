// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/game/cops_robbers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>

namespace reluva::age {

namespace {

constexpr float kPickUp = 5;        // metres from the gold (and 4 up or down)
constexpr float kDeliver = 12;      // metres from the base
constexpr float kRam = 4.5f;        // cars' middles this near, closing
constexpr float kRamSpeed = 4;      // at this many m/s or more, knock the gold loose
constexpr float kDropperWait = 3;   // seconds before the car that dropped it can take it back
constexpr float kHolderSafe = 1.5f; // seconds a new holder keeps it whatever hits it
constexpr float kLieFor = 60;       // seconds it lies away from home before it's taken back
constexpr float kBaseApart = 300;   // metres at least from the gold's home to a base
constexpr size_t kEventsKept = 6;

float flat(const Vec3 &a, const Vec3 &b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

class Bytes {
public:
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void vec(const Vec3 &v) {
        for (float f : v) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            u32(u);
        }
    }
    std::vector<uint8_t> out;
};

class Unbytes {
public:
    explicit Unbytes(const std::vector<uint8_t> &d) : data(d) {}
    uint8_t u8() {
        if (at >= data.size()) {
            ok = false;
            return 0;
        }
        return data[at++];
    }
    uint16_t u16() { return uint16_t(u8() | u8() << 8); }
    uint32_t u32() { return uint32_t(u16()) | uint32_t(u16()) << 16; }
    Vec3 vec() {
        Vec3 v;
        for (float &f : v) {
            const uint32_t u = u32();
            std::memcpy(&f, &u, 4);
            if (!std::isfinite(f))
                f = 0;
        }
        return v;
    }
    const std::vector<uint8_t> &data;
    size_t at = 0;
    bool ok = true;
};

} // namespace

void GoldGame::start(const NetSettings &settings, const std::vector<Waypoint> &points, uint32_t seed) {
    *this = GoldGame();
    _mode = settings.gold;
    _limit = settings.limit;
    _limitValue = settings.limitValue;
    for (const Waypoint &w : points)
        _points.push_back(w.position);
    if (_points.empty())
        _points.push_back({0, 0, 0});
    std::mt19937 rng(seed);
    // The gold's home at random; the bases among the places well away
    // from it, the second as far from the gold as the first (fair to both
    // sides) and well away from the first.
    const size_t homeAt = rng() % _points.size();
    _home = _points[homeAt];
    std::vector<size_t> far;
    for (size_t i = 0; i < _points.size(); ++i)
        if (i != homeAt && flat(_points[i], _home) >= kBaseApart)
            far.push_back(i);
    if (far.empty())
        for (size_t i = 0; i < _points.size(); ++i)
            if (i != homeAt || _points.size() == 1)
                far.push_back(i);
    const size_t first = far[rng() % far.size()];
    size_t second = first;
    float best = 1e30f;
    for (size_t i : far) {
        if (i == first || flat(_points[i], _points[first]) < kBaseApart)
            continue;
        const float d = std::fabs(flat(_points[i], _home) - flat(_points[first], _home));
        if (d < best) {
            best = d;
            second = i;
        }
    }
    _bases = {_points[first], _points[second]};
    restAt(_home, true);
}

Vec3 GoldGame::base(uint8_t side) const {
    return teams() ? _bases[side & 1] : _bases[0];
}

int GoldGame::score(uint8_t side) const {
    auto it = _scores.find(side);
    return it == _scores.end() ? 0 : it->second;
}

void GoldGame::restAt(const Vec3 &at, bool atHome) {
    _gold = at;
    _holder = kNoPlayer;
    _atHome = atHome;
    _restingFor = 0;
}

void GoldGame::event(GoldEvent::Kind kind, uint8_t player, uint8_t side) {
    _events.push_back({kind, player, side, ++_eventSeq});
    while (_events.size() > kEventsKept)
        _events.erase(_events.begin());
}

std::vector<GoldEvent> GoldGame::eventsAfter(uint32_t seq) const {
    std::vector<GoldEvent> out;
    for (const GoldEvent &e : _events)
        if (e.seq > seq)
            out.push_back(e);
    return out;
}

void GoldGame::step(float clock, float dt, const std::vector<Car> &cars, float floor) {
    if (_over)
        return;
    _dropperWait = std::max(0.0f, _dropperWait - dt);
    _holderSafe = std::max(0.0f, _holderSafe - dt);
    const Car *holder = nullptr;
    for (const Car &c : cars)
        if (c.id == _holder)
            holder = &c;
    if (_holder != kNoPlayer && !holder) {
        // Its car's gone (the player left): the gold stays where it was.
        restAt(_gold, false);
    }
    if (holder && holder->putBack) {
        event(GoldEvent::Kind::kDropped, holder->id, sideOf(*holder));
        _lastDropper = holder->id;
        _dropperWait = kDropperWait;
        restAt(_gold, false); // where it was before the car was moved
    } else if (holder) {
        _gold = holder->position;
        if (holder->position[1] < floor) {
            event(GoldEvent::Kind::kReturned, holder->id, sideOf(*holder));
            restAt(_home, true);
        } else if (flat(holder->position, base(sideOf(*holder))) < kDeliver &&
                   std::fabs(holder->position[1] - base(sideOf(*holder))[1]) < 10) {
            ++_scores[sideOf(*holder)];
            event(GoldEvent::Kind::kDelivered, holder->id, sideOf(*holder));
            restAt(_home, true);
        } else if (_holderSafe <= 0) {
            // Rammed by a car of another side.
            for (const Car &c : cars) {
                if (c.id == holder->id || sideOf(c) == sideOf(*holder) || c.putBack)
                    continue;
                Vec3 d = c.position - holder->position;
                d[1] = 0;
                const float dist = length(d);
                if (dist > kRam || dist < 1e-3f)
                    continue;
                const float closing = -dot(c.velocity - holder->velocity, d * (1 / dist));
                if (closing < kRamSpeed)
                    continue;
                event(GoldEvent::Kind::kDropped, holder->id, sideOf(*holder));
                _lastDropper = holder->id;
                _dropperWait = kDropperWait;
                restAt(holder->position, false);
                break;
            }
        }
    } else {
        // Lying: whoever's nearest within reach takes it.
        _restingFor += _atHome ? 0 : dt;
        if (!_atHome && (_restingFor > kLieFor || _gold[1] < floor)) {
            event(GoldEvent::Kind::kReturned, kNoPlayer, 0);
            restAt(_home, true);
        }
        const Car *taker = nullptr;
        float nearest = kPickUp;
        for (const Car &c : cars) {
            if ((c.id == _lastDropper && _dropperWait > 0) || c.putBack)
                continue;
            const float d = flat(c.position, _gold);
            if (d < nearest && std::fabs(c.position[1] - _gold[1]) < 4) {
                nearest = d;
                taker = &c;
            }
        }
        if (taker) {
            event(_atHome ? GoldEvent::Kind::kGot : GoldEvent::Kind::kRecovered, taker->id, sideOf(*taker));
            _holder = taker->id;
            _holderSafe = kHolderSafe;
            _gold = taker->position;
        }
    }
    // The limit.
    if (_limit == GoldLimit::kTime && _limitValue > 0 && clock >= float(_limitValue) * 60) {
        _over = true;
        event(GoldEvent::Kind::kTimeUp, kNoPlayer, 0);
    } else if (_limit == GoldLimit::kPoints && _limitValue > 0) {
        for (const auto &[side, points] : _scores)
            if (points >= _limitValue && !_over) {
                _over = true;
                event(GoldEvent::Kind::kPointLimit, kNoPlayer, side);
            }
    }
}

std::vector<uint8_t> GoldGame::encode() const {
    Bytes b;
    b.u8('G');
    b.u8(uint8_t(_mode));
    b.u8(_holder);
    b.u8(_atHome ? 1 : 0);
    b.u8(_over ? 1 : 0);
    b.vec(_gold);
    b.vec(_home);
    b.vec(_bases[0]);
    b.vec(_bases[1]);
    b.u8(uint8_t(std::min<size_t>(_scores.size(), 255)));
    for (const auto &[side, points] : _scores) {
        b.u8(side);
        b.u16(uint16_t(std::clamp(points, 0, 65535)));
    }
    b.u8(uint8_t(_events.size()));
    for (const GoldEvent &e : _events) {
        b.u8(uint8_t(e.kind));
        b.u8(e.player);
        b.u8(e.side);
        b.u32(e.seq);
    }
    return b.out;
}

bool GoldGame::decode(const std::vector<uint8_t> &data) {
    Unbytes r(data);
    if (r.u8() != 'G')
        return false;
    GoldGame g;
    g._mode = GoldMode(std::min<uint8_t>(r.u8(), uint8_t(GoldMode::kRobberTeams)));
    g._holder = r.u8();
    g._atHome = r.u8() != 0;
    g._over = r.u8() != 0;
    g._gold = r.vec();
    g._home = r.vec();
    g._bases[0] = r.vec();
    g._bases[1] = r.vec();
    for (size_t n = r.u8(); n > 0 && r.ok; --n) {
        const uint8_t side = r.u8();
        g._scores[side] = r.u16();
    }
    for (size_t n = r.u8(); n > 0 && r.ok; --n) {
        GoldEvent e;
        e.kind = GoldEvent::Kind(std::min<uint8_t>(r.u8(), uint8_t(GoldEvent::Kind::kPointLimit)));
        e.player = r.u8();
        e.side = r.u8();
        e.seq = r.u32();
        g._events.push_back(e);
        g._eventSeq = std::max(g._eventSeq, e.seq);
    }
    if (!r.ok)
        return false;
    g._limit = _limit;
    g._limitValue = _limitValue;
    g._points = _points;
    *this = std::move(g);
    return true;
}

Vec3 GoldBot::goal(uint8_t id, uint8_t team, const GoldGame &gold, const std::vector<GoldGame::Car> &cars,
                   bool &chasing) {
    chasing = false;
    const GoldGame::Car me{id, team, {}, {}};
    if (gold.holder() == id)
        return gold.base(gold.sideOf(me)); // home with it
    for (const GoldGame::Car &c : cars)
        if (c.id == gold.holder()) {
            if (gold.sideOf(c) == gold.sideOf(me)) {
                // A teammate has it: after whoever of the others is
                // nearest it, else home ahead of it.
                const GoldGame::Car *threat = nullptr;
                for (const GoldGame::Car &o : cars)
                    if (gold.sideOf(o) != gold.sideOf(me) &&
                        (!threat || flat(o.position, c.position) < flat(threat->position, c.position)))
                        threat = &o;
                if (!threat)
                    return gold.base(gold.sideOf(me));
                chasing = true;
                return threat->position + threat->velocity * 1.0f;
            }
            chasing = true;
            return c.position + c.velocity * 1.0f; // where it's going
        }
    return gold.goldPosition();
}

bool GoldBot::plan(float dt, const Vec3 &at, uint8_t id, uint8_t team, const GoldGame *gold,
                   const std::vector<GoldGame::Car> &cars, const std::vector<Waypoint> &places, const RoadGraph &roads,
                   bool pathDone, std::vector<Vec3> &path) {
    _replanIn -= dt;
    if (flat(at, _still) > 4) {
        _still = at;
        _stillFor = 0;
    } else
        _stillFor += dt;
    bool chasing = false;
    Vec3 target;
    if (gold) {
        target = goal(id, team, *gold, cars, chasing);
    } else {
        // A cruise: from place to place, the next at random.
        if (places.empty())
            return false;
        if (!_planning || flat(at, places[_place % places.size()].position) < 30)
            _place = (_seed = _seed * 1664525u + 1013904223u) >> 8;
        target = places[_place % places.size()].position;
    }
    const bool moved = flat(target, _planned) > (chasing ? 10.0f : 25.0f);
    if (_planning && _replanIn > 0 && !moved && !pathDone)
        return false;
    _planning = true;
    _replanIn = chasing ? 1.0f : 4.0f;
    _planned = target;
    if (flat(at, target) < 15 && !chasing)
        _stillFor = 0; // there, and waiting: not stuck
    // Near: straight at it (a car to ram through, 20 m past it); else by
    // the roads, from as far off them as the city's places are.
    Vec3 aim = target;
    if (chasing && flat(at, target) > 1) {
        Vec3 d = target - at;
        d[1] = 0;
        aim = target + normalized(d) * 20.0f;
    }
    if (flat(at, target) < 40)
        path = {aim};
    else
        path = roads.route(at, target, 250);
    return true;
}

Error loadGoldPoints(const SearchSet &files, const std::string &city, std::vector<Waypoint> &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read("race/" + city + "/multicopwaypoints.csv", data))
        return err;
    return parseWaypoints(std::string(data.begin(), data.end()), out);
}

std::map<std::string, std::vector<std::string>> parseSpeechTable(const std::string &csv) {
    // "<EVENT> header,," then "<folder>\<prefix> ,<last>,<before first>"
    // (London's with a fourth column, how many are used): the recordings
    // <prefix><n> for n after the third column up to the second.
    std::map<std::string, std::vector<std::string>> out;
    std::istringstream in(csv);
    std::string line, current;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        std::vector<std::string> f;
        std::istringstream cells(line);
        for (std::string c; std::getline(cells, c, ',');) {
            while (!c.empty() && c.back() == ' ')
                c.pop_back();
            f.push_back(c);
        }
        if (f.empty())
            continue;
        const size_t header = f[0].find(" header");
        if (header != std::string::npos) {
            current = f[0].substr(0, header);
            continue;
        }
        if (current.empty() || f.size() < 3)
            continue;
        const int last = std::atoi(f[1].c_str()), before = std::atoi(f[2].c_str());
        std::string prefix = f[0];
        std::replace(prefix.begin(), prefix.end(), '\\', '/');
        for (int n = std::max(0, before) + 1; n <= last && n < 100; ++n) {
            char num[8];
            std::snprintf(num, sizeof num, "%02d", n);
            out[current].push_back(prefix + num);
        }
        current.clear();
    }
    return out;
}

} // namespace reluva::age
