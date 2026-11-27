// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/movement.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace reluva::storm3d {

namespace {

// Fitted to a construction ship's two flights in the original (docs:
// movement.md): its speed closes on what it wants at 4.5 a second; it
// brakes as if at 14 units a second, every second; it's there within 8
// of the point and past a point on its way within 35; its way round an
// obstacle turns 1.4 times their radii together from its middle.
constexpr double kResponse = 4.5;
constexpr double kBraking = 14;
constexpr float kArrived = 8;
constexpr float kPassed = 35;
constexpr float kRoundOut = 1.4f;

float wrapAngle(float a) {
    const auto pi = std::numbers::pi_v<float>;
    while (a > pi)
        a -= 2 * pi;
    while (a < -pi)
        a += 2 * pi;
    return a;
}

} // namespace

Movement::Physics Movement::physicsOf(const Odf &unit, const Odf::Loader &loader) {
    Physics p;
    const std::string file = unit.string("physicsFile");
    Odf physics;
    if (file.empty() || physics.load(file, loader))
        return p;
    p.topSpeed = float(physics.number("warpSpeed", p.topSpeed));
    p.cornerSpeed = float(physics.number("impulseSpeed", p.cornerSpeed));
    p.turnRate = float(physics.number("yawRotationRate", p.turnRate));
    p.wideTurn = float(physics.number("headingCloseEnoughForWarp", 30) * std::numbers::pi / 180);
    p.warpFrom = float(physics.number("tooCloseForWarp", p.warpFrom));
    p.turnFrom = float(physics.number("tooCloseToTurn", p.turnFrom));
    p.reverse = float(physics.number("maxReverseSpeed", p.reverse));
    return p;
}

gfx::Vec3 Movement::avoid(Ship &ship, const gfx::Vec3 &middle, float radius, const std::vector<Neighbour> &others) {
    constexpr float kLookAhead = 1.4f;
    constexpr float kMargin = 1.2f;
    constexpr float kRepulsionY = 0.5f;
    constexpr float kOldWeight = 0.7f;
    const gfx::Vec3 velocity{std::sin(ship.heading) * ship.speed, 0, std::cos(ship.heading) * ship.speed};
    gfx::Vec3 push{};
    for (const Neighbour &n : others) {
        // When they're nearest within the look ahead, and how near.
        gfx::Vec3 gap{};
        gfx::Vec3 closing{};
        float closingSquared = 0;
        float along = 0;
        for (size_t k = 0; k < 3; ++k) {
            gap[k] = middle[k] - n.middle[k];
            closing[k] = velocity[k] - n.velocity[k];
            closingSquared += closing[k] * closing[k];
            along += gap[k] * closing[k];
        }
        const float t = closingSquared > 1e-6f ? std::clamp(-along / closingSquared, 0.0f, kLookAhead) : 0.0f;
        gfx::Vec3 then{};
        float d = 0;
        for (size_t k = 0; k < 3; ++k) {
            then[k] = gap[k] + closing[k] * t;
            d += then[k] * then[k];
        }
        d = std::sqrt(d);
        const float clear = (radius + n.radius) * kMargin;
        if (d >= clear)
            continue;
        // Away from where it'll be, by how far inside it comes; straight
        // aside if they'd meet head on.
        const float by = (clear - d) / clear;
        if (d < 1e-3f) {
            then = {std::cos(ship.heading), 0, -std::sin(ship.heading)};
            d = 1;
        }
        push[0] += then[0] / d * by;
        push[1] += then[1] / d * by * kRepulsionY;
        push[2] += then[2] / d * by;
    }
    for (size_t k = 0; k < 3; ++k)
        ship.avoiding[k] = ship.avoiding[k] * kOldWeight + push[k] * (1 - kOldWeight);
    return ship.avoiding;
}

float Movement::climb(float height, float rise, float settle) {
    // The original drops the steering's upward part above the ceiling and
    // its downward part below the floor, rather than holding the ship.
    if ((height > kCeiling && rise > 0) || (height < kFloor && rise < 0))
        rise = 0;
    height += rise;
    return height + std::clamp(kPlane - height, -settle, settle);
}

