// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "engines/age/aimap.h"
#include "engines/age/car_actor.h"
#include "engines/age/engine_sound.h"
#include "engines/age/traffic_lights.h"
#include "engines/age/opponent.h"
#include "engines/age/road_graph.h"
#include "engines/age/traffic.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

// Police cars starting where the aimap puts them ([Police]), waiting
// there or patrolling the traffic's lanes, which chase the player once it
// speeds past one or runs into one, along the roads and then straight at
// it, sirens going; a player held stopped beside a chasing car for a few
// seconds is busted. They give up once left far behind. Reluva's own
// rules; see docs/engines/age-traffic.md.
class Police {
public:
    static constexpr float kNotice = 40;     // metres within which a cop sees the player speed
    static constexpr float kBustTime = 3;    // seconds held stopped beside a chasing cop
    static constexpr float kGiveUp = 300;    // metres behind for long, and it gives up

    ~Police() { clear(); }
    // The first `count` (all if negative) of the aimap's police cars;
    // their siren from `sirenTable` (see SirenSound).
    Error setup(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                const AiMapSettings& settings, int count, audio::Mixer* mixer,
                const std::string& sirenTable = "aud/cardata/opponent/policesiren.csv");
    void clear();
    // Patrolling the lanes (stopping at the lights) while not chasing,
    // rather than waiting; none of them without a network.
    void setPatrols(const TrafficNetwork* lanes, const TrafficLights* lights) {
        lanes_ = lanes;
        lights_ = lights;
    }

    // `speedLimit` m/s: a player going half as fast again past a cop is
    // chased. `traffic`: what a patrolling car keeps behind.
    void update(float dt, CarSim& player, const CollisionWorld& world, const RoadGraph& roads, float speedLimit,
                const std::vector<RoadUser>& traffic = {});
    void draw(const SceneLight& light) const;
    void drawFlares(const FlareDrawer& flares, const Vec3& eye) const;
    // Their headlights on (evening, night, fog, rain).
    void setLampsLit(bool lit) { lampsLit_ = lit; }
    void headlightPools(std::vector<gfx::DynamicLight>& out) const;

    bool busted() const { return busted_; }
    // The player put back somewhere else: the chases end.
    void forgive();
    int chasing() const;
    int patrolling() const;
    size_t count() const { return cops_.size(); }
    void users(std::vector<RoadUser>& out) const;
    // Their simulations, to bump into the other cars and the traffic.
    std::vector<CarSim*> cars();

private:
    struct Cop {
        std::unique_ptr<CarActor> car;
        PathDriver driver;
        bool chasing = false;
        float reroute = 0;  // seconds to the next route to the player at the latest
        Vec3 routedTo{};    // where the player was then
        float behind = 0;   // seconds left far behind
        float stuck = 0, backing = 0; // seconds stopped while ramming, and backing out
        float settled = 0;            // seconds waiting (after a moment, far ones aren't simulated)
        // Patrolling: the lanes it follows as one path, and for each of its
        // points the lane whose end it is (-1 for the rest).
        bool patrolling = false;
        std::vector<int> laneEnds;
    };

    void startPatrol(Cop& cop, float speedLimit);
    CarControls patrol(Cop& cop, const std::vector<RoadUser>& traffic, float dt);

    std::vector<Cop> cops_;
    audio::Mixer* mixer_ = nullptr;
    SirenSound siren_;
    std::shared_ptr<audio::Sound> arrest_; // played when busted
    const TrafficNetwork* lanes_ = nullptr;
    const TrafficLights* lights_ = nullptr;
    std::mt19937 rng_{7};
    float noticeDistance_ = kNotice;
    float bustTimer_ = 0, clock_ = 0;
    bool busted_ = false;
    bool lampsLit_ = false;
};

} // namespace reluva::age
