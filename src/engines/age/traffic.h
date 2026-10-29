// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/aimap.h"
#include "engines/age/bai.h"
#include "engines/age/car.h"
#include "engines/age/collision.h"
#include "engines/age/pathset.h"
#include "engines/age/render_cache.h"
#include "engines/age/traffic_lights.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

// Something on the road that traffic slows for and keeps clear of: the
// player, opponents, police.
struct RoadUser {
    Vec3 position{}, velocity{};
    bool solid = false; // never driven through, even after a long wait (an open bridge)
};

// A lane of the road map in the direction it's driven: side 1's lanes run
// from a road's first point to its last where traffic keeps to the right,
// side 0's the other way (and the other way round where it keeps left).
struct TrafficLane {
    uint32_t road = 0;
    int side = 0;
    std::vector<Vec3> points;
    std::vector<float> at; // distance along it to each point
    uint32_t from = 0, to = 0; // junctions
    uint32_t toSlot = 0;       // the road's place in the list of junction `to`
    int toEnd = 0;             // the road's end it arrives at (0 its last point, 1 its first)
    uint16_t control = 3;      // that end's: 1 lights, 0 give way, 3 none (as read: see bai.md)
    int left = -1, right = -1; // the lanes beside it going the same way
    float speed = 15;          // m/s
    bool spawns = true;        // traffic starts on it (a race's exceptions leave some out)
    bool open = true;          // traffic goes on it: not a road the road map flags 0x2 (bai.md)
    float length() const { return at.empty() ? 0 : at.back(); }
};

// The lanes of a road map and which leave each junction.
class TrafficNetwork {
public:
    void build(const AiRoadMap& map, const AiMapSettings& settings);
    const std::vector<TrafficLane>& lanes() const { return lanes_; }
    const std::vector<uint32_t>& leaving(uint32_t junction) const { return leaving_[junction]; }
    // The ways on from a lane's end: open lanes onto other roads, else
    // any onto other roads, else any (back along the same road).
    std::vector<uint32_t> waysOn(int lane) const;
    size_t roadsAt(uint32_t junction) const { return roadsAt_[junction]; }
    size_t junctions() const { return leaving_.size(); }

private:
    std::vector<TrafficLane> lanes_;
    std::vector<std::vector<uint32_t>> leaving_;
    std::vector<size_t> roadsAt_;
};

// Where a path is `s` along it, and which way it's heading there.
Vec3 pointAlong(const std::vector<Vec3>& points, const std::vector<float>& at, float s, Vec3* heading = nullptr);

// The game's ambient cars (va_*): picked by the aimap's shares, driven
// along the lanes through the junctions, stopping for red lights and giving
// way, taking turns at each, slowing for what's ahead and changing lanes
// round it. Knocked by a driven car they slide to a stop and stay. Their
// lamps show: brake lights, indicators, and headlights at night. And the
// city's parked cars, which stay where they are until knocked.
// Reluva's own driving (the game's is in its encrypted program); see
// docs/engines/age-traffic.md.
class Traffic {
public:
    // tune/vehicle/<model>.aiVehicleData for each kind the aimap lists.
    void setup(const SearchSet& files, const AiRoadMap& map, const AiMapSettings& settings, uint32_t seed);
    void clear() {
        cars_.clear();
        parked_ = 0;
    }
    // 0..1: how many cars there are about the focus (1 = kCarsAtFull).
    void setDensity(float density) { density_ = density; }
    float density() const { return density_; }
    // The lights they stop for (none: every junction is taken in turns).
    void setLights(const TrafficLights* lights) { lights_ = lights; }
    void setLaneChanges(bool on) { laneChanges_ = on; }
    // Cars parked along `lines` (race/<city>/<city>_parkedcar.pathset): nose
    // to tail beside a road, facing from a line's first point to its last,
    // else side by side; a line's own model, else one of `models` (none
    // there: the aimap's ambient cars up to 3 tonnes). Replaces those parked
    // before; after setup().
    void park(const SearchSet& files, const std::vector<PathsetPath>& lines, const std::vector<std::string>& models);

