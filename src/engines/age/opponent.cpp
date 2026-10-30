// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/opponent.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kReach = 14;          // metres from a point that counts as reaching it
constexpr float kBraking = 7;         // metres a second a second the driver plans to brake at
constexpr float kCornerGrip = 7;      // metres a second a second it takes a corner at (speed squared over radius)
constexpr float kSlowest = 6;         // m/s it takes even the tightest corner at
constexpr float kBrakeEarly = 6;      // metres before a corner it means to be down to its speed
constexpr float kLookBase = 8, kLookPerSpeed = 0.6f, kLookMost = 35; // metres ahead it steers for
constexpr float kLookFar = 160;                                      // metres of the way ahead it plans for
constexpr float kSpurClose = 3; // metres: back where it was two points ago, a spur's been driven
constexpr float kLookLeast = 4;
constexpr float kLookLeastTime = 0.4f; // seconds ahead it steers for, at the least
constexpr float kTurningBrake = 0.4f;  // the brake it uses while steering hard
constexpr float kSteerGrip =
    10; // m/s^2 the steering asks of the tyres at most       // metres ahead it steers for, at the least
constexpr float kCornerSpan = 8; // metres either side of a point over which its corner's turn is measured
constexpr float kMostCut = 4;    // metres it cuts a corner by (its line from the path's corners)
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
    passed_ = 0;
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
    ++passed_;
    if (++index_ >= path_.size()) {
        if (laps_ > 0 && ++lap_ < laps_)
            index_ = loopFrom_;
        else
            finished_ = true;
    }
}

void PathDriver::follow(std::vector<Vec3> path, float skill) {
    const float stuck = stuckTime_, reverse = reverseTime_;
    const int resets = resetsHere_;
    start(std::move(path), 0, skill);
    stuckTime_ = stuck;
    reverseTime_ = reverse;
    resetsHere_ = resets;
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
        ++passed_;
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

    // The way ahead: where the car is on the path (its nearest point on
    // the way to the target), then the points after, laps unrolled.
    std::vector<Vec3> ahead;
    {
        const Vec3 from = index_ > 0 ? point(index_ - 1) : pos;
        const Vec3 ab = flat(target - from);
        const float t = dot(ab, ab) > 1e-4f ? std::clamp(dot(flat(pos - from), ab) / dot(ab, ab), 0.0f, 1.0f) : 1;
        ahead.push_back(from + (target - from) * t);
        float along = 0;
        size_t i = index_;
        int lapsLeft = laps_ > 0 ? laps_ - lap_ - 1 : 0;
        for (int steps = 0; steps < 64 && along < kLookFar; ++steps) {
            along += length(flat(point(i) - ahead.back()));
            ahead.push_back(point(i));
            if (++i >= n) {
                if (lapsLeft-- <= 0)
                    break;
                i = loopFrom_;
            }
        }
    }

    // Steer (pure pursuit) for the point a little way along it, further
    // the faster the car goes: the wheels' angle for the arc through it.
    const float speed = car.forwardSpeed();
    const float look = std::clamp(kLookBase + kLookPerSpeed * std::fabs(speed), kLookBase, kLookMost);
    // The point `along` metres on from the car's place on the path, and
    // how far the straight line to it passes from the corners on the way
    // (a long look cuts a junction's corner through its buildings).
    auto pointAlong = [&](float along, float& cut) {
        cut = 0;
        float left = along;
        Vec3 at = ahead.back();
        size_t k = 0;
        for (; k + 1 < ahead.size(); ++k) {
            const float seg = length(flat(ahead[k + 1] - ahead[k]));
            if (seg >= left) {
                at = ahead[k] + (ahead[k + 1] - ahead[k]) * (seg > 1e-4f ? left / seg : 0);
                break;
            }
            left -= seg;
        }
        const Vec3 chord = flat(at - pos);
        const float chordLength = length(chord);
        for (size_t j = 1; j <= k && j < ahead.size() && chordLength > 0.5f; ++j) {
            const Vec3 v = flat(ahead[j] - pos);
            const float u = std::clamp(dot(v, chord) / (chordLength * chordLength), 0.0f, 1.0f);
            cut = std::max(cut, length(v - chord * u));
        }
        return at;
    };
    float cut = 0;
    Vec3 aim = pointAlong(look, cut);
    if (cut > kMostCut) { // shorter, until the line stays on the road (but not so short it swerves)
        float lo = std::max(kLookLeast, kLookLeastTime * std::fabs(speed)), hi = std::max(lo, look);
        aim = pointAlong(lo, cut);
        for (int i = 0; i < 8; ++i) {
            const float mid = (lo + hi) / 2;
            const Vec3 at = pointAlong(mid, cut);
            if (cut <= kMostCut) {
                lo = mid;
                aim = at;
            } else {
                hi = mid;
            }
        }
    }
    const Vec3 toAim = flat(aim - pos);
    const float x = dot(toAim, right), z = dot(toAim, forward);
    const float angle = std::atan2(x, z);
    const CarSetup& setup = car.setup();
    const float wheelbase = std::max(1.0f, std::fabs(setup.wheels[0][2] - setup.wheels[2][2]));
    // The lock the car has at this speed (CarSim takes some off as it goes
    // faster): asked for in full, the wheels turn as far as the arc needs.
    const float lock = std::max(0.05f, setup.front.steeringLimit) / (1 + std::fabs(speed) / 25);
    if (z <= 0) {
        c.steer = x < 0 ? -1.0f : 1.0f; // behind: round as hard as it goes
    } else {
        // No tighter than the tyres hold at this speed: the speed's to come
        // down for a corner, not the car to spin.
        const float most = kSteerGrip / std::max(1.0f, speed * speed);
        const float curvature = std::clamp(2 * x / std::max(1.0f, x * x + z * z), -most, most);
        c.steer = std::clamp(std::atan(wheelbase * curvature) / lock, -1.0f, 1.0f);
    }

    // Speed: the car's top speed, less for each corner ahead (as fast as
    // an arc that fits the corner's legs can be driven), braking in time
    // for the slowest; and slowly while pointing well off the way.
    const float top = std::min(setup.highSpeed * skill_, limit_);
    float wanted = top;
    {
        // Each point's distance along the way, and each leg's heading.
        std::vector<float> at(ahead.size(), 0), heading(ahead.size(), 0);
        at[0] = length(flat(ahead[0] - pos));
        for (size_t k = 1; k < ahead.size(); ++k) {
            const Vec3 d = flat(ahead[k] - ahead[k - 1]);
            at[k] = at[k - 1] + length(d);
            heading[k] = std::atan2(d[0], -d[2]);
        }
        // The way's heading at a distance along it (its leg's).
        auto headingAt = [&](float s) {
            size_t k = 1;
            while (k + 1 < ahead.size() && at[k] < s)
                ++k;
            return heading[std::min(k, ahead.size() - 1)];
        };
        // A corner's radius: how far its heading turns over the stretch
        // round it (a junction turns over a few short legs, not at a point).
        for (size_t k = 1; k + 1 < ahead.size(); ++k) {
            if (length(flat(ahead[k + 1] - ahead[k])) < 0.5f)
                continue;
            float turn = std::fabs(headingAt(at[k] + kCornerSpan) - headingAt(std::max(at[0], at[k] - kCornerSpan)));
            turn = std::min(turn, 2 * kPi - turn);
            if (turn < 0.05f)
                continue;
            const float radius = 2 * kCornerSpan / turn;
            const float cornerSpeed = std::max(kSlowest, std::sqrt(kCornerGrip * radius));
            const float reachable =
                std::sqrt(cornerSpeed * cornerSpeed + 2 * kBraking * std::max(0.0f, at[k] - kCornerSpan - kBrakeEarly));
            wanted = std::min(wanted, reachable);
        }
    }
    if (std::fabs(angle) > 1.0f)
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
        c.brake = std::fabs(c.steer) > 0.3f ? kTurningBrake : 1.0f; // gently in a turn, or it spins
    return c;
}

