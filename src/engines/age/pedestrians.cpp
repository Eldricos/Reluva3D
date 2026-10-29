// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Pedestrians on the road map's sidewalks; the rules are Reluva's,
// described in docs/engines/age-traffic.md.

#include "engines/age/pedestrians.h"

#include "engines/age/car_actor.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kSpawnNear = 70, kSpawnFar = 120, kForget = 140;
constexpr float kDrawDistance = 120;
constexpr float kCorner = 12;    // metres to another sidewalk's end to walk on along it
constexpr float kLookAhead = 1.6f; // seconds ahead a car is seen coming
constexpr float kMiss = 2.4f;      // metres it has to pass by not to be dived from

float flat(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[2] * v[2]); }
float yawOf(const Vec3& d) { return std::atan2(-d[0], -d[2]); }

std::vector<float> distances(const std::vector<Vec3>& points) {
    std::vector<float> at(points.size(), 0);
    for (size_t i = 1; i < points.size(); ++i)
        at[i] = at[i - 1] + length(points[i] - points[i - 1]);
    return at;
}

} // namespace

Error Pedestrians::setup(gfx::Renderer3D& renderer, const SearchSet& files, const AiRoadMap& map,
                         const AiMapSettings& settings, bool rain, uint32_t seed) {
    unload();
    rng_.seed(seed);
    // Every sidewalk's middle line.
    for (const AiRoad& road : map.roads)
        for (const AiRoadSide& side : road.sides) {
            // Kind 3 sides have no sidewalk (the Underground, ramps).
            if (side.sidewalk.size() < 2 || side.kind == 3)
                continue;
            Walk w;
            w.points = side.sidewalk;
            w.at = distances(w.points);
            if (w.at.back() < 2)
                continue;
            if (!side.curb.empty() && !side.outerEdge.empty())
                w.width = std::max(0.5f, flat(side.curb.front() - side.outerEdge.front()));
            walks_.push_back(std::move(w));
        }
    Error first = Error::success();
    for (const auto& pair : settings.peds) {
        auto model = std::make_unique<PedModel>();
        const Error err = model->load(renderer, files, rain ? pair.bad : pair.good,
                                      {"WALK", "STAND", "STAND_WALK", "WALK_LDIVE", "WALK_RDIVE"});
        if (err) {
            if (!first)
                first = err;
            continue;
        }
        if (model->move("WALK") >= 0)
            models_.push_back(std::move(model));
    }
    return models_.empty() ? first : Error::success();
}

void Pedestrians::unload() {
    peds_.clear();
    models_.clear();
    walks_.clear();
}

void Pedestrians::startMove(Ped& p, int move) {
    p.move = move;
    p.time = 0;
}

void Pedestrians::placeOnWalk(Ped& p) {
    const Walk& w = walks_[size_t(p.walk)];
    Vec3 heading;
    const Vec3 c = pointAlong(w.points, w.at, p.s, &heading);
    if (!p.ahead)
        heading = -heading;
    Vec3 across = normalized(Vec3{-heading[2], 0, heading[0]});
    p.position = c + across * (p.side * w.width);
    p.yaw = yawOf(heading);
}

void Pedestrians::nextWalk(Ped& p) {
    // On round the corner along a sidewalk that starts or ends near here,
    // else back the way it came.
    const Walk& w = walks_[size_t(p.walk)];
    const Vec3 here = p.ahead ? w.points.back() : w.points.front();
    std::vector<std::pair<int, bool>> ways;
    for (size_t i = 0; i < walks_.size(); ++i) {
        if (int(i) == p.walk)
            continue;
        if (flat(walks_[i].points.front() - here) < kCorner)
            ways.push_back({int(i), true});
        if (flat(walks_[i].points.back() - here) < kCorner)
            ways.push_back({int(i), false});
    }
    if (ways.empty()) {
        p.ahead = !p.ahead;
        return;
    }
    const auto [walk, ahead] = ways[std::uniform_int_distribution<size_t>(0, ways.size() - 1)(rng_)];
    p.walk = walk;
    p.ahead = ahead;
    p.s = ahead ? 0 : walks_[size_t(walk)].at.back();
}

