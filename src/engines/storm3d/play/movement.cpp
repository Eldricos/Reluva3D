// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/movement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
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

Movement::Terrain Movement::Terrain::over(float x0, float z0, float x1, float z1) {
    // The planner's quadtree covers the map and 500 past each edge (0x408d38).
    constexpr float kMargin = 500;
    Terrain t;
    t.x0 = x0 - kMargin;
    t.z0 = z0 - kMargin;
    t.across = std::max(1, int(std::ceil((x1 - x0 + 2 * kMargin) / kCell)));
    t.down = std::max(1, int(std::ceil((z1 - z0 + 2 * kMargin) / kCell)));
    t.cost.assign(size_t(t.across) * size_t(t.down), kDefault);
    // Past the map's own edges, off the map.
    for (int z = 0; z < t.down; ++z)
        for (int x = 0; x < t.across; ++x) {
            const float cx = t.x0 + (float(x) + 0.5f) * kCell;
            const float cz = t.z0 + (float(z) + 0.5f) * kCell;
            if (cx < x0 || cx > x1 || cz < z0 || cz > z1)
                t.cost[size_t(z) * size_t(t.across) + size_t(x)] = kOffMap;
        }
    return t;
}

void Movement::Terrain::mark(const gfx::Vec3 &middle, float half, int price) {
    const int xa = std::max(0, int(std::floor((middle[0] - half - x0) / kCell)));
    const int xb = std::min(across - 1, int(std::floor((middle[0] + half - x0) / kCell)));
    const int za = std::max(0, int(std::floor((middle[2] - half - z0) / kCell)));
    const int zb = std::min(down - 1, int(std::floor((middle[2] + half - z0) / kCell)));
    for (int z = za; z <= zb; ++z)
        for (int x = xa; x <= xb; ++x) {
            int &c = cost[size_t(z) * size_t(across) + size_t(x)];
            if (c == kImpassable)
                continue;
            c = price == kImpassable ? kImpassable : std::max(c, price);
        }
}

int Movement::Terrain::at(float x, float z) const {
    const int cx = int(std::floor((x - x0) / kCell));
    const int cz = int(std::floor((z - z0) / kCell));
    if (cx < 0 || cz < 0 || cx >= across || cz >= down)
        return kOffMap;
    return cost[size_t(cz) * size_t(across) + size_t(cx)];
}

std::vector<gfx::Vec3> Movement::plan(const Terrain &terrain, const gfx::Vec3 &from, const gfx::Vec3 &to) {
    if (terrain.cost.empty())
        return {to};
    const float cell = Terrain::kCell;
    auto cellOf = [&](const gfx::Vec3 &p) {
        const int x = std::clamp(int(std::floor((p[0] - terrain.x0) / cell)), 0, terrain.across - 1);
        const int z = std::clamp(int(std::floor((p[2] - terrain.z0) / cell)), 0, terrain.down - 1);
        return z * terrain.across + x;
    };
    auto enterable = [&](int c) {
        const int price = terrain.cost[size_t(c)];
        return price != Terrain::kImpassable && price <= Terrain::kCannotEnter;
    };
    auto middleOf = [&](int c) {
        return gfx::Vec3{terrain.x0 + (float(c % terrain.across) + 0.5f) * cell, from[1],
                         terrain.z0 + (float(c / terrain.across) + 0.5f) * cell};
    };
    // The cost of the straight line between two points, by the cells it
    // crosses, a step a quarter cell; infinite through one not entered.
    auto lineCost = [&](const gfx::Vec3 &a, const gfx::Vec3 &b) {
        const float length = std::hypot(b[0] - a[0], b[2] - a[2]);
        const int steps = std::max(1, int(std::ceil(length / (cell / 4))));
        double sum = 0;
        for (int k = 0; k < steps; ++k) {
            const float t = (float(k) + 0.5f) / float(steps);
            const gfx::Vec3 p{a[0] + (b[0] - a[0]) * t, a[1], a[2] + (b[2] - a[2]) * t};
            const int c = cellOf(p);
            if (k > 0 && !enterable(c) && c != cellOf(a))
                return std::numeric_limits<double>::infinity();
            sum += double(length) / steps * terrain.cost[size_t(c)] / Terrain::kDefault;
        }
        return sum;
    };
    const int start = cellOf(from);
    const int goal = cellOf(to);
    // Clear all the way: straight there.
    if (std::isfinite(lineCost(from, to)) && lineCost(from, to) <= std::hypot(to[0] - from[0], to[2] - from[2]) + 1e-3)
        return {to};
    // A* over the cells, eight ways, the straight distance to the goal its guess.
    const size_t n = terrain.cost.size();
    std::vector<double> best(n, std::numeric_limits<double>::infinity());
    std::vector<int> cameFrom(n, -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    auto guess = [&](int c) {
        const gfx::Vec3 m = middleOf(c);
        return double(std::hypot(m[0] - to[0], m[2] - to[2]));
    };
    best[size_t(start)] = 0;
    open.push({guess(start), start});
    int nearest = start;
    double nearestLeft = guess(start);
    while (!open.empty()) {
        const auto [f, c] = open.top();
        open.pop();
        if (f - guess(c) > best[size_t(c)] + 1e-6)
            continue;
        if (c == goal) {
            nearest = goal;
            break;
        }
        if (guess(c) < nearestLeft)
            nearest = c, nearestLeft = guess(c);
        const int cx = c % terrain.across;
        const int cz = c / terrain.across;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz)
                    continue;
                const int nx = cx + dx;
                const int nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= terrain.across || nz >= terrain.down)
                    continue;
                const int next = nz * terrain.across + nx;
                if (!enterable(next))
                    continue;
                const double step =
                    (dx && dz ? std::numbers::sqrt2 : 1.0) * cell * terrain.cost[size_t(next)] / Terrain::kDefault;
                if (best[size_t(c)] + step < best[size_t(next)]) {
                    best[size_t(next)] = best[size_t(c)] + step;
                    cameFrom[size_t(next)] = c;
                    open.push({best[size_t(next)] + guess(next), next});
                }
            }
    }
    std::vector<gfx::Vec3> points;
    for (int c = nearest; c != -1 && c != start; c = cameFrom[size_t(c)])
        points.push_back(middleOf(c));
    std::reverse(points.begin(), points.end());
    if (nearest == goal) {
        if (!points.empty())
            points.back() = to;
        else
            points.push_back(to);
    }
    // Rubber banding: from each point, on to the furthest the straight line
    // reaches for no more than the way there costs.
    std::vector<gfx::Vec3> way;
    gfx::Vec3 at = from;
    size_t i = 0;
    while (i < points.size()) {
        size_t far = i;
        double along = 0;
        gfx::Vec3 prev = at;
        for (size_t j = i; j < points.size(); ++j) {
            along += lineCost(prev, points[j]);
            prev = points[j];
            const double straight = lineCost(at, points[j]);
            if (!std::isfinite(straight))
                break;
            if (straight <= along + 1e-3)
                far = j;
        }
        way.push_back(points[far]);
        at = points[far];
        i = far + 1;
    }
    return way;
}

} // namespace reluva::storm3d
