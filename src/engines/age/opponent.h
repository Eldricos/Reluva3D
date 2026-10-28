// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/car.h"
#include "engines/age/road_graph.h"

#include <vector>

namespace reluva::age {

// Drives a car through a path's points (an opponent's .opp path): steers
// for the next point, slows for the corner there, backs out when stuck.
// Reluva's stand-in for the game's opponents, whose driving is in its
// encrypted program; the game also has road maps for them (.aimap) that
// aren't read yet. See docs/engines/age-racing.md.
class PathDriver {
public:
    // `laps` > 0 drives the path round that many times, from its end back
    // to point `loopFrom` each time; 0 drives it once. `skill` scales the
    // top speed.
    void start(std::vector<Vec3> path, int laps, float skill, size_t loopFrom = 0);
    CarControls drive(const CarSim& car, float dt);
    // At most this fast (metres a second), as a patrolling police car.
    void setSpeedLimit(float limit) { limit_ = limit; }

    bool finished() const { return finished_; }
    int lap() const { return lap_; }
    size_t next() const { return index_; }
    const std::vector<Vec3>& path() const { return path_; }
    // Stuck for long, or 8 s without getting nearer the next point: the
    // caller puts the car back at `resetPoint` facing `resetYaw`, and
    // calls resetDone(). After two at the same point the third puts it on
    // that point, and it goes on to the next.
    bool wantsReset() const { return stuckTime_ > 6 || lostTime_ > 8; }
    Vec3 resetPoint() const;
    float resetYaw() const;
    void resetDone();

private:
    std::vector<Vec3> path_;
    int laps_ = 0, lap_ = 0;
    size_t index_ = 0, loopFrom_ = 0;
    float skill_ = 1;
    float limit_ = 1e9f;
    bool finished_ = false;
    float stuckTime_ = 0, reverseTime_ = 0;
    // Circling without getting nearer the target: the nearest it's been,
    // and for how long it hasn't got 2 m nearer.
    float best_ = 1e9f, lostTime_ = 0;
    int resetsHere_ = 0; // at the current point: after two, the next reset skips it
};

// An opponent's points joined along the roads: from `start` to the first,
// on through the rest and (for a circuit, `loop`) from the last back to
// the first. `loopFrom` is set to where the loop begins in the result.
std::vector<Vec3> routePath(const RoadGraph& roads, const Vec3& start, const std::vector<Vec3>& points, bool loop,
                            size_t& loopFrom);

} // namespace reluva::age
