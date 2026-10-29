// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/cops_robbers.h"

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

float flat(const Vec3& a, const Vec3& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }

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
    void vec(const Vec3& v) {
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
    explicit Unbytes(const std::vector<uint8_t>& d) : data(d) {}
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
        for (float& f : v) {
            const uint32_t u = u32();
            std::memcpy(&f, &u, 4);
            if (!std::isfinite(f))
                f = 0;
        }
        return v;
    }
    const std::vector<uint8_t>& data;
    size_t at = 0;
    bool ok = true;
};

} // namespace

void GoldGame::start(const NetSettings& settings, const std::vector<Waypoint>& points, uint32_t seed) {
    *this = GoldGame();
    mode_ = settings.gold;
    limit_ = settings.limit;
    limitValue_ = settings.limitValue;
    for (const Waypoint& w : points)
        points_.push_back(w.position);
    if (points_.empty())
        points_.push_back({0, 0, 0});
    std::mt19937 rng(seed);
    // The gold's home at random; the bases among the places well away
    // from it, the second as far from the gold as the first (fair to both
    // sides) and well away from the first.
    const size_t homeAt = rng() % points_.size();
    home_ = points_[homeAt];
    std::vector<size_t> far;
    for (size_t i = 0; i < points_.size(); ++i)
        if (i != homeAt && flat(points_[i], home_) >= kBaseApart)
            far.push_back(i);
    if (far.empty())
        for (size_t i = 0; i < points_.size(); ++i)
            if (i != homeAt || points_.size() == 1)
                far.push_back(i);
    const size_t first = far[rng() % far.size()];
    size_t second = first;
    float best = 1e30f;
    for (size_t i : far) {
        if (i == first || flat(points_[i], points_[first]) < kBaseApart)
            continue;
        const float d = std::fabs(flat(points_[i], home_) - flat(points_[first], home_));
        if (d < best) {
            best = d;
            second = i;
        }
    }
    bases_ = {points_[first], points_[second]};
    restAt(home_, true);
}

Vec3 GoldGame::base(uint8_t side) const { return teams() ? bases_[side & 1] : bases_[0]; }

int GoldGame::score(uint8_t side) const {
    auto it = scores_.find(side);
    return it == scores_.end() ? 0 : it->second;
}

void GoldGame::restAt(const Vec3& at, bool atHome) {
    gold_ = at;
    holder_ = kNoPlayer;
    atHome_ = atHome;
    restingFor_ = 0;
}

void GoldGame::event(GoldEvent::Kind kind, uint8_t player, uint8_t side) {
    events_.push_back({kind, player, side, ++eventSeq_});
    while (events_.size() > kEventsKept)
        events_.erase(events_.begin());
}

std::vector<GoldEvent> GoldGame::eventsAfter(uint32_t seq) const {
    std::vector<GoldEvent> out;
    for (const GoldEvent& e : events_)
        if (e.seq > seq)
            out.push_back(e);
    return out;
}

void GoldGame::step(float clock, float dt, const std::vector<Car>& cars, float floor) {
    if (over_)
        return;
    dropperWait_ = std::max(0.0f, dropperWait_ - dt);
    holderSafe_ = std::max(0.0f, holderSafe_ - dt);
    const Car* holder = nullptr;
    for (const Car& c : cars)
        if (c.id == holder_)
            holder = &c;
    if (holder_ != kNoPlayer && !holder) {
        // Its car's gone (the player left): the gold stays where it was.
        restAt(gold_, false);
    }
    if (holder && holder->putBack) {
        event(GoldEvent::Kind::Dropped, holder->id, sideOf(*holder));
        lastDropper_ = holder->id;
        dropperWait_ = kDropperWait;
        restAt(gold_, false); // where it was before the car was moved
    } else if (holder) {
        gold_ = holder->position;
        if (holder->position[1] < floor) {
            event(GoldEvent::Kind::Returned, holder->id, sideOf(*holder));
            restAt(home_, true);
        } else if (flat(holder->position, base(sideOf(*holder))) < kDeliver &&
                   std::fabs(holder->position[1] - base(sideOf(*holder))[1]) < 10) {
            ++scores_[sideOf(*holder)];
            event(GoldEvent::Kind::Delivered, holder->id, sideOf(*holder));
            restAt(home_, true);
        } else if (holderSafe_ <= 0) {
            // Rammed by a car of another side.
            for (const Car& c : cars) {
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
                event(GoldEvent::Kind::Dropped, holder->id, sideOf(*holder));
                lastDropper_ = holder->id;
                dropperWait_ = kDropperWait;
                restAt(holder->position, false);
                break;
            }
        }
    } else {
        // Lying: whoever's nearest within reach takes it.
        restingFor_ += atHome_ ? 0 : dt;
        if (!atHome_ && (restingFor_ > kLieFor || gold_[1] < floor)) {
            event(GoldEvent::Kind::Returned, kNoPlayer, 0);
            restAt(home_, true);
        }
        const Car* taker = nullptr;
        float nearest = kPickUp;
        for (const Car& c : cars) {
            if ((c.id == lastDropper_ && dropperWait_ > 0) || c.putBack)
                continue;
            const float d = flat(c.position, gold_);
            if (d < nearest && std::fabs(c.position[1] - gold_[1]) < 4) {
                nearest = d;
                taker = &c;
            }
        }
        if (taker) {
            event(atHome_ ? GoldEvent::Kind::Got : GoldEvent::Kind::Recovered, taker->id, sideOf(*taker));
            holder_ = taker->id;
            holderSafe_ = kHolderSafe;
            gold_ = taker->position;
        }
    }
    // The limit.
    if (limit_ == GoldLimit::Time && limitValue_ > 0 && clock >= float(limitValue_) * 60) {
        over_ = true;
        event(GoldEvent::Kind::TimeUp, kNoPlayer, 0);
    } else if (limit_ == GoldLimit::Points && limitValue_ > 0) {
        for (const auto& [side, points] : scores_)
            if (points >= limitValue_ && !over_) {
                over_ = true;
                event(GoldEvent::Kind::PointLimit, kNoPlayer, side);
            }
    }
}

