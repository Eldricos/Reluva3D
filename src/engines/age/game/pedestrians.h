// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/formats/aimap.h"
#include "engines/age/formats/bai.h"
#include "engines/age/game/collision.h"
#include "engines/age/render/ped_model.h"
#include "engines/age/game/traffic.h"

#include <memory>
#include <random>
#include <vector>

namespace reluva::age {

// People on the sidewalks: the aimap's models (those for rain when it
// rains), walking along the road map's sidewalks and on round the
// corners, diving out of the way of cars coming at them. Reluva's own
// rules; see docs/engines/age-traffic.md.
class Pedestrians {
public:
    Error setup(gfx::Renderer3D& renderer, const SearchSet& files, const AiRoadMap& map,
                const AiMapSettings& settings, bool rain, uint32_t seed);
    void unload();
    void clear() { _peds.clear(); }
    // 0..1: how many there are about the focus (1 = kPedsAtFull).
    void setDensity(float density) { _density = density; }

    void update(float dt, const Vec3& focus, const std::vector<RoadUser>& cars, const CollisionWorld& world,
                bool fill = false);
    void draw(const gfx::Mat4& viewProj, const Vec3& eye, const SceneLight& light) const;
    size_t count() const { return _peds.size(); }

    static constexpr int kPedsAtFull = 300;

private:
    struct Walk {
        std::vector<Vec3> points;
        std::vector<float> at;
        float width = 1; // of the sidewalk, curb to outer edge
    };
    struct Ped {
        int model = 0;
        uint32_t paintJob = 0;
        int walk = 0;
        bool ahead = true;  // along the walk's points, else back
        float s = 0, side = 0; // along it, and across it (share of its width)
        Vec3 position{};
        float yaw = 0;
        int move = 0;
        float time = 0;     // into the move
        bool walking = true;
        float idle = 0;     // seconds left standing before walking on
    };

    void startMove(Ped& p, int move);
    void placeOnWalk(Ped& p);
    void nextWalk(Ped& p);

    std::vector<std::unique_ptr<PedModel>> _models;
    std::vector<Walk> _walks;
    std::vector<Ped> _peds;
    std::mt19937 _rng;
    float _density = 0;
};

} // namespace reluva::age
