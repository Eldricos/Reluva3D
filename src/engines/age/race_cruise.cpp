// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race menu's Cruise: the city to drive about with no race, its police
// patrolling where race/<city>/roam.aimap puts them. And, under it, San
// Francisco's "Stunt Mode: Acceleration" (mmlang.dll 199-204: "Race to the
// end of the pier! But don't fall in!", "Put the pedal to the metal!",
// "Great job! You can stop on a dime!") along race/sf/accel0waypoints.csv.
// See docs/engines/age-racing.md.

#include "engines/age/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr float kStopped = 0.5;  // m/s: stopped, at the end of the pier
constexpr float kFallenIn = 3;   // metres below the lowest point: in the water
constexpr float kBustedHold = 3; // seconds to see it before the car's put back
constexpr float kWreckedHold = 4;

float flatDistance(const Vec3& a, const Vec3& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

} // namespace

void RaceViewer::startCruise() {
    // The cruise starts at one of the city's Cops & Robbers places (as a
    // multiplayer cruise does), picked by the car, so a replay starts in
    // the same place; the stunt at its first point.
    cruiseHold_ = 0;
    stuntMessage_ = 0;
    stuntMessageFor_ = 0;
    if (stunt()) {
        // Faced along the road: the file's first heading is some 25
        // degrees off it, towards the buildings.
        if (race_.waypoints.size() > 1) {
            const Vec3 a = race_.waypoints[0].position, b = race_.waypoints[1].position;
            race_.waypoints[0].heading = std::atan2(b[0] - a[0], -(b[2] - a[2])) * 180 / kPi;
        }
        // The end of the pier: on along the last leg until there's no
        // ground under it (some 140 m past the last point).
        pierEnd_ = {};
        if (race_.waypoints.size() > 1) {
            const Vec3 a = race_.waypoints[race_.waypoints.size() - 2].position, b = race_.waypoints.back().position;
            const float len = std::hypot(b[0] - a[0], b[2] - a[2]);
            for (float d = 0; d < 500 && len > 0; d += 1) {
                const Vec3 p{b[0] + (b[0] - a[0]) / len * d, b[1] + 20, b[2] + (b[2] - a[2]) / len * d};
                CollisionWorld::RayHit hit;
                if (!world_.raycast(p, {0, -1, 0}, 30, hit))
                    break;
                pierEnd_ = {p[0], b[1], p[2]};
            }
        }
        return;
    }
    if (race_.index == kPrototypeRace && !race_.waypoints.empty()) {
        race_.waypoints.resize(1); // its one point: the start
        return;
    }
    std::vector<Waypoint> places;
    loadGoldPoints(ctx_.files, cities_[size_t(city_)].map, places);
    Waypoint start{scene_->start, -scene_->startYaw * 180 / kPi, 0};
    if (!places.empty())
        start = places[size_t(car_ * 7 + level_) % places.size()];
    race_.waypoints = {start};
}

void RaceViewer::stepCruise(float dt) {
    Racer& player = racers_.front();
    CarSim& sim = player.car->sim;
    if (cruising()) {
        // No end. Busted or wrecked: a moment to see it, then back where
        // the cruise started, mended.
        if (cruiseHold_ > 0) {
            cruiseHold_ -= dt;
            if (cruiseHold_ <= 0) {
                const Waypoint& w = race_.waypoints[0];
                sim.reset(player.car->setup, w.position + Vec3{0, 0.5f, 0}, headingToYaw(w.heading));
                wear_.mend(*player.car);
                if (smoke_)
                    smoke_->clear();
                life_.police().forgive();
                outcome_.clear();
                cameraReset_ = true;
                fill_ = true;
            }
        } else if (life_.police().busted()) {
            outcome_ = "Busted!";
            cruiseHold_ = kBustedHold;
        } else if (wear_.wrecked()) {
            outcome_ = "Wrecked!";
            cruiseHold_ = kWreckedHold;
        }
        return;
    }

    // The stunt: flat out down to the pier through the checkpoints, then
    // stopped short of its end without going over into the water.
    if (state_ == State::Finished)
        return;
    if (clock_ >= 0 && clock_ - dt < 0) {
        stuntMessage_ = 203; // "Put the pedal to the metal!"
        stuntMessageFor_ = LessonJudge::kMessageTime;
    }
    stuntMessageFor_ = std::max(0.0f, stuntMessageFor_ - dt);
    float lowest = race_.waypoints.front().position[1];
    for (const Waypoint& w : race_.waypoints)
        lowest = std::min(lowest, w.position[1]);
    if (sim.position[1] < lowest - kFallenIn) {
        outcome_ = "You fell in!";
        state_ = State::Finished;
    } else if (life_.police().busted() || wear_.wrecked()) {
        outcome_ = life_.police().busted() ? "Busted!" : "Wrecked!";
        state_ = State::Finished;
    } else if (player.next >= targets_.size() && std::fabs(sim.forwardSpeed()) < kStopped) {
        player.finishTime = clock_;
        char shortOf[64];
        std::snprintf(shortOf, sizeof shortOf, "  %.0f m from the end", flatDistance(sim.position, pierEnd_));
        outcome_ = strings_.get(204, "Great job! You can stop on a dime!") + "  " + clockText(clock_) + shortOf;
        state_ = State::Finished;
    }
}

void RaceViewer::drawCruiseHud() {
    // As the game's cruise: no words but the police after you and why the
    // car's about to be put back.
    const Racer& player = racers_.front();
    if (!outcome_.empty())
        drawCentred(outcome_, kFrameH / 2 - 40, {255, 120, 90}, 4);
    if (life_.police().chasing() > 0 && cruiseHold_ <= 0)
        font::drawText(overlay_, 4, kFrameH - 150, "POLICE CHASING", {255, 90, 90}, 2); // above the speedometer
    if (hudShown_)
        drawSpeedometer(overlay_, player.car->sim,
                        hudMap_.loaded() ? int(hudMap_.setup().left * float(kFrameW)) - 8 : kFrameW - 8,
                        wear_.fraction(), pictures_);
}

} // namespace reluva::age
