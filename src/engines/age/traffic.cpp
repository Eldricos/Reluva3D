// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Ambient traffic on the road map (bai.h) as the aimap settles it
// (aimap.h); the rules are Reluva's, described in
// docs/engines/age-traffic.md.

#include "engines/age/traffic.h"

#include "engines/age/car_actor.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kAccel = 3;          // m/s^2
constexpr float kBrake = 7;          // m/s^2
constexpr float kLook = 30;          // metres ahead it slows for others
constexpr float kGap = 3;            // metres kept behind what's ahead
constexpr float kSpawnNear = 140;    // out of sight: no nearer than this
constexpr float kSpawnFar = 300;
constexpr float kForget = 340;       // left out beyond this
constexpr float kHoldLimit = 6;      // seconds held at a junction before going anyway
constexpr float kLooseFriction = 6;  // m/s^2 slowing a knocked car

float flat(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[2] * v[2]); }

std::vector<float> distances(const std::vector<Vec3>& points) {
    std::vector<float> at(points.size(), 0);
    for (size_t i = 1; i < points.size(); ++i)
        at[i] = at[i - 1] + length(points[i] - points[i - 1]);
    return at;
}

float yawOf(const Vec3& heading) { return std::atan2(-heading[0], -heading[2]); }

} // namespace

Vec3 pointAlong(const std::vector<Vec3>& points, const std::vector<float>& at, float s, Vec3* heading) {
    if (points.empty())
        return {};
    if (points.size() == 1) {
        if (heading)
            *heading = {0, 0, -1};
        return points[0];
    }
    s = std::clamp(s, 0.0f, at.back());
    size_t i = size_t(std::upper_bound(at.begin(), at.end(), s) - at.begin());
    i = std::clamp<size_t>(i, 1, points.size() - 1);
    const float seg = at[i] - at[i - 1];
    const float t = seg > 1e-4f ? (s - at[i - 1]) / seg : 0;
    if (heading) {
        // Smoothed over a few metres either side, so it turns rather than
        // snapping at each point.
        const Vec3 a = pointAlong(points, at, std::max(0.0f, s - 2), nullptr);
        const Vec3 b = pointAlong(points, at, std::min(at.back(), s + 2), nullptr);
        const Vec3 d = b - a;
        *heading = length(d) > 1e-3f ? normalized(d) : normalized(points[i] - points[i - 1]);
    }
    return points[i - 1] + (points[i] - points[i - 1]) * t;
}

void TrafficNetwork::build(const AiRoadMap& map, const AiMapSettings& settings) {
    lanes_.clear();
    leaving_.assign(map.junctions.size(), {});
    roadsAt_.clear();
    for (const AiJunction& j : map.junctions)
        roadsAt_.push_back(j.roads.size());
    for (const AiRoad& road : map.roads) {
        float speed = settings.speedLimit > 0 ? settings.speedLimit : road.speedLimit;
        bool spawns = true;
        for (const auto& e : settings.exceptions)
            if (e.road == road.id) {
                spawns = e.density > 0;
                if (e.speedLimit > 0)
                    speed = e.speedLimit;
            }
        for (int side = 0; side < 2; ++side) {
            // The lanes are stored the way they're driven where traffic
            // keeps right (side 1's from the road's first point to its
            // last, side 0's back); where it keeps left, the other way.
            const bool forward = (side == 1) != settings.driveOnLeft;
            for (const auto& points : road.sides[size_t(side)].lanes) {
                if (points.size() < 2)
                    continue;
                TrafficLane lane;
                lane.road = road.id;
                lane.side = side;
                lane.points = points;
                if (settings.driveOnLeft)
                    std::reverse(lane.points.begin(), lane.points.end());
                lane.at = distances(lane.points);
                lane.from = road.ends[forward ? 1 : 0].junction;
                lane.to = road.ends[forward ? 0 : 1].junction;
                lane.toSlot = road.ends[forward ? 0 : 1].slot;
                lane.speed = speed;
                lane.spawns = spawns;
                if (lane.length() < 1)
                    continue;
                leaving_[lane.from].push_back(uint32_t(lanes_.size()));
                lanes_.push_back(std::move(lane));
            }
        }
    }
}

