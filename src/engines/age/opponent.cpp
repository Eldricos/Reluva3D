// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/opponent.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kReach = 14;          // metres from a point that counts as reaching it
constexpr float kBraking = 7;         // metres a second a second the driver plans to brake at
constexpr float kPi = 3.14159265358979f;

Vec3 flat(Vec3 v) {
    v[1] = 0;
    return v;
}

} // namespace

void PathDriver::start(std::vector<Vec3> path, int laps, float skill, size_t loopFrom) {
    path_ = std::move(path);
    loopFrom_ = std::min(loopFrom, path_.empty() ? 0 : path_.size() - 1);
    laps_ = laps;
    lap_ = 0;
    index_ = 0;
    skill_ = skill;
    finished_ = path_.empty();
    stuckTime_ = reverseTime_ = lostTime_ = 0;
    best_ = 1e9f;
    resetsHere_ = 0;
}

Vec3 PathDriver::resetPoint() const {
    if (path_.empty())
        return {};
    if (resetsHere_ >= 2)
        return path_[index_];
    return path_[index_ > 0 ? index_ - 1 : 0];
}

float PathDriver::resetYaw() const {
    if (path_.empty())
        return 0;
    // Facing the point after the one it's put at.
    const size_t to = resetsHere_ >= 2 ? (index_ + 1 < path_.size() ? index_ + 1 : loopFrom_) : index_;
    const Vec3 d = flat(path_[to] - resetPoint());
    return length(d) > 0.1f ? std::atan2(-d[0], -d[2]) : 0;
}

void PathDriver::resetDone() {
    stuckTime_ = reverseTime_ = lostTime_ = 0;
    best_ = 1e9f;
    if (++resetsHere_ < 3)
        return;
    // Put on the point it couldn't reach: on to the next.
    resetsHere_ = 0;
    if (++index_ >= path_.size()) {
        if (laps_ > 0 && ++lap_ < laps_)
            index_ = loopFrom_;
        else
            finished_ = true;
    }
}

CarControls PathDriver::drive(const CarSim& car, float dt) {
    CarControls c;
    if (finished_ || path_.empty()) {
        c.brake = std::fabs(car.forwardSpeed()) > 1 ? 1.0f : 0.0f;
        return c;
    }
    const Vec3 pos = car.position;
    const Vec3 forward = normalized(flat(-car.orientation.z));
    const Vec3 right = normalized(flat(car.orientation.x));
    const size_t n = path_.size();
    auto point = [&](size_t i) { return path_[std::min(i, n - 1)]; };

    // Reached (or passed) the next point: on to the one after.
    const Vec3 previous = index_ > 0 ? point(index_ - 1) : pos;
    Vec3 target = point(index_);
    const Vec3 segment = normalized(flat(target - previous));
    const float distance = length(flat(target - pos));
    if (distance < kReach || (distance < 40 && dot(flat(pos - target), segment) > 0)) {
        ++index_;
        if (index_ >= n) {
            if (laps_ > 0 && ++lap_ < laps_) {
                index_ = loopFrom_;
            } else {
                finished_ = true;
                return c;
            }
        }
        target = point(index_);
        best_ = 1e9f;
        lostTime_ = 0;
        resetsHere_ = 0;
    }
    const float nearNow = length(flat(target - pos));
    if (nearNow < best_ - 2) {
        best_ = nearNow;
        lostTime_ = 0;
    } else {
        lostTime_ += dt;
    }

    // The corner ahead: the sharpest the path turns from the car's way
    // within 60 m past the target, and how far off that is.
    const Vec3 to = flat(target - pos);
    const Vec3 in = length(to) > 1 ? normalized(to) : forward;
    float corner = 0, toCorner = length(to);
    {
        float along = length(to);
        size_t i = index_;
        int lapsLeft = laps_ > 0 ? laps_ - lap_ - 1 : 0;
        for (int steps = 0; steps < 64 && along < length(to) + 60; ++steps) {
            size_t j = i + 1;
            if (j >= n) {
                if (lapsLeft-- <= 0)
                    break;
                j = loopFrom_;
            }
            const Vec3 seg = flat(point(j) - point(i));
            if (length(seg) > 0.5f) {
                const float turn = std::acos(std::clamp(dot(in, normalized(seg)), -1.0f, 1.0f));
                if (turn > corner) {
                    corner = turn;
                    toCorner = along;
                }
            }
            along += length(seg);
            i = j;
        }
    }

    // Steer for the target.
    const float angle = std::atan2(dot(to, right), dot(to, forward));
    c.steer = std::clamp(angle * 2, -1.0f, 1.0f);

    // Speed: the car's top speed, less for the corner ahead in time to
    // brake for it, and less while pointing well off the target.
    const float top = std::min(car.setup().highSpeed * skill_, limit_);
    const float cornerSpeed = std::clamp(12 + (1 - corner / (kPi / 2)) * 30, 8.0f, top);
    const float speed = car.forwardSpeed();
    float wanted = top;
    if (toCorner < (speed * speed - cornerSpeed * cornerSpeed) / (2 * kBraking) + 12)
        wanted = cornerSpeed;
    if (std::fabs(angle) > 0.6f)
        wanted = std::min(wanted, 9.0f);

    // Stuck against something: back out, steering the other way.
    if (reverseTime_ > 0) {
        reverseTime_ -= dt;
        c.brake = 1;
        c.steer = -c.steer;
        stuckTime_ += dt;
        return c;
    }
    if (std::fabs(speed) < 1.5f) {
        stuckTime_ += dt;
        if (stuckTime_ > 2.5f && std::fmod(stuckTime_, 4.0f) < dt * 2)
            reverseTime_ = 1.2f;
    } else {
        stuckTime_ = 0;
    }

    if (speed < wanted - 1)
        c.throttle = 1;
    else if (speed > wanted + 2 && speed > 2)
        c.brake = 1;
    return c;
}

std::vector<Vec3> routePath(const RoadGraph& roads, const Vec3& start, const std::vector<Vec3>& points, bool loop,
                            size_t& loopFrom) {
    std::vector<Vec3> out;
    if (points.empty())
        return out;
    auto append = [&](const Vec3& a, const Vec3& b) {
        for (const Vec3& p : roads.route(a, b))
            out.push_back(p);
    };
    append(start, points[0]);
    loopFrom = 0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        if (loop && i == 0)
            loopFrom = out.size() - 1; // the lap starts at the first point
        append(points[i], points[i + 1]);
    }
    if (loop)
        append(points.back(), points[0]);
    return out;
}

} // namespace reluva::age
