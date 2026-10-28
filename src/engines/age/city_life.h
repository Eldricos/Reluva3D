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
        uint32_t seed = 1;
    };
    // Starts afresh with `settings` (the city's, or a race's read over
    // them). Returns the first problem loading models, if any.
    Error start(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                const AiMapSettings& settings, const Options& options, audio::Mixer* mixer);
    void stop();
    void setTrafficDensity(float d) { traffic_.setDensity(d); }
    void setPedDensity(float d) { peds_.setDensity(d); }

    // Moves everyone round the player; `others` are the other driven cars
    // (opponents). `fill` places the traffic and pedestrians anywhere
    // round the player rather than out of sight (just started).
    void update(float dt, CarSim& player, const std::vector<CarSim*>& others, const CollisionWorld& world,
                const RoadGraph& roads, bool fill = false);
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye);

    Traffic& traffic() { return traffic_; }
    Pedestrians& pedestrians() { return peds_; }
    Police& police() { return police_; }

private:
    AiRoadMap roads_;
    AiMapSettings city_, current_;
    Traffic traffic_;
    Pedestrians peds_;
    Police police_;
};

} // namespace reluva::age
