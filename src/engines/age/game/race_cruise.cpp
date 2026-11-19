// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race menu's Cruise: the city to drive about with no race, its police
// patrolling where race/<city>/roam.aimap puts them. And, under it, San
// Francisco's "Stunt Mode: Acceleration" (mmlang.dll 199-204: "Race to the
// end of the pier! But don't fall in!", "Put the pedal to the metal!",
// "Great job! You can stop on a dime!") along race/sf/accel0waypoints.csv.
// See docs/engines/age-racing.md.

#include "engines/age/ui/screen.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr float kStopped = 0.5;  // m/s: stopped, at the end of the pier
constexpr float kFallenIn = 3;   // metres below the lowest point: in the water
constexpr float kBustedHold = 3; // seconds to see it before the car's put back
constexpr float kWreckedHold = 4;

float flatDistance(const Vec3 &a, const Vec3 &b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

} // namespace

void RaceScreen::startCruise() {
    // The cruise starts at one of the city's Cops & Robbers places (as a
    // multiplayer cruise does), picked by the car, so a replay starts in
    // the same place; the stunt at its first point.
    _cruiseHold = 0;
    _stuntMessage = 0;
    _stuntMessageFor = 0;
    if (stunt()) {
        // Faced along the road: the file's first heading is some 25
        // degrees off it, towards the buildings.
        if (_race.waypoints.size() > 1) {
            const Vec3 a = _race.waypoints[0].position, b = _race.waypoints[1].position;
            _race.waypoints[0].heading = std::atan2(b[0] - a[0], -(b[2] - a[2])) * 180 / kPi;
        }
        // The end of the pier: on along the last leg until there's no
        // ground under it (some 140 m past the last point).
        _pierEnd = {};
        if (_race.waypoints.size() > 1) {
            const Vec3 a = _race.waypoints[_race.waypoints.size() - 2].position, b = _race.waypoints.back().position;
            const float len = std::hypot(b[0] - a[0], b[2] - a[2]);
            for (float d = 0; d < 500 && len > 0; d += 1) {
                const Vec3 p{b[0] + (b[0] - a[0]) / len * d, b[1] + 20, b[2] + (b[2] - a[2]) / len * d};
                CollisionWorld::RayHit hit;
                if (!_world.raycast(p, {0, -1, 0}, 30, hit))
                    break;
                _pierEnd = {p[0], b[1], p[2]};
            }
        }
        return;
    }
    if (_race.index == kPrototypeRace && !_race.waypoints.empty()) {
        _race.waypoints.resize(1); // its one point: the start
        return;
    }
    std::vector<Waypoint> places;
    loadGoldPoints(_ctx.files, _cities[size_t(_city)].map, places);
    Waypoint start{_scene->start, -_scene->startYaw * 180 / kPi, 0};
    if (!places.empty())
        start = places[size_t(_car * 7 + _level) % places.size()];
    _race.waypoints = {start};
}

void RaceScreen::stepCruise(float dt) {
    Racer &player = _racers.front();
    CarSim &sim = player.car->sim;
    if (cruising()) {
        // No end. Busted or wrecked: a moment to see it, then back where
        // the cruise started, mended.
        if (_cruiseHold > 0) {
            _cruiseHold -= dt;
            if (_cruiseHold <= 0) {
                const Waypoint &w = _race.waypoints[0];
                sim.reset(player.car->setup, w.position + Vec3{0, 0.5f, 0}, headingToYaw(w.heading));
                _wear.mend(*player.car);
                if (_smoke)
                    _smoke->clear();
                _life.police().forgive();
                _outcome.clear();
                _cameraReset = true;
                _fill = true;
            }
        } else if (_life.police().busted()) {
            _outcome = "Busted!";
            _cruiseHold = kBustedHold;
        } else if (_wear.wrecked()) {
            _outcome = "Wrecked!";
            _cruiseHold = kWreckedHold;
        }
        return;
    }

    // The stunt: flat out down to the pier through the checkpoints, then
    // stopped short of its end without going over into the water.
    if (_state == State::kFinished)
        return;
    if (_clock >= 0 && _clock - dt < 0) {
        _stuntMessage = 203; // "Put the pedal to the metal!"
        _stuntMessageFor = LessonJudge::kMessageTime;
    }
    _stuntMessageFor = std::max(0.0f, _stuntMessageFor - dt);
    float lowest = _race.waypoints.front().position[1];
    for (const Waypoint &w : _race.waypoints)
        lowest = std::min(lowest, w.position[1]);
    if (sim.position[1] < lowest - kFallenIn) {
        _outcome = "You fell in!";
        _state = State::kFinished;
    } else if (_life.police().busted() || _wear.wrecked()) {
        _outcome = _life.police().busted() ? "Busted!" : "Wrecked!";
        _state = State::kFinished;
    } else if (player.next >= _targets.size() && std::fabs(sim.forwardSpeed()) < kStopped) {
        player.finishTime = _clock;
        char shortOf[64];
        std::snprintf(shortOf, sizeof shortOf, "  %.0f m from the end", flatDistance(sim.position, _pierEnd));
        _outcome = _strings.get(204, "Great job! You can stop on a dime!") + "  " + clockText(_clock) + shortOf;
        _state = State::kFinished;
    }
}

void RaceScreen::drawCruiseHud() {
    // As the game's cruise: no words but the police after you and why the
    // car's about to be put back.
    const Racer &player = _racers.front();
    if (!_outcome.empty())
        drawCentred(_outcome, kFrameH / 2 - 40, {255, 120, 90}, 4);
    if (_life.police().chasing() > 0 && _cruiseHold <= 0)
        font::drawText(_overlay, 4, kFrameH - 150, "POLICE CHASING", {255, 90, 90}, 2); // above the speedometer
    if (_hudShown)
        drawSpeedometer(_overlay, player.car->sim,
                        _hudMap.loaded() ? int(_hudMap.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        _wear.fraction(), _pictures);
}

} // namespace reluva::age
