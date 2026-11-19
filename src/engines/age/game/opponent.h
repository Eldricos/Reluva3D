// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/game/car.h"
#include "engines/age/game/road_graph.h"

#include <vector>

namespace reluva::age {

/**
 * Drives a car through a path's points (an opponent's .opp path, routed
 * along the roads): steers (pure pursuit) for a point a little way along
 * the path, slows for each corner ahead in time, backs out when stuck.
 * Reluva's stand-in for the game's opponents, whose driving is in its
 * encrypted program. See docs/engines/age/games/mm2/systems/races.md.
 */
class PathDriver {
public:
    /**
     * `laps` > 0 drives the path round that many times, from its end back
     * to point `loopFrom` each time; 0 drives it once. `skill` scales the
     * top speed.
     */
    void start(std::vector<Vec3> path, int laps, float skill, size_t loopFrom = 0);
    /**
     * A new path driven once, kept stuck or backing out as it was (a
     * computer player changing its mind as it goes).
     */
    void follow(std::vector<Vec3> path, float skill);
    /** The controls for the car this step; once finished (or with no path), it brakes to a stop. */
    CarControls drive(const CarSim &car, float dt);
    /** At most this fast (metres a second), as a patrolling police car. */
    void setSpeedLimit(float limit) { _limit = limit; }

    /** Whether it has driven the whole path (every lap); true for an empty one. */
    bool finished() const { return _finished; }
    /** The lap it's on, from 0. */
    int lap() const { return _lap; }
    /** The index of the point it's heading for. */
    size_t next() const { return _index; }
    /** How many of the path's points it's gone on from, laps unrolled. */
    size_t passed() const { return _passed; }
    /** As start() was given: 0 for once through. */
    int laps() const { return _laps; }
    /** The point each lap after the first starts again from. */
    size_t loopFrom() const { return _loopFrom; }
    /** The path as start() or follow() gave it. */
    const std::vector<Vec3> &path() const { return _path; }
    /**
     * Stuck for long, or 8 s without getting nearer the next point: the
     * caller puts the car back at `resetPoint` facing `resetYaw`, and
     * calls resetDone(). After two at the same point the third puts it on
     * that point, and it goes on to the next.
     */
    bool wantsReset() const { return _stuckTime > 6 || _lostTime > 8; }
    /** Where to put the car back: the point it last passed, or the next one on the third reset there. */
    Vec3 resetPoint() const;
    /** The way to face there, in radians (0 facing -z): towards the point after it. */
    float resetYaw() const;
    /** Tells it the car has been put back. */
    void resetDone();

private:
    std::vector<Vec3> _path;
    int _laps = 0;
    int _lap = 0;
    size_t _index = 0;
    size_t _loopFrom = 0;
    size_t _passed = 0;
    float _skill = 1;
    float _limit = 1e9f;
    bool _finished = false;
    float _stuckTime = 0;
    float _reverseTime = 0;
    // Circling without getting nearer the target: the nearest it's been,
    // and for how long it hasn't got 2 m nearer.
    float _best = 1e9f;
    float _lostTime = 0;
    int _resetsHere = 0; // at the current point: after two, the next reset skips it
};

/**
 * Where along a driver's path (as passed() counts its points) it's
 * nearest each of `gates` in turn: the race's checkpoints, which an
 * opponent's own path doesn't always go through.
 */
std::vector<size_t> pathPointsNearest(const PathDriver &driver, const std::vector<Vec3> &gates);

/**
 * An opponent's points joined along the roads: from `start` to the first,
 * on through the rest and (for a circuit, `loop`) from the last back to
 * the first. `loopFrom` is set to where the loop begins in the result.
 * `fallback`: another graph (the road map's) for the legs `roads` has no
 * way for.
 */
std::vector<Vec3> routePath(const RoadGraph &roads, const Vec3 &start, const std::vector<Vec3> &points, bool loop,
                            size_t &loopFrom, const RoadGraph *fallback = nullptr);

} // namespace reluva::age