std::vector<gfx::Vec3> Movement::routeAround(const gfx::Vec3 &from, const gfx::Vec3 &to,
                                             const std::vector<Obstacle> &obstacles, float radius) {
    // Past the first obstacle in the way, then on from there past the next,
    // each once.
    std::vector<gfx::Vec3> route;
    std::vector<bool> passed(obstacles.size(), false);
    gfx::Vec3 at = from;
    for (size_t turn = 0; turn < obstacles.size(); ++turn) {
        const float dx = to[0] - at[0];
        const float dz = to[2] - at[2];
        const float length = std::hypot(dx, dz);
        if (length < 1e-3f)
            break;
        const float ux = dx / length;
        const float uz = dz / length;
        size_t nearest = obstacles.size();
        float nearestAlong = 0;
        float side = 0;
        for (size_t i = 0; i < obstacles.size(); ++i) {
            const Obstacle &o = obstacles[i];
            const float ox = o.middle[0] - at[0];
            const float oz = o.middle[2] - at[2];
            const float along = ox * ux + oz * uz;
            const float across = ox * uz - oz * ux; // > 0: the obstacle is to the right of the way
            if (passed[i] || along <= 0 || along >= length || std::abs(across) >= o.radius + radius)
                continue;
            if (nearest == obstacles.size() || along < nearestAlong) {
                nearest = i;
                nearestAlong = along;
                side = across;
            }
        }
        if (nearest == obstacles.size())
            break;
        // Out from it square to the way, on the side away from it.
        const Obstacle &o = obstacles[nearest];
        const float out = (o.radius + radius) * kRoundOut;
        const float nx = side >= 0 ? -uz : uz;
        const float nz = side >= 0 ? ux : -ux;
        at = gfx::Vec3{o.middle[0] + nx * out, from[1], o.middle[2] + nz * out};
        route.push_back(at);
        passed[nearest] = true;
    }
    return route;
}

void Movement::step(Ship &ship, const Physics &physics, double seconds) {
    if (ship.waiting > 0) {
        ship.waiting = std::max(0.0f, ship.waiting - float(seconds));
        return;
    }
    double want = 0;
    while (ship.goal && !ship.route.empty() &&
           std::hypot(ship.route.front()[0] - ship.at[0], ship.route.front()[2] - ship.at[2]) < kPassed)
        ship.route.erase(ship.route.begin());
    if (ship.goal) {
        const gfx::Vec3 &next = ship.route.empty() ? *ship.goal : ship.route.front();
        const float dx = next[0] - ship.at[0];
        const float dz = next[2] - ship.at[2];
        // It brakes for the end of its way, not for the points on it.
        float left = std::hypot(dx, dz);
        if (!ship.route.empty())
            left = std::max(left, physics.topSpeed * physics.topSpeed / float(2 * kBraking));
        // Its goal just behind it, nearer than tooCloseToTurn, it backs to
        // it without turning, at maxReverseSpeed of its impulse *(inferred:
        // the keys' names)*.
        const float behind = std::abs(wrapAngle(std::atan2(dx, dz) - ship.heading));
        if (left < kArrived) {
            ship.goal.reset();
        } else if (ship.route.empty() && left < physics.turnFrom && behind > float(std::numbers::pi) * 0.75f) {
            want = -std::min(double(physics.cornerSpeed * physics.reverse), std::sqrt(2 * kBraking * left));
        } else {
            const float off = wrapAngle(std::atan2(dx, dz) - ship.heading);
            const float most = float(physics.turnRate * seconds);
            ship.heading = wrapAngle(ship.heading + std::clamp(off, -most, most));
            want = std::min(double(physics.topSpeed), std::sqrt(2 * kBraking * left));
            // Warp, its top speed, only past tooCloseForWarp of its goal and
            // headed within headingCloseEnoughForWarp of it.
            if (std::abs(off) > physics.wideTurn || (ship.route.empty() && left < physics.warpFrom))
                want = std::min(want, double(physics.cornerSpeed));
        }
    }
    ship.speed += float((want - ship.speed) * std::min(1.0, kResponse * seconds));
    ship.at[0] += float(std::sin(ship.heading) * ship.speed * seconds);
    ship.at[2] += float(std::cos(ship.heading) * ship.speed * seconds);
}

} // namespace reluva::storm3d
