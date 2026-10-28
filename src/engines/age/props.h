// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/car.h"
#include "engines/age/collision.h"
#include "engines/age/pathset.h"
#include "engines/age/render_cache.h"
#include "engines/age/traffic_lights.h"

#include <functional>
#include <string>
#include <vector>

namespace reluva::age {

// A prop's place: its model, the point it stands on and the way it
// faces (its X axis along `along`, flat).
struct PropPlacement {
    std::string model;
    Vec3 at{};
    Vec3 along{1, 0, 0};
};
// city/<city>/props.pathset as Reluva reads it: a path whose end word's
// low byte is 0 is a prop at each point; 1, a prop at every other point
// facing the next (a point a metre or two away); 2, props along the line
// every (end word's second byte / 10) metres, along it. See
// docs/formats/age/pathset.md.
std::vector<PropPlacement> placeProps(const std::vector<PathsetPath>& paths);

// tune/banger/<model>.dgBangerData: what Reluva takes from it.
struct BangerData {
    float mass = 100;
    float impulseLimit2 = 0; // ImpulseLimit2: the square of the impulse that breaks it loose
};
Error parseBangerData(const std::string& text, BangerData& out);

// The city's props and its traffic lights, as things a car can knock
// down: standing, collided with (their bounds, else their models' boxes);
// knocked hard enough (CarSim::broke), they fall over where they were
// pushed and are no longer in the way. See docs/engines/age-traffic.md.
class Props {
public:
    void clear() { props_.clear(); }
    // city/<map>/props.pathset's props.
    void load(const SearchSet& files, const std::string& map);
    // The traffic lights, as poles.
    void addLights(const TrafficLights& lights);
    // Their shapes and banger data, once the models are at hand
    // (`box`: a model's bounds, false if there's no such model).
    using ModelBox = std::function<bool(const std::string& model, Vec3& low, Vec3& high)>;
    void prepare(const SearchSet& files, ModelCache& models);
    void prepare(const SearchSet& files, const ModelBox& box);
    bool prepared() const { return prepared_; }
    // Their triangles into `world` (any put there before taken out first).
    void collide(CollisionWorld& world) const;
    // The props the cars broke loose come away (out of `world`; a traffic
    // light out of `lights`), then the loose ones fall.
    void update(float dt, const std::vector<CarSim*>& cars, CollisionWorld& world, TrafficLights& lights);
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye) const;

    size_t count() const { return props_.size(); }
    size_t loose() const;

private:
    struct Prop {
        std::string model;
        Vec3 at{};                                  // the point it stands on
        Mat3 axes;                                  // its own
        float lift = 0;                             // raises the model (modelled about its middle) to stand on `at`
        int light = -1;                             // a traffic light's number
        std::vector<std::array<Vec3, 3>> triangles; // in the world
        CollisionSurface surface;
        float height = 1;
        // Loose: falling over about `axis` through `at`, sliding.
        bool loose = false;
        Vec3 axis{1, 0, 0}, slide{};
        float angle = 0, spin = 0, fallTo = 0;
        Vec3 moved{};
    };
    std::vector<Prop> props_;
    bool prepared_ = false;
};

} // namespace reluva::age
