// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool age cars: every car of an install driven by Reluva's
// simulation on a flat plane, against the figures the game gives for it
// (its .info's garage bars, its .vehCarSim's power and mass). The bars
// aren't the simulation's numbers, so what's compared is how the cars
// rank. See docs/engines/age/games/mm2/systems/car-simulation.md (Against
// the game's figures).

#include "devtools/tool.h"
#include "engines/age/filesystem.h"
#include "engines/age/game/car.h"
#include "engines/age/game/collision.h"
#include "engines/age/ui/screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::age;
using namespace reluva::tool;

constexpr float kMph = 0.44704f; // m/s
constexpr float kStep = 0.05f;   // s
constexpr float kQuarterMile = 402.3f;

struct Run {
    std::string car;
    // The garage's bars (.info).
    float horsepower = 0;
    float topSpeed = 0;
    // The simulation's numbers (.vehCarSim).
    float power = 0; // MaxHorsePower over Mass
    // Reluva's driving, from a standstill at full throttle.
    float to60 = -1; // s
    float to100 = -1;
    float quarter = -1;
    float top = 0;      // mph after a minute
    float brake60 = -1; // m from 60 mph to a stop
};

float infoFigure(const std::string &text, const char *key) {
    const size_t at = text.find(key);
    return at == std::string::npos ? 0 : std::strtof(text.c_str() + at + std::strlen(key), nullptr);
}

void settle(CarSim &car, const CollisionWorld &flat) {
    const CarControls none;
    for (int i = 0; i < 30; ++i)
        car.step(none, flat, kStep);
}

Run drive(const CarSetup &setup, const CollisionWorld &flat) {
    Run r;
    CarSim car;
    car.reset(setup, {0, 0, 0}, 0);
    settle(car, flat);
    CarControls go;
    go.throttle = 1;
    const Vec3 start = car.position;
    float t = 0;
    for (int i = 0; i < 1200; ++i) {
        car.step(go, flat, kStep);
        t += kStep;
        const float v = car.forwardSpeed();
        if (r.to60 < 0 && v >= 60 * kMph)
            r.to60 = t;
        if (r.to100 < 0 && v >= 100 * kMph)
            r.to100 = t;
        if (r.quarter < 0 && length(car.position - start) >= kQuarterMile)
            r.quarter = t;
    }
    r.top = car.forwardSpeed() / kMph;

    CarSim braking;
    braking.reset(setup, {0, 0, 0}, 0);
    settle(braking, flat);
    for (int i = 0; i < 1200 && braking.forwardSpeed() < 60 * kMph; ++i)
        braking.step(go, flat, kStep);
    const Vec3 from = braking.position;
    CarControls stop;
    stop.brake = 1;
    for (int i = 0; i < 400 && braking.forwardSpeed() > 0.5f; ++i)
        braking.step(stop, flat, kStep);
    r.brake60 = length(braking.position - from);
    return r;
}

// Spearman's rank agreement of two lists: 1 the same order, -1 reversed.
float rankAgreement(const std::vector<float> &a, const std::vector<float> &b) {
    auto ranks = [](const std::vector<float> &v) {
        std::vector<size_t> order(v.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return v[x] < v[y]; });
        std::vector<float> rank(v.size());
        for (size_t k = 0; k < order.size(); ++k)
            rank[order[k]] = float(k);
        return rank;
    };
    const std::vector<float> ra = ranks(a);
    const std::vector<float> rb = ranks(b);
    const float n = float(a.size());
    float sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += (ra[i] - rb[i]) * (ra[i] - rb[i]);
    return n < 2 ? 0 : 1 - 6 * sum / (n * (n * n - 1));
}

int cmdCars(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool age cars <game folder>\n");
        return 2;
    }
    SearchSet files;
    std::vector<std::string> warnings;
    if (Error err = mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    CollisionWorld flat;
    flat.add({-9000, 0, -9000}, {-9000, 0, 9000}, {9000, 0, 9000});
    flat.add({-9000, 0, -9000}, {9000, 0, 9000}, {9000, 0, -9000});
    flat.build(64);

    std::vector<Run> runs;
    for (const CarInfo &info : findCars(files)) {
        std::vector<uint8_t> data;
        CarSetup setup;
        if (files.read("tune/" + info.base + ".info", data) || loadCarSetup(files, info.base, setup))
            continue;
        const std::string text(data.begin(), data.end());
        Run r = drive(setup, flat);
        r.car = info.base;
        r.horsepower = infoFigure(text, "Horsepower=");
        r.topSpeed = infoFigure(text, "Top Speed=");
        r.power = setup.mass > 0 ? setup.maxHorsePower / setup.mass : 0;
        runs.push_back(r);
    }
    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.car < b.car; });
    std::printf("%-12s %5s %5s %6s %6s %6s %6s %6s %6s\n", "car", "hp", "top", "hp/kg", "0-60", "0-100", "1/4mi", "mph",
                "60-0m");
    std::vector<float> topBar;
    std::vector<float> top;
    std::vector<float> power;
    std::vector<float> quarter;
    std::vector<float> to60;
    for (const Run &r : runs) {
        std::printf("%-12s %5.0f %5.0f %6.3f %6.1f %6.1f %6.1f %6.1f %6.1f\n", r.car.c_str(), r.horsepower, r.topSpeed,
                    r.power, r.to60, r.to100, r.quarter, r.top, r.brake60);
        topBar.push_back(r.topSpeed);
        top.push_back(r.top);
        power.push_back(r.power);
        quarter.push_back(-r.quarter);
        to60.push_back(-r.to60);
    }
    std::printf("cars: %zu; rank agreement: top speed with the Top Speed bar %.2f, quarter mile with power to "
                "weight %.2f, 0-60 with power to weight %.2f\n",
                runs.size(), double(rankAgreement(top, topBar)), double(rankAgreement(quarter, power)),
                double(rankAgreement(to60, power)));
    return 0;
}

const Commands kCommands("age",
                         {
                             {"cars", "<game folder>",
                              "drive every car on a flat plane in Reluva's simulation (0-60, 0-100, quarter mile, "
                              "top speed, braking) and rank the cars against the game's own figures for them",
                              cmdCars},
                         });

} // namespace