void Traffic::setup(const SearchSet& files, const AiRoadMap& map, const AiMapSettings& settings, uint32_t seed) {
    network_.build(map, settings);
    holders_.assign(network_.junctions(), {});
    cars_.clear();
    kinds_.clear();
    rng_.seed(seed);
    for (const auto& a : settings.ambients) {
        if (!files.has("geometry/" + a.model + ".pkg"))
            continue;
        Kind k;
        k.model = a.model;
        k.upTo = a.upTo;
        std::vector<uint8_t> data;
        TuneNode root;
        if (!files.read("tune/vehicle/" + a.model + ".aiVehicleData", data) &&
            !parseTune(std::string(data.begin(), data.end()), root) && !root.children.empty()) {
            const TuneNode& n = root.children.front();
            for (size_t i = 0; i < 3; ++i)
                k.size[i] = n.number("Size", i, k.size[i]);
            k.mass = n.number("Mass", 0, k.mass);
        }
        kinds_.push_back(k);
    }
}

int Traffic::pickKind() {
    const float r = std::uniform_real_distribution<float>(0, 1)(rng_);
    for (size_t i = 0; i < kinds_.size(); ++i)
        if (r < kinds_[i].upTo)
            return int(i);
    return int(kinds_.size()) - 1;
}

int Traffic::pickNext(int lane) {
    // Any lane leaving the junction ahead but back along the same road,
    // unless that's the only way.
    const TrafficLane& l = network_.lanes()[size_t(lane)];
    const auto& out = network_.leaving(l.to);
    std::vector<uint32_t> ways;
    for (uint32_t k : out)
        if (network_.lanes()[k].road != l.road)
            ways.push_back(k);
    if (ways.empty())
        ways = out;
    if (ways.empty())
        return -1;
    return int(ways[std::uniform_int_distribution<size_t>(0, ways.size() - 1)(rng_)]);
}

void Traffic::startOn(Car& c, int lane, float s) {
    const TrafficLane& l = network_.lanes()[size_t(lane)];
    c.lane = lane;
    c.crossing = false;
    c.path = l.points;
    c.at = l.at;
    c.s = s;
    c.maxSpeed = l.speed;
    c.nextLane = pickNext(lane);
}

void Traffic::enterJunction(Car& c) {
    // A curve from the end of this lane to the start of the next, as long
    // as the gap between, slowed for how sharply it turns.
    const TrafficLane& from = network_.lanes()[size_t(c.lane)];
    const TrafficLane& to = network_.lanes()[size_t(c.nextLane)];
    const Vec3 p0 = from.points.back(), p1 = to.points.front();
    Vec3 d0 = normalized(from.points.back() - from.points[from.points.size() - 2]);
    Vec3 d1 = normalized(to.points[1] - to.points[0]);
    const float span = std::max(1.0f, length(p1 - p0));
    std::vector<Vec3> curve;
    for (int i = 0; i <= 10; ++i) {
        const float t = float(i) / 10, t2 = t * t, t3 = t2 * t;
        curve.push_back(p0 * (2 * t3 - 3 * t2 + 1) + d0 * ((t3 - 2 * t2 + t) * span) + p1 * (-2 * t3 + 3 * t2) +
                        d1 * ((t3 - t2) * span));
    }
    const float turn = std::acos(std::clamp(d0[0] * d1[0] + d0[2] * d1[2], -1.0f, 1.0f));
    c.crossing = true;
    c.path = std::move(curve);
    c.at = distances(c.path);
    c.s = 0;
    c.maxSpeed = std::max(5.0f, from.speed - (from.speed - 6) * std::min(1.0f, turn / (kPi / 2)));
}

bool Traffic::tryHold(Car& c, uint32_t junction) {
    // A junction is taken by the cars from one road at a time, or at a
    // crossroads from two opposite roads (the junction lists its roads in
    // order round it), as a green light would let them.
    auto& h = holders_[junction];
    const TrafficLane& mine = network_.lanes()[size_t(c.lane)];
    const bool crossroads = network_.roadsAt(junction) == 4;
    for (size_t lane : h) {
        const TrafficLane& other = network_.lanes()[lane];
        const bool together = other.road == mine.road || (crossroads && other.toSlot % 2 == mine.toSlot % 2);
        if (!together && c.waited < kHoldLimit)
            return false;
    }
    h.push_back(size_t(c.lane));
    c.holding = int(junction);
    return true;
}

void Traffic::release(Car& c) {
    if (c.holding < 0)
        return;
    auto& h = holders_[size_t(c.holding)];
    const auto it = std::find(h.begin(), h.end(), size_t(c.lane));
    if (it != h.end())
        h.erase(it);
    c.holding = -1;
}

void Traffic::place(Car& c) {
    Vec3 heading;
    c.position = pointAlong(c.path, c.at, c.s, &heading);
    c.heading = heading;
    c.yaw = yawOf(heading);
}

