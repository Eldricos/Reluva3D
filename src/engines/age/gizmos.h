// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/gfx3d.h"
#include "engines/age/collision.h"
#include "engines/age/pathset.h"
#include "engines/age/render_cache.h"
#include "engines/age/traffic.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

// The things that move by themselves along a city's path sets
// (race/<city>/<city>_ferry, _sailboat, _bridge and _train.pathset):
// ferries, tugs, water taxis, sailboards and ducks going round their
// paths, drawbridges whose leaves rise and fall (and the gates that stand
// still), and London's Underground trains. The way they move is Reluva's;
// see docs/engines/age-traffic.md.
class Gizmos {
public:
    Gizmos() = default;
    ~Gizmos() { clear(); }
    Gizmos(const Gizmos&) = delete;
    Gizmos& operator=(const Gizmos&) = delete;
    // Seconds of a drawbridge's cycle: down, rising, up, falling.
    static constexpr float kDown = 50, kRising = 8, kUp = 20, kFalling = 8;
    static constexpr float kRaised = 0.5f; // radians a leaf rises to
    // A train: its carriages (as London's aimap's commented-out
    // "[Subway] va_ug_l 3" has it), top speed (m/s), acceleration and
    // braking (m/s^2), and seconds standing at each end of its line.
    static constexpr int kCarriages = 3;
    static constexpr float kTrainSpeed = 14, kTrainAccel = 1, kTrainStand = 15;

    // The city's files, and a race's (<city>_bridge_<variant>.pathset...)
    // over them where it has its own: a path in both, the same model
    // starting within 5 m, is the race's.
    // With `mixer`, the trains are heard (aud/ambient/subwaycar.csv's loop).
    Error load(const SearchSet& files, const std::string& map, const std::string& variant = "",
               audio::Mixer* mixer = nullptr);
    void clear();

    // Moves them, and puts the leaves' road surfaces in `world` (as
    // moving triangles); the trains are heard from `listener`.
    void update(float dt, CollisionWorld& world, const Vec3& listener = {});
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye) const;
    // Lines of solid road users across each bridge that isn't down, and
    // along each train, for the traffic to stop at.
    void users(std::vector<RoadUser>& out) const;
    // A driven car against the trains (which don't give); returns the
    // hardest knock (change of speed, m/s), 0 if none.
    float collide(CarSim& car) const;
    // The trains shove the traffic out of their way.
    void push(Traffic& traffic) const;

    // A triangle of the city across a drawbridge's gap (its middle under a
    // leaf, near the road): taken out where the leaves are.
    bool inGap(const Vec3& a, const Vec3& b, const Vec3& c) const;

    size_t boats() const { return boats_.size(); }
    size_t bridges() const { return bridges_.size(); }
    size_t trains() const { return trains_.size(); }
    // Where a train's first carriage is, and how fast it's going.
    Vec3 trainPosition(size_t train) const { return trains_[train].carriages.front().center; }
    float trainSpeed(size_t train) const { return trains_[train].speed; }
    // 0 down .. 1 up, for a bridge.
    float raised(size_t bridge) const { return bridges_[bridge].raised; }

private:
    struct Boat {
        std::string model;
        std::vector<Vec3> points; // closed (back to the first) unless going back and forth
        std::vector<float> at;
        bool backAndForth = false;
        float speed = 2, s = 0, sway = 0; // sway: where in its bobbing it is
        int way = 1;
        float window = 3; // metres either side its heading is taken over
        Vec3 position{};
        Mat3 axes;
    };
    struct Leaf {
        Vec3 hinge{}, tip{}; // down
        float width = 8;
    };
    struct Bridge {
        std::string model;
        std::vector<Leaf> leaves;
        bool inactive = false,
             standing = false; // standing: modelled about its middle (a gate)
        float lift = 0;        // up off the path for a standing one
        float offset = 0;      // seconds into the cycle at clock 0
        float raised = 0;
    };
    struct Carriage {
        Vec3 center{};
        Mat3 axes; // x right, y up, z back (along the line, from its last point to its first)
    };
    struct Train {
        std::string model;
        std::vector<Vec3> points;
        std::vector<float> at;
        float length = 15, width = 3, height = 3, gap = 0.6f, lift = 0; // a carriage's
        float u = 0;                                                    // its end nearer the line's start, along it
        float speed = 0, standing = 0;
        int way = 1;
        std::vector<Carriage> carriages;
        float span() const { return float(kCarriages) * length + float(kCarriages - 1) * gap; }
        Vec3 velocity() const;
        // Spheres of `radius` along its carriages, for knocking the traffic.
        std::vector<Vec3> spheres(float radius) const;
    };
    struct Bounds {
        Vec3 low{}, high{};
    };
    Bounds boundsOf(const SearchSet& files, const std::string& model);
    void advance(float dt);
    void placeTrain(Train& t);
    Mat3 leafAxes(const Bridge& b, const Leaf& l, Vec3& center) const;

    std::vector<Boat> boats_;
    std::vector<Bridge> bridges_;
    std::vector<Train> trains_;
    audio::Mixer* mixer_ = nullptr;
    std::shared_ptr<audio::Sound> trainSound_;
    audio::SoundHandle trainPlaying_ = audio::kNoSound;
    std::map<std::string, Bounds> bounds_;
    float clock_ = 0;
};

} // namespace reluva::age
