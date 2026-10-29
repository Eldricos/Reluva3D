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
    if (holder) {
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
                if (c.id == holder->id || sideOf(c) == sideOf(*holder))
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
            if (c.id == lastDropper_ && dropperWait_ > 0)
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