void Traffic::update(float dt, const Vec3& focus, const std::vector<RoadUser>& others, const CollisionWorld& world,
                     bool fill) {
    if (kinds_.empty() || network_.lanes().empty())
        return;
    // Leave out those far away.
    for (size_t i = 0; i < cars_.size();) {
        if (flat(cars_[i].position - focus) > kForget) {
            release(cars_[i]);
            cars_[i] = std::move(cars_.back());
            cars_.pop_back();
        } else {
            ++i;
        }
    }
    // Start new ones, out of sight unless filling.
    const size_t wanted = size_t(std::lround(std::clamp(density_, 0.0f, 1.0f) * kCarsAtFull));
    const auto& lanes = network_.lanes();
    std::uniform_int_distribution<size_t> anyLane(0, lanes.size() - 1);
    for (int tries = fill ? 4000 : 200; tries > 0 && cars_.size() < wanted; --tries) {
        const size_t li = anyLane(rng_);
        const TrafficLane& l = lanes[li];
        if (!l.spawns)
            continue;
        const float s = std::uniform_real_distribution<float>(0, l.length())(rng_);
        const Vec3 p = pointAlong(l.points, l.at, s);
        const float d = flat(p - focus);
        if (d > kSpawnFar || d < (fill ? 25 : kSpawnNear))
            continue;
        bool clear = true;
        for (const Car& o : cars_)
            clear = clear && flat(o.position - p) > 14;
        for (const RoadUser& u : others)
            clear = clear && flat(u.position - p) > 20;
        if (!clear)
            continue;
        Car c;
        c.kind = pickKind();
        c.paintJob = uint32_t(rng_() % 8);
        startOn(c, int(li), s);
        c.speed = c.maxSpeed * 0.8f;
        place(c);
        cars_.push_back(std::move(c));
    }

    for (Car& c : cars_) {
        if (c.loose) {
            // Sliding to a stop where it was knocked.
            const float v = flat(c.velocity);
            const float slow = std::min(v, kLooseFriction * dt);
            if (v > 1e-3f)
                c.velocity = c.velocity * ((v - slow) / v);
            c.velocity[1] = 0;
            c.position += c.velocity * dt;
            c.yaw += c.spin * dt;
            c.spin *= std::max(0.0f, 1 - 2 * dt);
            const Vec3 from = c.position + Vec3{0, 2, 0};
            CollisionWorld::RayHit hit;
            if (world.raycast(from, {0, -1, 0}, 6, hit))
                c.position[1] = hit.point[1];
            continue;
        }
        // How fast it may go: its lane's limit (or the curve's), what's
        // ahead, and whether the junction ahead is its to cross.
        float target = c.maxSpeed;
        if (c.ghost > 0) {
            c.ghost -= dt;
        } else {
            float nearest = kLook;
            auto consider = [&](const Vec3& p) {
                const Vec3 rel = p - c.position;
                const float ahead = rel[0] * c.heading[0] + rel[2] * c.heading[2];
                if (ahead <= 0 || ahead > nearest)
                    return;
                const float side = flat(rel - c.heading * ahead);
                if (side < 2.2f && std::fabs(rel[1]) < 4)
                    nearest = ahead;
            };
            for (const Car& o : cars_)
                if (&o != &c)
                    consider(o.position);
            for (const RoadUser& u : others)
                consider(u.position);
            if (nearest < kLook) {
                const float room = std::max(0.0f, nearest - 5 - kGap);
                target = std::min(target, std::sqrt(2 * kBrake * room));
            }
        }
        if (!c.crossing && c.holding < 0 && c.nextLane >= 0) {
            const float left = c.at.back() - c.s;
            const float stopping = c.speed * c.speed / (2 * kBrake) + 4;
            if (left < stopping && !tryHold(c, network_.lanes()[size_t(c.lane)].to))
                target = std::min(target, std::sqrt(2 * kBrake * std::max(0.0f, left - 2)));
        }
        if (target < 0.5f && c.speed < 0.5f) {
            c.waited += dt;
            if (c.waited > 10 && c.ghost <= 0)
                c.ghost = 3; // held up for long by something that isn't moving: go on through
        } else if (c.speed > 2) {
            c.waited = 0;
        }
        c.speed = c.speed < target ? std::min(target, c.speed + kAccel * dt) : std::max(target, c.speed - kBrake * dt);
        c.s += c.speed * dt;
        // On along its way: across the junction, then onto the next lane.
        while (c.s >= c.at.back()) {
            const float over = c.s - c.at.back();
            if (c.nextLane < 0) { // a dead end: turn round on another lane later
                c.s = c.at.back();
                c.speed = 0;
                break;
            }
            if (!c.crossing) {
                if (c.holding < 0 && !tryHold(c, network_.lanes()[size_t(c.lane)].to)) {
                    c.s = c.at.back();
                    c.speed = 0;
                    break;
                }
                enterJunction(c);
                c.s = over;
            } else {
                release(c);
                startOn(c, c.nextLane, over);
            }
        }
        place(c);
    }
}