void Pedestrians::update(float dt, const Vec3& focus, const std::vector<RoadUser>& cars, const CollisionWorld& world,
                         bool fill) {
    if (models_.empty() || walks_.empty())
        return;
    for (size_t i = 0; i < peds_.size();) {
        if (flat(peds_[i].position - focus) > kForget) {
            peds_[i] = peds_.back();
            peds_.pop_back();
        } else {
            ++i;
        }
    }
    const size_t wanted = size_t(std::lround(std::clamp(density_, 0.0f, 1.0f) * kPedsAtFull));
    std::uniform_int_distribution<size_t> anyWalk(0, walks_.size() - 1);
    std::uniform_real_distribution<float> unit(0, 1);
    for (int tries = fill ? 4000 : 100; tries > 0 && peds_.size() < std::min<size_t>(wanted, 60); --tries) {
        Ped p;
        p.walk = int(anyWalk(rng_));
        const Walk& w = walks_[size_t(p.walk)];
        p.s = unit(rng_) * w.at.back();
        p.ahead = unit(rng_) < 0.5f;
        p.side = (unit(rng_) - 0.5f) * 0.5f;
        placeOnWalk(p);
        const float d = flat(p.position - focus);
        if (d > kSpawnFar || d < (fill ? 5 : kSpawnNear))
            continue;
        p.model = int(rng_() % models_.size());
        p.paintJob = rng_() % std::max(1u, models_[size_t(p.model)]->paintJobs());
        startMove(p, models_[size_t(p.model)]->move("WALK"));
        p.time = unit(rng_);
        peds_.push_back(p);
    }

    for (Ped& p : peds_) {
        const PedModel& model = *models_[size_t(p.model)];
        p.time += dt;
        if (p.walking) {
            // A car coming at it: dive to the side away from its way.
            for (const RoadUser& c : cars) {
                const Vec3 rel = p.position - c.position;
                const float v2 = c.velocity[0] * c.velocity[0] + c.velocity[2] * c.velocity[2];
                if (v2 < 9 || flat(rel) > 30 || std::fabs(rel[1]) > 3)
                    continue;
                const float t = (rel[0] * c.velocity[0] + rel[2] * c.velocity[2]) / v2;
                if (t < 0 || t > kLookAhead)
                    continue;
                const Vec3 miss = rel - c.velocity * t;
                if (flat(miss) > kMiss)
                    continue;
                // Away: across the car's way, on the side it's already on.
                Vec3 away{-c.velocity[2], 0, c.velocity[0]};
                if (away[0] * rel[0] + away[2] * rel[2] < 0)
                    away = -away;
                const Vec3 right = Mat3::rotationY(p.yaw) * Vec3{1, 0, 0};
                const int dive = model.move(dot(away, right) > 0 ? "WALK_RDIVE" : "WALK_LDIVE");
                if (dive >= 0) {
                    p.walking = false;
                    startMove(p, dive);
                }
                break;
            }
        }
        if (p.walking) {
            if (p.idle > 0) {
                p.idle -= dt;
                continue;
            }
            const Walk& w = walks_[size_t(p.walk)];
            const float speed = std::fabs(model.moveAt(p.move).travel[2]);
            p.s += (p.ahead ? speed : -speed) * dt;
            if (p.s > w.at.back() || p.s < 0) {
                nextWalk(p);
                p.s = std::clamp(p.s, 0.0f, walks_[size_t(p.walk)].at.back());
            }
            placeOnWalk(p);
            continue;
        }
        // Diving and getting up: carried by the moves, on the ground.
        const PedModel::Move& m = model.moveAt(p.move);
        p.position += Mat3::rotationY(p.yaw) * m.travel * dt;
        PedModel::Move mv = m;
        if (p.time * PedModel::kFrameRate >= float(mv.frames - 1) && mv.next != p.move) {
            startMove(p, mv.next);
        } else if (p.move == model.move("STAND") && p.time > 1) {
            // Up again: back to the nearest point of its sidewalk.
            const Walk& w = walks_[size_t(p.walk)];
            float best = 1e9f;
            for (size_t i = 0; i < w.points.size(); ++i) {
                const float d = flat(w.points[i] - p.position);
                if (d < best) {
                    best = d;
                    p.s = w.at[i];
                }
            }
            p.walking = true;
            startMove(p, model.move("WALK"));
            placeOnWalk(p);
            continue;
        }
        CollisionWorld::RayHit hit;
        if (world.raycast(p.position + Vec3{0, 2, 0}, {0, -1, 0}, 5, hit))
            p.position[1] = hit.point[1];
    }
}

void Pedestrians::draw(const gfx::Mat4& viewProj, const Vec3& eye, const SceneLight& light) const {
    const ViewFrustum frustum(viewProj);
    for (const Ped& p : peds_) {
        if (length(p.position - eye) > kDrawDistance || !frustum.visible(p.position + Vec3{0, 1, 0}, 1.2f))
            continue;
        models_[size_t(p.model)]->draw(matrixOf(Mat3::rotationY(p.yaw), p.position), p.move, p.time, p.paintJob, light);
    }
}

} // namespace reluva::age