std::vector<uint8_t> GoldGame::encode() const {
    Bytes b;
    b.u8('G');
    b.u8(uint8_t(mode_));
    b.u8(holder_);
    b.u8(atHome_ ? 1 : 0);
    b.u8(over_ ? 1 : 0);
    b.vec(gold_);
    b.vec(home_);
    b.vec(bases_[0]);
    b.vec(bases_[1]);
    b.u8(uint8_t(std::min<size_t>(scores_.size(), 255)));
    for (const auto& [side, points] : scores_) {
        b.u8(side);
        b.u16(uint16_t(std::clamp(points, 0, 65535)));
    }
    b.u8(uint8_t(events_.size()));
    for (const GoldEvent& e : events_) {
        b.u8(uint8_t(e.kind));
        b.u8(e.player);
        b.u8(e.side);
        b.u32(e.seq);
    }
    return b.out;
}

bool GoldGame::decode(const std::vector<uint8_t>& data) {
    Unbytes r(data);
    if (r.u8() != 'G')
        return false;
    GoldGame g;
    g.mode_ = GoldMode(std::min<uint8_t>(r.u8(), uint8_t(GoldMode::RobberTeams)));
    g.holder_ = r.u8();
    g.atHome_ = r.u8() != 0;
    g.over_ = r.u8() != 0;
    g.gold_ = r.vec();
    g.home_ = r.vec();
    g.bases_[0] = r.vec();
    g.bases_[1] = r.vec();
    for (size_t n = r.u8(); n > 0 && r.ok; --n) {
        const uint8_t side = r.u8();
        g.scores_[side] = r.u16();
    }
    for (size_t n = r.u8(); n > 0 && r.ok; --n) {
        GoldEvent e;
        e.kind = GoldEvent::Kind(std::min<uint8_t>(r.u8(), uint8_t(GoldEvent::Kind::PointLimit)));
        e.player = r.u8();
        e.side = r.u8();
        e.seq = r.u32();
        g.events_.push_back(e);
        g.eventSeq_ = std::max(g.eventSeq_, e.seq);
    }
    if (!r.ok)
        return false;
    g.limit_ = limit_;
    g.limitValue_ = limitValue_;
    g.points_ = points_;
    *this = std::move(g);
    return true;
}

Vec3 GoldBot::goal(uint8_t id, uint8_t team, const GoldGame& gold, const std::vector<GoldGame::Car>& cars,
                   bool& chasing) {
    chasing = false;
    const GoldGame::Car me{id, team, {}, {}};
    if (gold.holder() == id)
        return gold.base(gold.sideOf(me)); // home with it
    for (const GoldGame::Car& c : cars)
        if (c.id == gold.holder()) {
            if (gold.sideOf(c) == gold.sideOf(me)) {
                // A teammate has it: after whoever of the others is
                // nearest it, else home ahead of it.
                const GoldGame::Car* threat = nullptr;
                for (const GoldGame::Car& o : cars)
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

bool GoldBot::plan(float dt, const Vec3& at, uint8_t id, uint8_t team, const GoldGame* gold,
                   const std::vector<GoldGame::Car>& cars, const std::vector<Waypoint>& places, const RoadGraph& roads,
                   bool pathDone, std::vector<Vec3>& path) {
    replanIn_ -= dt;
    if (flat(at, still_) > 4) {
        still_ = at;
        stillFor_ = 0;
    } else
        stillFor_ += dt;
    bool chasing = false;
    Vec3 target;
    if (gold) {
        target = goal(id, team, *gold, cars, chasing);
    } else {
        // A cruise: from place to place, the next at random.
        if (places.empty())
            return false;
        if (!planning_ || flat(at, places[place_ % places.size()].position) < 30)
            place_ = (seed_ = seed_ * 1664525u + 1013904223u) >> 8;
        target = places[place_ % places.size()].position;
    }
    const bool moved = flat(target, planned_) > (chasing ? 10.0f : 25.0f);
    if (planning_ && replanIn_ > 0 && !moved && !pathDone)
        return false;
    planning_ = true;
    replanIn_ = chasing ? 1.0f : 4.0f;
    planned_ = target;
    if (flat(at, target) < 15 && !chasing)
        stillFor_ = 0; // there, and waiting: not stuck
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

Error loadGoldPoints(const SearchSet& files, const std::string& city, std::vector<Waypoint>& out) {
    std::vector<uint8_t> data;
    if (Error err = files.read("race/" + city + "/multicopwaypoints.csv", data))
        return err;
    return parseWaypoints(std::string(data.begin(), data.end()), out);
}

std::map<std::string, std::vector<std::string>> parseSpeechTable(const std::string& csv) {
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
