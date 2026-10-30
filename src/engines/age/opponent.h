// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/car.h"
#include "engines/age/road_graph.h"

#include <vector>

namespace reluva::age {

// Drives a car through a path's points (an opponent's .opp path, routed
// along the roads): steers (pure pursuit) for a point a little way along
// the path, slows for each corner ahead in time, backs out when stuck.
// Reluva's stand-in for the game's opponents, whose driving is in its
// encrypted program. See docs/engines/age-racing.md.
class PathDriver {
public:
    // `laps` > 0 drives the path round that many times, from its end back
    // to point `loopFrom` each time; 0 drives it once. `skill` scales the
    // top speed.
    void start(std::vector<Vec3> path, int laps, float skill, size_t loopFrom = 0);
    // A new path driven once, kept stuck or backing out as it was (a
    // computer player changing its mind as it goes).
    void follow(std::vector<Vec3> path, float skill);
    CarControls drive(const CarSim& car, float dt);
    // At most this fast (metres a second), as a patrolling police car.
    void setSpeedLimit(float limit) { limit_ = limit; }

    bool finished() const { return finished_; }
    int lap() const { return lap_; }
    size_t next() const { return index_; }
    // How many of the path's points it's gone on from, laps unrolled.
    size_t passed() const { return passed_; }
    int laps() const { return laps_; }
    size_t loopFrom() const { return loopFrom_; }
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
    size_t index_ = 0, loopFrom_ = 0, passed_ = 0;
    float skill_ = 1;
    float limit_ = 1e9f;
    bool finished_ = false;
    float stuckTime_ = 0, reverseTime_ = 0;
    // Circling without getting nearer the target: the nearest it's been,
    // and for how long it hasn't got 2 m nearer.
    float best_ = 1e9f, lostTime_ = 0;
    int resetsHere_ = 0; // at the current point: after two, the next reset skips it
};

// Where along a driver's path (as passed() counts its points) it's
// nearest each of `gates` in turn: the race's checkpoints, which an
// opponent's own path doesn't always go through.
std::vector<size_t> pathPointsNearest(const PathDriver& driver, const std::vector<Vec3>& gates);

// An opponent's points joined along the roads: from `start` to the first,
// on through the rest and (for a circuit, `loop`) from the last back to
// the first. `loopFrom` is set to where the loop begins in the result.
// `fallback`: another graph (the road map's) for the legs `roads` has no
// way for.
std::vector<Vec3> routePath(const RoadGraph& roads, const Vec3& start, const std::vector<Vec3>& points, bool loop,
                            size_t& loopFrom, const RoadGraph* fallback = nullptr);

} // namespace reluva::age
