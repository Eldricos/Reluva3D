// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/water_warp.h"

#include <cmath>
#include <cstring>
#include <numbers>

namespace reluva::stawayteam {

namespace {

// The warped picture's side: the program's surface is 128 by 128, and its
// waves' tables 128 long (0x5f54ad).
constexpr int kSide = 128;

std::string textOf(const TextObject &o, const char *key) {
    const std::string *value = o.value(key);
    return value ? *value : std::string();
}

WaterWave readWave(const TextObject *o) {
    WaterWave wave;
    if (o) {
        wave.numWaves = int(o->integer("Num Waves").value_or(0));
        wave.amplitude = float(o->number("Amplitude").value_or(0.0));
        wave.pixelsPerSecond = float(o->number("Pixels Per Second").value_or(0.0));
    }
    wave.tabulate();
    return wave;
}

// How far along a wave has moved: its speed by the time, rounded, round
// the 128 (0x5f46ae).
int phase(const WaterWave &wave, double seconds) {
    const long moved = std::lrint(seconds * double(wave.pixelsPerSecond));
    return int(((moved % kSide) + kSide) % kSide);
}

const uint8_t *source(const image::Image &texture, int x, int y) {
    // A texture not 128 square repeats over the 128 (inferred: the
    // program's are).
    return texture.pixel(x % texture.w, y % texture.h);
}

} // namespace

void WaterWave::tabulate() {
    // Each entry's angle in steps of 2 pi / 256, rounded to the nearest
    // pixel as the FPU rounds.
    for (int i = 0; i < kSide; ++i) {
        const double angle = double(i * numWaves * 2) * (std::numbers::pi / 128.0);
        shift[size_t(i)] = int(std::lrint(std::cos(angle) * double(amplitude)));
    }
}

WaterWarp readWaterWarp(const TextObject &o) {
    WaterWarp warp;
    warp.v1 = readWave(o.object("V1 Wave"));
    warp.v2 = readWave(o.object("V2 Wave"));
    warp.h1 = readWave(o.object("H1 Wave"));
    warp.h2 = readWave(o.object("H2 Wave"));
    // Horizontal Then Vertical Animated warps the Animated Texture's frame
    // as Horizontal Then Vertical; no map has one.
    const std::string order = textOf(o, "Render Type");
    if (order == "Horizontal")
        warp.order = WaterWarp::Order::kHorizontal;
    else if (order == "Vertical")
        warp.order = WaterWarp::Order::kVertical;
    else if (order == "Vertical Then Horizontal")
        warp.order = WaterWarp::Order::kVerticalThenHorizontal;
    warp.texture = textOf(o, "Static Texture");
    return warp;
}

void warpWater(const WaterWarp &warp, const image::Image &texture, double seconds, image::Image &out) {
    out.w = kSide;
    out.h = kSide;
    out.rgba.assign(size_t(kSide) * kSide * 4, 0);
    if (texture.w <= 0 || texture.h <= 0)
        return;
    const int pv1 = phase(warp.v1, seconds);
    const int pv2 = phase(warp.v2, seconds);
    const int ph1 = phase(warp.h1, seconds);
    const int ph2 = phase(warp.h2, seconds);
    // The H waves shift a row along by its place; the V waves a column up
    // or down by its. Both: the second by where the first took it from
    // (0x5f4890, 0x5f4ae0).
    const auto across = [&](int row) {
        return warp.h1.shift[size_t((row + ph1) & 127)] + warp.h2.shift[size_t((row + ph2) & 127)];
    };
    const auto down = [&](int col) {
        return warp.v1.shift[size_t((col + pv1) & 127)] + warp.v2.shift[size_t((col + pv2) & 127)];
    };
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            int sx = x;
            int sy = y;
            switch (warp.order) {
            case WaterWarp::Order::kHorizontal:
                sx = (x + across(y)) & 127;
                break;
            case WaterWarp::Order::kVertical:
                sy = (y + down(x)) & 127;
                break;
            case WaterWarp::Order::kHorizontalThenVertical:
                sx = (x + across(y)) & 127;
                sy = (y + down(sx)) & 127;
                break;
            case WaterWarp::Order::kVerticalThenHorizontal:
                sy = (y + down(x)) & 127;
                sx = (x + across(sy)) & 127;
                break;
            }
            std::memcpy(out.pixel(x, y), source(texture, sx, sy), 4);
        }
}

} // namespace reluva::stawayteam