    // Moves the cars, starting new ones about `focus` (the player) and
    // leaving out those far from it. `fill` starts them anywhere round it
    // rather than only out of sight (a new race, a new city).
    void update(float dt, const Vec3& focus, const std::vector<RoadUser>& others, const CollisionWorld& world,
                bool fill = false);
    // A driven car bumping into the traffic. Returns the hardest knock
    // (change of speed, m/s), 0 if none.
    float collide(CarSim& car);
    // The same for any body: `spheres` of `radius` on a body of `mass`
    // (moved and changed as it's pushed back; a very heavy one, a train,
    // just pushes).
    float collide(const std::vector<Vec3>& spheres, float radius, float mass, Vec3& position, Vec3& velocity);
    // `night`: their headlights and tail lights on, and flares at the
    // headlights (with `flares`).
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light, const gfx::Mat4& viewProj,
              const Vec3& eye, bool night = false, const FlareDrawer* flares = nullptr);
    // Where their headlights light the road ahead, for those within
    // `distance` of `eye`.
    void headlightPools(std::vector<gfx::DynamicLight>& out, const Vec3& eye, float distance = 120) const;

    size_t count() const { return cars_.size() - parked_; } // those driving
    size_t parked() const { return parked_; }
    // Where the parked cars stand and which way they face (knocked or not).
    void parkedPlaces(std::vector<std::pair<Vec3, Vec3>>& out) const;
    void users(std::vector<RoadUser>& out) const; // for others to keep clear of
    const TrafficNetwork& network() const { return network_; }

    static constexpr int kCarsAtFull = 60;

private:
    struct Kind {
        std::string model;
        float upTo = 1;
        Vec3 size{1.8f, 1.4f, 4.5f}; // width, height, length
        float mass = 1000;
    };
    int kindOf(const SearchSet& files, const std::string& model);
    // Whether a line from `a` to `b` runs beside a lane, within 5 m.
    bool besideLane(const Vec3& a, const Vec3& b) const;
    struct Car {
        int kind = 0;
        uint32_t paintJob = 0;
        // On a lane, or on the curve across a junction to `nextLane`.
        int lane = -1, nextLane = -1;
        bool crossing = false;
        std::vector<Vec3> path;
        std::vector<float> at;
        float s = 0, speed = 0, maxSpeed = 15;
        float pace = 1;     // its share of the speed limit (lorries and buses slower)
        int holding = -1;   // the junction it may cross (or is crossing)
        float waited = 0;   // seconds held up
        float ghost = 0;    // seconds left ignoring what's ahead (after a long wait)
        bool gaveWay = false; // stopped at a give-way end
        int turn = 0;       // which way it turns at the junction ahead: -1 left, 1 right
        // Changing lanes: where it was, moved onto the new lane over the
        // time left; and which side it's indicating, for how long.
        Vec3 shift{};
        float shiftLeft = 0, laneWait = 0;
        int indicate = 0;
        float indicateTime = 0;
        bool braking = false;
        // Its place: from the path while driven, its own once knocked.
        Vec3 position{}, heading{0, 0, -1};
        float yaw = 0;
        bool loose = false;
        Vec3 velocity{};
        float spin = 0;
        bool parked = false, settled = false; // settled: stood on the ground
    };

    int pickKind();
    int pickNext(int lane);
    void startOn(Car& c, int lane, float s);
    void enterJunction(Car& c);
    void place(Car& c);
    bool tryHold(Car& c, uint32_t junction);
    bool mayEnter(Car& c, bool canStop);
    void changeLane(Car& c, const std::vector<RoadUser>& others);
    void release(Car& c);

    TrafficNetwork network_;
    std::vector<Kind> kinds_;
    size_t ambient_ = 0; // the first kinds: the aimap's, the rest parked only
    size_t parked_ = 0;
    std::vector<Car> cars_;
    std::vector<std::vector<size_t>> holders_; // per junction: lanes the cars crossing it came from
    std::mt19937 rng_;
    float density_ = 0;
    const TrafficLights* lights_ = nullptr;
    bool laneChanges_ = false;
    float clock_ = 0;
};

} // namespace reluva::age
