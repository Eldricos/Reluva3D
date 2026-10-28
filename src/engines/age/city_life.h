// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "engines/age/aimap.h"
#include "engines/age/bai.h"
#include "engines/age/pedestrians.h"
#include "engines/age/police.h"
#include "engines/age/road_graph.h"
#include "engines/age/traffic.h"
#include "engines/age/traffic_lights.h"

#include <string>
#include <vector>

namespace reluva::age {

// A city's traffic, pedestrians and police together, as the Drive and
// Race pages run them round the player.
class CityLife {
public:
    // city/<map>.bai and city/<map>.aimap.
    Error load(const SearchSet& files, const std::string& map);
    const AiMapSettings& citySettings() const { return city_; }
    bool loaded() const { return !roads_.roads.empty(); }

    struct Options {
        float traffic = 0, peds = 0; // densities, 0..1
        int cops = -1;                // how many of the settings' police (-1: all)
        bool rain = false;            // the pedestrians dressed for it
        bool laneChanges = false;     // traffic changes lanes (else as the settings say)
        bool patrols = false;         // the police patrol the lanes rather than wait
        uint32_t seed = 1;
    };
    // Starts afresh with `settings` (the city's, or a race's read over
    // them). Returns the first problem loading models, if any.
    Error start(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                const AiMapSettings& settings, const Options& options, audio::Mixer* mixer);
    void stop();
    void setTrafficDensity(float d) { traffic_.setDensity(d); }
    void setPedDensity(float d) { peds_.setDensity(d); }
    // Evening, night, fog or rain: the cars' headlights on; `night`: the
    // traffic lights' night glows.
    void setLamps(bool lit, bool night);

    // Moves everyone round the player; `others` are the other driven cars
    // (opponents). `fill` places the traffic and pedestrians anywhere
    // round the player rather than out of sight (just started).
    void update(float dt, CarSim& player, const std::vector<CarSim*>& others, const CollisionWorld& world,
                const RoadGraph& roads, bool fill = false);
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye, const FlareDrawer* flares = nullptr);
    // Where the traffic's and the police's headlights light the road.
    void headlightPools(std::vector<gfx::DynamicLight>& out, const Vec3& eye) const;

    Traffic& traffic() { return traffic_; }
    Pedestrians& pedestrians() { return peds_; }
    Police& police() { return police_; }
    const TrafficLights& lights() const { return lights_; }

private:
    AiRoadMap roads_;
    std::string map_;
    AiMapSettings city_, current_;
    Traffic traffic_;
    Pedestrians peds_;
    Police police_;
    TrafficLights lights_;
    bool lampsLit_ = false, night_ = false;
};

} // namespace reluva::age
