// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/bai.h"
#include "engines/age/car.h"
#include "engines/age/collision.h"
#include "engines/age/pathset.h"
#include "engines/age/render_cache.h"
#include "engines/age/traffic_lights.h"

#include <functional>
#include <map>
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

// The street furniture along the roads' sidewalks: city/<city>/propdefs.csv
// names the kinds (a street light, a meter...), proprules.csv which kinds
// line each side of a road under rules n01left... and each road block's
// second byte (PsdlBlock::extra) is its rule's number. See
// docs/formats/age/pathset.md.
struct StreetPropKind {
    float start = 0;   // metres along the side to the first
    float spacing = 0; // and between them
    int most = 0;      // maxUse: at most this many on a side
    float minLerp = 0; // across the sidewalk from its curb (0) to its outer edge (1)
    float maxLerp = 0;
    std::vector<std::string> models; // one picked for each
};
std::map<std::string, StreetPropKind> parsePropDefs(const std::string& text);
std::map<std::string, std::vector<std::string>> parsePropRules(const std::string& text);
// Each road's sides by its blocks' rule (`blockRules[block]`, 0 none):
// side 0 (on the left from its first point) by rule n<NN>left, side 1 by
// n<NN>right.
std::vector<PropPlacement> placeStreetProps(const AiRoadMap& roads, const std::vector<uint8_t>& blockRules,
                                            const std::map<std::string, StreetPropKind>& kinds,
                                            const std::map<std::string, std::vector<std::string>>& rules,
                                            uint32_t seed = 1);

// tune/banger/<model>.dgBangerData: what Reluva takes from it.
struct BangerData {
    float mass = 100;
    float impulseLimit2 = 0; // ImpulseLimit2: the square of the impulse that breaks it loose
    Vec3 size{}, cg{};       // its box's size, and its middle from the prop's foot
    int parts = 0;           // NumParts: its break pieces (<model>_BREAK01... in its .pkg)
};
Error parseBangerData(const std::string& text, BangerData& out);

// The city's props and its traffic lights, as things a car can knock
// down: standing, collided with (their bounds, else their models' boxes);
// knocked hard enough (CarSim::broke), they fall over where they were
// pushed and are no longer in the way. See docs/engines/age-traffic.md.
class Props {
public:
    void clear() { props_.clear(); }
    // city/<map>/props.pathset's props, and a race's own
    // (race/<map>/<variant>.pathset: "circuit0"...).
    void load(const SearchSet& files, const std::string& map, const std::string& variant = "");
    // The street furniture (placeStreetProps from city/<map>/propdefs.csv
    // and proprules.csv).
    void addStreetProps(const SearchSet& files, const std::string& map, const AiRoadMap& roads,
                        const std::vector<uint8_t>& blockRules);
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
    // Pieces of props come apart, and those come to rest; the lowest
    // height any of them is at (for tests).
    size_t pieces(size_t* resting = nullptr, float* lowest = nullptr) const;

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
        // Its break pieces (BREAK01...), each on its own once it comes
        // apart: a body falling and tumbling to rest on the ground.
        struct Piece {
            std::string part;
            Vec3 cg{}, size{}; // from the banger data
            Vec3 position{}, velocity{}, spin{};
            Mat3 orientation;
            bool resting = false;
        };
        std::vector<Piece> pieces;
        bool apart = false;
    };
    std::vector<Prop> props_;
    bool prepared_ = false;
};

} // namespace reluva::age
