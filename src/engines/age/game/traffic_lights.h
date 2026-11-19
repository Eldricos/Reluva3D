// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/gfx3d.h"
#include "engines/age/formats/aimap.h"
#include "engines/age/formats/bai.h"
#include "engines/age/render/render_cache.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

enum class Signal { kGreen, kYellow, kRed };

// The traffic lights the game puts up at road ends: where an end's
// control is 1 (docs/engines/age/games/mm2/formats/bai.md), at its two points (on the
// curb, and a metre across the road). Each junction's lights take turns:
// at a crossroads two opposite roads at a time, elsewhere a road at a
// time. Reluva's timing; see docs/engines/age/games/mm2/systems/traffic.md.
class TrafficLights {
public:
    // seconds
    static constexpr float kGreen = 12;
    static constexpr float kYellow = 3;
    static constexpr float kAllRed = 1;

    void build(const AiRoadMap &map, const AiMapSettings &settings);
    void update(float dt) { _clock += dt; }
    void setClock(float seconds) { _clock = seconds; }

    // The light for traffic arriving at road `road`'s end `end` (0 at its
    // last point, 1 at its first); Green where there's none.
    Signal signal(uint32_t road, int end) const;
    bool has(uint32_t road, int end) const { return _index.count(key(road, end)) > 0; }
    size_t count() const { return _lights.size(); }
    // Where light `i` stands (its model, the point it's stood on, its
    // axes), and knocking it down (it's no longer drawn; its signal goes
    // on, for the traffic).
    struct Placement {
        std::string model;
        Vec3 at{};
        Mat3 axes;
    };
    Placement placement(size_t i) const { return {_models[size_t(_lights[i].model)], _lights[i].at, _lights[i].axes}; }
    void knockDown(size_t i) { _lights[i].down = true; }

    // The models with the glows for each light's state (the night glows,
    // larger, when `night`), within `distance` of the eye.
    void draw(gfx::Renderer3D &renderer, ModelCache &models, const SceneLight &light, const gfx::Mat4 &viewProj,
              const Vec3 &eye, bool night, float distance = 350) const;

private:
    struct Light {
        uint32_t road = 0;
        uint32_t junction = 0;
        int end = 0;
        int phase = 0;
        int model = 0;     // 0 narrow, 1 wide
        bool down = false; // knocked down
        Vec3 at{};
        Mat3 axes; // X back towards the curb, Y up, Z facing the traffic
    };
    struct Junction {
        int phases = 2;
        float offset = 0; // seconds into its cycle at clock 0
    };
    static uint64_t key(uint32_t road, int end) { return uint64_t(road) << 1 | uint64_t(end & 1); }
    Signal signalOf(const Light &l) const;

    std::vector<Light> _lights;
    std::vector<Junction> _junctions;
    std::map<uint64_t, size_t> _index;
    std::array<std::string, 2> _models;
    float _clock = 0;
};

} // namespace reluva::age
