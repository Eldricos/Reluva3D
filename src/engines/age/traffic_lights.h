// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"
#include "engines/age/aimap.h"
#include "engines/age/bai.h"
#include "engines/age/render_cache.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

enum class Signal { Green, Yellow, Red };

// The traffic lights the game puts up at road ends: where an end's
// control is 1 (docs/formats/age/bai.md), at its two points (on the
// curb, and a metre across the road). Each junction's lights take turns:
// at a crossroads two opposite roads at a time, elsewhere a road at a
// time. Reluva's timing; see docs/engines/age-traffic.md.
class TrafficLights {
public:
    static constexpr float kGreen = 12, kYellow = 3, kAllRed = 1; // seconds

    void build(const AiRoadMap& map, const AiMapSettings& settings);
    void update(float dt) { clock_ += dt; }
    void setClock(float seconds) { clock_ = seconds; }

    // The light for traffic arriving at road `road`'s end `end` (0 at its
    // last point, 1 at its first); Green where there's none.
    Signal signal(uint32_t road, int end) const;
    bool has(uint32_t road, int end) const { return index_.count(key(road, end)) > 0; }
    size_t count() const { return lights_.size(); }

    // The models with the glows for each light's state (the night glows,
    // larger, when `night`), within `distance` of the eye.
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye, bool night, float distance = 350) const;

private:
    struct Light {
        uint32_t road = 0, junction = 0;
        int end = 0, phase = 0;
        int model = 0; // 0 narrow, 1 wide
        Vec3 at{};
        Mat3 axes;     // X back towards the curb, Y up, Z facing the traffic
    };
    struct Junction {
        int phases = 2;
        float offset = 0; // seconds into its cycle at clock 0
    };
    static uint64_t key(uint32_t road, int end) { return uint64_t(road) << 1 | uint64_t(end & 1); }
    Signal signalOf(const Light& l) const;

    std::vector<Light> lights_;
    std::vector<Junction> junctions_;
    std::map<uint64_t, size_t> index_;
    std::array<std::string, 2> models_;
    float clock_ = 0;
};

} // namespace reluva::age
