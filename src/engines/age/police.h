// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "engines/age/aimap.h"
#include "engines/age/car_actor.h"
#include "engines/age/opponent.h"
#include "engines/age/road_graph.h"
#include "engines/age/traffic.h"

#include <memory>
#include <vector>

namespace reluva::age {

// Police cars waiting where the aimap puts them ([Police]), which chase
// the player once it speeds past one or runs into one, along the roads
// and then straight at it, sirens going; a player held stopped beside a
// chasing car for a few seconds is busted. They give up once left far
// behind. Reluva's own rules; see docs/engines/age-traffic.md.
class Police {
public:
    static constexpr float kNotice = 40;     // metres within which a cop sees the player speed
    static constexpr float kBustTime = 3;    // seconds held stopped beside a chasing cop
    static constexpr float kGiveUp = 300;    // metres behind for long, and it gives up

    ~Police() { clear(); }
    // The first `count` (all if negative) of the aimap's police cars.
    Error setup(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                const AiMapSettings& settings, int count, audio::Mixer* mixer);
    void clear();

    // `speedLimit` m/s: a player going half as fast again past a cop is
    // chased.
    void update(float dt, CarSim& player, const CollisionWorld& world, const RoadGraph& roads, float speedLimit);
    void draw(const SceneLight& light) const;
    void drawFlares(const FlareDrawer& flares, const Vec3& eye) const;
    // Their headlights on (evening, night, fog, rain).
    void setLampsLit(bool lit) { lampsLit_ = lit; }
    void headlightPools(std::vector<gfx::DynamicLight>& out) const;

    bool busted() const { return busted_; }
    // The player put back somewhere else: the chases end.
    void forgive();
    int chasing() const;
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
    };

    std::vector<Cop> cops_;
    audio::Mixer* mixer_ = nullptr;
    std::shared_ptr<audio::Sound> sirenSound_;
    audio::SoundHandle siren_ = audio::kNoSound;
    float noticeDistance_ = kNotice;
    float bustTimer_ = 0, clock_ = 0;
    bool busted_ = false;
    bool lampsLit_ = false;
};

} // namespace reluva::age