float Traffic::collide(CarSim& car) {
    float hardest = 0;
    float radius;
    const auto spheres = car.bumpers(radius);
    const float ma = car.setup().mass;
    for (Car& c : cars_) {
        const Kind& k = kinds_[size_t(c.kind)];
        if (flat(c.position - car.position) > k.size[2] + 6)
            continue;
        const Mat3 turn = Mat3::rotationY(c.yaw);
        const Vec3 right = turn.x, back = turn.z;
        const float hw = k.size[0] / 2, hl = k.size[2] / 2;
        for (const Vec3& p : spheres) {
            const Vec3 d = p - c.position;
            if (d[1] < -1 || d[1] > k.size[1] + 1.5f)
                continue;
            const float lx = dot(d, right), lz = dot(d, back);
            const float cx = std::clamp(lx, -hw, hw), cz = std::clamp(lz, -hl, hl);
            float nx = lx - cx, nz = lz - cz;
            float dist = std::sqrt(nx * nx + nz * nz);
            float depth;
            if (dist > 1e-4f) {
                depth = radius - dist;
                nx /= dist;
                nz /= dist;
            } else { // the middle inside: out the nearest side
                const float ox = hw - std::fabs(lx), oz = hl - std::fabs(lz);
                if (ox < oz) {
                    nx = lx < 0 ? -1.0f : 1.0f;
                    nz = 0;
                    depth = ox + radius;
                } else {
                    nx = 0;
                    nz = lz < 0 ? -1.0f : 1.0f;
                    depth = oz + radius;
                }
            }
            if (depth <= 0)
                continue;
            Vec3 n = right * nx + back * nz; // from the traffic car to the driven one
            n[1] = 0;
            n = normalized(n);
            const float mb = k.mass;
            car.position += n * (depth * mb / (ma + mb));
            c.position -= n * (depth * ma / (ma + mb));
            const Vec3 vb = c.loose ? c.velocity : c.heading * c.speed;
            const float closing = dot(car.velocity - vb, n);
            if (closing < 0) {
                const float j = -1.3f * closing / (1 / ma + 1 / mb);
                car.velocity += n * (j / ma);
                if (!c.loose) {
                    // Knocked off its lane: it slides to a stop and stays.
                    release(c);
                    c.loose = true;
                    c.velocity = vb;
                }
                c.velocity -= n * (j / mb);
                const Vec3 arm = c.position - (p - n * radius);
                const float inertia = mb * (k.size[0] * k.size[0] + k.size[2] * k.size[2]) / 12;
                c.spin += (arm[2] * n[0] - arm[0] * n[2]) * j / inertia;
                hardest = std::max(hardest, j / ma);
            }
            break;
        }
    }
    return hardest;
}

void Traffic::users(std::vector<RoadUser>& out) const {
    for (const Car& c : cars_)
        out.push_back({c.position, c.loose ? c.velocity : c.heading * c.speed});
}

void Traffic::draw(gfx::Renderer3D& renderer, ModelCache& models, const SceneLight& light,
                   const gfx::Mat4& viewProj, const Vec3& eye) {
    const ViewFrustum frustum(viewProj);
    for (bool translucent : {false, true})
        for (const Car& c : cars_) {
            const ModelCache::Model& m = models.get(kinds_[size_t(c.kind)].model, c.paintJob);
            if (!m.ok)
                continue;
            const float d = length(c.position - eye);
            if (!frustum.visible(c.position + Vec3{0, 1, 0}, m.radius + 1))
                continue;
            const size_t level = std::min(m.levels.size() - 1, size_t(d < 40 ? 0 : d < 100 ? 1 : d < 200 ? 2 : 3));
            // Upright, pitched along its way while driven.
            Vec3 ahead = c.loose ? Mat3::rotationY(c.yaw) * Vec3{0, 0, -1} : c.heading;
            Vec3 right = cross(ahead, {0, 1, 0});
            right = length(right) > 1e-3f ? normalized(right) : Vec3{1, 0, 0};
            const Vec3 up = cross(right, ahead);
            const Mat3 axes{right, up, -ahead};
            drawModelMesh(renderer, m.levels[level], matrixOf(axes, c.position), light, translucent);
        }
}

} // namespace reluva::age
