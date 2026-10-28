// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/traffic_lights.h"

#include "engines/age/car_actor.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace reluva::age {

void TrafficLights::build(const AiRoadMap& map, const AiMapSettings& settings) {
    lights_.clear();
    index_.clear();
    models_ = settings.trafficLights;
    junctions_.assign(map.junctions.size(), {});
    // Each junction's controlled road slots, for their phases.
    std::vector<std::set<uint32_t>> slots(map.junctions.size());
    for (const AiRoad& road : map.roads)
        for (const AiRoadEnd& e : road.ends)
            if (e.control == 1 && length(e.b - e.a) > 0.5f)
                slots[e.junction].insert(e.slot);
    for (size_t j = 0; j < map.junctions.size(); ++j) {
        const bool crossroads = map.junctions[j].roads.size() == 4;
        junctions_[j].phases = crossroads ? 2 : std::max(2, int(slots[j].size()));
        // Out of step with each other, the same each time.
        junctions_[j].offset = float((j * 7919) % 97) / 97 * 60;
    }
    for (const AiRoad& road : map.roads) {
        for (int end = 0; end < 2; ++end) {
            const AiRoadEnd& e = road.ends[size_t(end)];
            Vec3 across = e.b - e.a; // over the road from the curb
            if (e.control != 1 || length(across) < 0.5f)
                continue;
            Light l;
            l.road = road.id;
            l.end = end;
            l.junction = e.junction;
            const bool crossroads = map.junctions[e.junction].roads.size() == 4;
            l.phase = crossroads ? int(e.slot % 2)
                                 : int(std::distance(slots[e.junction].begin(), slots[e.junction].find(e.slot)));
            l.model = road.sides[end == 0 ? 1 : 0].lanes.size() + road.sides[end == 0 ? 0 : 1].lanes.size() > 2;
            l.at = e.a;
            if (settings.driveOnLeft && !road.middle.empty()) {
                // Placed for traffic keeping right: on the other curb where
                // it keeps left, facing the same way (London's light is a
                // pole without an arm).
                const Vec3 mid = end == 0 ? road.middle.back() : road.middle.front();
                l.at = Vec3{2 * mid[0] - e.a[0], e.a[1], 2 * mid[2] - e.a[2]};
            }
            across[1] = 0;
            const Vec3 x = normalized(-across);
            const Vec3 y{0, 1, 0};
            l.axes = {x, y, cross(x, y)};
            index_[key(l.road, l.end)] = lights_.size();
            lights_.push_back(l);
        }
    }
}

Signal TrafficLights::signalOf(const Light& l) const {
    const Junction& j = junctions_[l.junction];
    const float step = kGreen + kYellow + kAllRed;
    const float t = std::fmod(clock_ + j.offset, step * float(j.phases));
    const int current = int(t / step);
    const float into = t - float(current) * step;
    if (current != l.phase || into >= kGreen + kYellow)
        return Signal::Red;
    return into < kGreen ? Signal::Green : Signal::Yellow;
}

Signal TrafficLights::signal(uint32_t road, int end) const {
    const auto it = index_.find(key(road, end));
    return it == index_.end() ? Signal::Green : signalOf(lights_[it->second]);
}

void TrafficLights::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light,
                         const gfx::Mat4& viewProj, const Vec3& eye, bool night, float distance) const {
    const ViewFrustum frustum(viewProj);
    const char* const glows[3] = {"GREENGLOW", "YELLOWGLOW", "REDGLOW"};
    const std::string when = night ? "NIGHT" : "DAY";
    for (bool translucent : {false, true})
        for (const Light& l : lights_) {
            const float d = length(l.at - eye);
            const std::string& name = models_[size_t(l.model)];
            const ModelCache::Model& m = models.get(name);
            if (!m.ok || l.down || d > distance || !frustum.visible(l.at + Vec3{0, 4, 0}, 8))
                continue;
            // The body is modelled about its middle, the glows as if it
            // stood on its origin: it's stood on the ground.
            const ModelMesh& body = m.levels[std::min(m.levels.size() - 1, size_t(d < 60 ? 0 : d < 150 ? 1 : 2))];
            const Vec3 lift{0, -m.levels.front().low[1], 0};
            if (!translucent) {
                drawModelMesh(renderer, body, matrixOf(l.axes, l.at + lift), light, false);
                continue;
            }
            drawModelMesh(renderer, body, matrixOf(l.axes, l.at + lift), light, true);
            const Signal s = signalOf(l);
            const gfx::Mat4 at = matrixOf(l.axes, l.at);
            drawModelMesh(renderer, models.part(name, glows[int(s)] + when), at, light, true);
            // The pedestrians' signal: walk while the road has red.
            const std::string walk = (s == Signal::Red ? "WALK_" : "NOWALK_") + when;
            drawModelMesh(renderer, models.part(name, walk), at, light, true);
        }
}

} // namespace reluva::age
