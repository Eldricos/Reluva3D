// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::nfs {

// A track's sky, fog and weather settings (3trNN[n][w].hrz: day, night,
// weather), as docs/formats/nfs/hrz.md describes. The file is commented
// text; its comments name each value, and the names here follow them.
struct Horizon {
    using Rgb = std::array<int, 3>;
    struct FogPoint {
        Rgb colour{};
        int density = 0; // percent
    };
    struct FogRegion {
        FogPoint start, apex, end;
        int beforeSlice = 0, centreSlice = 0, afterSlice = 0; // blocks
    };

    Rgb fogColour{};
    int fogDensity = 0, fogOnPixmap = 0; // percent
    std::vector<FogRegion> fogRegions;
    int cloudType = 0; // 0 none?, 1 additive, 2 blended
    int fogClouds = 0;
    int lightning = 0, lightningChance = 0;
    int visualHeight = 0;
    std::array<int, 2> cloudDomeOffset{};
    int cloudBright = 0, cloudVariance = 0, nimbus = 0;
    int windSpeed = 0, windDirection = 0;
    bool blackHorizon = false, mirror = false;
    int radius = 0, rotation = 0; // world units, degrees
    bool hasPixmap = false;
    // Heights: the Gouraud-shaded area's base (world), then offsets from it.
    int shadedBase = 0, shadedHeight = 0, shadedMidpoint = 0, pixmapTop = 0, pixmapBottom = 0;
    Rgb earthTop{}, earthBase{}, skyTop{}, skyBaseSun{}, skyBaseOpposite{};
    struct Weather {
        int type = 0, startSlice = 0, endSlice = 0, fade = 0;
        int stayOn = 0, fadeOff = 0, stayOff = 0, fadeOn = 0; // ticks
    } weather;
    Rgb ambient{}; // percent
};

Error parseHorizon(std::string_view text, Horizon& out);

// The fog value the game sends its renderer (THRASH state 0xe; thrash.md,
// Fog) for a fog density in percent: for the scenery, and for the sky's
// horizon picture, earth and clouds, which it fogs less by the horizon's
// radius and the fog's effect on the pixmap. 0 for no fog.
int sceneryFogValue(int density);
int skyFogValue(const Horizon& h, int density);

// How nfs3.exe lays out the sky (docs/formats/nfs/hrz.md, What the game
// does with it). The horizon has 16 slots of 22.5 degrees; the slot at a
// heading h (in turns, atan2(x, z) / 2 pi in the track's coordinates) is
// 16 x frac(h + 0.75 + horizonTurn(h)).
float horizonTurn(const Horizon& h); // the rotation, in turns, 0 to 1
// The sky's colour at each slot edge, just above the horizon: the sun
// side's colour at edge 0, stepping an eighth of the way to the far
// side's each edge, and back.
std::array<Horizon::Rgb, 16> skyBaseColours(const Horizon& h);
// The cloud dome's height above the camera at a distance r along the
// ground, for a point of that brightness (with nimbus 100, brighter
// points hang lower), and its grid: 13 x 13 points 450 apart, -3000 to
// +2400 on each axis, each with a brightness (0-255; random, as the
// game's, from `seed`), row by row along z.
float cloudHeight(const Horizon& h, float r, int brightness = 0);
constexpr int kCloudPoints = 13;
constexpr float kCloudStep = 450, kCloudFirst = -3000;
std::array<uint8_t, kCloudPoints * kCloudPoints> cloudBrightness(const Horizon& h, uint32_t seed);
// The clouds' drift each game tick, in tiles along x and z.
std::array<float, 2> cloudDrift(const Horizon& h);

// Random numbers for the sky, standing in for the game's rand(): 0 to
// 32767.
struct SkyRandom {
    uint32_t seed = 1;
    int next() {
        seed = seed * 1103515245u + 12345u;
        return static_cast<int>((seed >> 16) & 0x7fff);
    }
};

// Lightning as the game makes it (0x47b5c0, 0x4945a0, 0x494420, 0x494630):
// a bolt is points crawling across the cloud dome from where it starts,
// each joined to its parent, camera-relative in the track's coordinates.
// It lives 24 game ticks; each tick its points jitter.
struct LightningBolt {
    struct Point {
        std::array<float, 3> at{};
        int parent = -1;  // -1 for the first
        float angle = 0;  // turns, the way it heads
        float jitter = 0; // how far it moves a tick, at most
    };
    std::vector<Point> points;
    int life = 24;
};
// Whether a bolt starts on this game tick: every `lightningChance` ticks,
// a `lightning` percent chance.
bool lightningStarts(const Horizon& h, uint64_t tick, SkyRandom& random);
LightningBolt makeBolt(const Horizon& h, SkyRandom& random);
void tickBolt(LightningBolt& bolt, SkyRandom& random);
// How bright its flash is (0-255): full for half its life, then fading.
int boltFlash(const LightningBolt& bolt);

} // namespace reluva::nfs
