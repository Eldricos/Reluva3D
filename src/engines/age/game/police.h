// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/age/formats/aimap.h"
#include "engines/age/render/car_actor.h"
#include "engines/age/audio/engine_sound.h"
#include "engines/age/game/traffic_lights.h"
#include "engines/age/game/opponent.h"
#include "engines/age/game/road_graph.h"
#include "engines/age/game/traffic.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * Police cars starting where the aimap puts them ([Police]), waiting
 * there or patrolling the traffic's lanes, which chase the player once it
 * speeds past one or runs into one, along the roads and then straight at
 * it, sirens going; a player held stopped beside a chasing car for a few
 * seconds is busted. They give up once left far behind. Reluva's own
 * rules; see docs/engines/age/games/mm2/systems/police.md.
 */
class Police {
public:
    static constexpr float kNotice = 40;  // metres within which a cop sees the player speed, if the aimap gives none
    static constexpr float kBustTime = 3; // seconds held stopped beside a chasing cop
    static constexpr float kGiveUp = 300; // metres behind for 8 s, and it gives up

    ~Police() { clear(); }
    /**
     * The first `count` (all if negative) of the aimap's police cars;
     * their siren from `sirenTable` (see SirenSound), and with it the
     * arrest's sound, when there's a `mixer`. A car that won't load is left
     * out.
     * @return The first error met (a car, the siren), though the rest are set up.
     */
    Error setup(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files,
                const AiMapSettings &settings, int count, audio::Mixer *mixer,
                const std::string &sirenTable = "aud/cardata/opponent/policesiren.csv");
    /** Takes the cars away and stops the siren; no longer busted. */
    void clear();
    /**
     * Patrolling the lanes (stopping at the lights) while not chasing,
     * rather than waiting; none of them without a network.
     */
    void setPatrols(const TrafficNetwork *lanes, const TrafficLights *lights) {
        _lanes = lanes;
        _lights = lights;
    }

    /**
     * Drives the police for a frame, bumping into the player's car.
     * @param roads What a chasing car routes along.
     * @param speedLimit m/s: a player going half as fast again past a cop is
     *        chased; a patrolling car keeps to it.
     * @param traffic What a patrolling car keeps behind.
     */
    void update(float dt, CarSim &player, const CollisionWorld &world, const RoadGraph &roads, float speedLimit,
                const std::vector<RoadUser> &traffic = {});
    /** Draws the cars, and their headlights' and sirens' flares. */
    ///@{
    void draw(const SceneLight &light) const;
    void drawFlares(const FlareDrawer &flares, const Vec3 &eye) const;
    ///@}
    /** Their headlights on (evening, night, fog, rain). */
    void setLampsLit(bool lit) { _lampsLit = lit; }
    /** Adds the pools of light their headlights throw on the road. */
    void headlightPools(std::vector<gfx::DynamicLight> &out) const;

    /** Whether the player has been busted (until forgive() or clear()). */
    bool busted() const { return _busted; }
    /** The player put back somewhere else: the chases end. */
    void forgive();
    /** Every car after the player at once (Crash Course's cop chases). */
    void chaseAll();
    /** How many cars are chasing the player, and how many patrolling. */
    ///@{
    int chasing() const;
    int patrolling() const;
    ///@}
    /** How many police cars there are. */
    size_t count() const { return _cops.size(); }
    /** Adds where the cars are and how they move, for other drivers and the pedestrians. */
    void users(std::vector<RoadUser> &out) const;
    /** Their simulations, to bump into the other cars and the traffic. */
    std::vector<CarSim *> cars();

private:
    struct Cop {
        std::unique_ptr<CarActor> car;
        PathDriver driver;
        bool chasing = false;
        float reroute = 0; // seconds to the next route to the player at the latest
        Vec3 routedTo{};   // where the player was then
        float behind = 0;  // seconds left far behind
        // seconds stopped while ramming, and backing out
        float stuck = 0;
        float backing = 0;
        float settled = 0; // seconds waiting (after a moment, far ones aren't simulated)
        // Patrolling: the lanes it follows as one path, and for each of its
        // points the lane whose end it is (-1 for the rest).
        bool patrolling = false;
        std::vector<int> laneEnds;
    };

    void startPatrol(Cop &cop, float speedLimit);
    CarControls patrol(Cop &cop, const std::vector<RoadUser> &traffic, float dt);

    std::vector<Cop> _cops;
    audio::Mixer *_mixer = nullptr;
    SirenSound _siren;
    std::shared_ptr<audio::Sound> _arrest; // played when busted
    const TrafficNetwork *_lanes = nullptr;
    const TrafficLights *_lights = nullptr;
    std::mt19937 _rng{7};
    float _noticeDistance = kNotice;
    float _bustTimer = 0;
    float _clock = 0;
    bool _busted = false;
    bool _lampsLit = false;
};

} // namespace reluva::age
