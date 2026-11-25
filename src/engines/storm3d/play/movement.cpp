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
    return p;
}

std::vector<gfx::Vec3> Movement::routeAround(const gfx::Vec3 &from, const gfx::Vec3 &to,
                                             const std::vector<Obstacle> &obstacles, float radius) {
    const float dx = to[0] - from[0];
    const float dz = to[2] - from[2];
    const float length = std::hypot(dx, dz);
    if (length < 1e-3f)
        return {};
    const float ux = dx / length;
    const float uz = dz / length;
    const Obstacle *nearest = nullptr;
    float nearestAlong = 0;
    float side = 0;
    for (const Obstacle &o : obstacles) {
        const float ox = o.middle[0] - from[0];
        const float oz = o.middle[2] - from[2];
        const float along = ox * ux + oz * uz;
        const float across = ox * uz - oz * ux; // > 0: the obstacle is to the right of the way
        if (along <= 0 || along >= length || std::abs(across) >= o.radius + radius)
            continue;
        if (!nearest || along < nearestAlong) {
            nearest = &o;
            nearestAlong = along;
            side = across;
        }
    }
    if (!nearest)
        return {};
    // Out from it square to the way, on the side away from it.
    const float out = (nearest->radius + radius) * kRoundOut;
    const float nx = side >= 0 ? -uz : uz;
    const float nz = side >= 0 ? ux : -ux;
    return {gfx::Vec3{nearest->middle[0] + nx * out, from[1], nearest->middle[2] + nz * out}};
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
        if (left < kArrived) {
            ship.goal.reset();
        } else {
            const float off = wrapAngle(std::atan2(dx, dz) - ship.heading);
            const float most = float(physics.turnRate * seconds);
            ship.heading = wrapAngle(ship.heading + std::clamp(off, -most, most));
            want = std::min(double(physics.topSpeed), std::sqrt(2 * kBraking * left));
            if (std::abs(off) > physics.wideTurn)
                want = std::min(want, double(physics.cornerSpeed));
        }
    }
    ship.speed += float((want - ship.speed) * std::min(1.0, kResponse * seconds));
    ship.at[0] += float(std::sin(ship.heading) * ship.speed * seconds);
    ship.at[2] += float(std::cos(ship.heading) * ship.speed * seconds);
}

} // namespace reluva::storm3d