std::vector<size_t> pathPointsNearest(const PathDriver& driver, const std::vector<Vec3>& gates) {
    // The path as it's driven: once through, then from the loop's start
    // for each lap after the first.
    const std::vector<Vec3>& path = driver.path();
    std::vector<size_t> order;
    for (size_t i = 0; i < path.size(); ++i)
        order.push_back(i);
    for (int lap = 1; lap < driver.laps(); ++lap)
        for (size_t i = driver.loopFrom(); i < path.size(); ++i)
            order.push_back(i);
    // Each gate in turn: the nearest of the points from the last gate's on,
    // looking no further than a lap ahead.
    const size_t lapLength = std::max<size_t>(1, path.size() - std::min(driver.loopFrom(), path.size()));
    std::vector<size_t> out;
    size_t from = 0;
    for (const Vec3& gate : gates) {
        size_t best = from;
        float nearest = 1e9f;
        for (size_t k = from; k < order.size() && k < from + lapLength; ++k) {
            const float d = length(flat(path[order[k]] - gate));
            if (d < nearest) {
                nearest = d;
                best = k;
            }
        }
        out.push_back(best);
        from = std::min(best + 1, order.empty() ? 0 : order.size() - 1);
    }
    return out;
}

std::vector<Vec3> routePath(const RoadGraph& roads, const Vec3& start, const std::vector<Vec3>& points, bool loop,
                            size_t& loopFrom, const RoadGraph* fallback) {
    std::vector<Vec3> out;
    if (points.empty())
        return out;
    auto append = [&](const Vec3& a, const Vec3& b) {
        // No way found (just the end, from far off): the other graph's, if
        // it has one; else straight there.
        std::vector<Vec3> way = roads.route(a, b);
        if (way.size() <= 1 && fallback && length(flat(b - a)) > kReach) {
            std::vector<Vec3> other = fallback->route(a, b);
            if (other.size() > 1)
                way = std::move(other);
        }
        for (const Vec3& p : way)
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
    // There and back the same way (a path point a little off the main road,
    // reached up a side street): cut off, as no car turns round in a street.
    for (bool cut = true; cut;) {
        cut = false;
        for (size_t i = 1; i + 1 < out.size(); ++i) {
            if (i == loopFrom || i + 1 == loopFrom || length(flat(out[i] - out[i - 1])) < 1 ||
                length(flat(out[i + 1] - out[i - 1])) > kSpurClose)
                continue;
            out.erase(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(i + 2));
            if (loopFrom > i + 1)
                loopFrom -= 2;
            cut = true;
            break;
        }
    }
    return out;
}

} // namespace reluva::age
