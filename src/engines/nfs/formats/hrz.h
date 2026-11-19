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
// weather), as docs/engines/nfs/games/nfs3/formats/hrz.md describes. The file is commented
// text; its comments name each value, and the names here follow them.
struct Horizon {
    using Rgb = std::array<int, 3>;
    struct FogPoint {
        Rgb colour{};
        int density = 0; // percent
    };
    struct FogRegion {
        FogPoint start;
        FogPoint apex;
        FogPoint end;
        // road slices
        int beforeSlice = 0;
        int centreSlice = 0;
        int afterSlice = 0;
    };

    Rgb fogColour{};
    // percent
    int fogDensity = 0;
    int fogOnPixmap = 0;
    std::vector<FogRegion> fogRegions;
    int cloudType = 0; // 0 none?, 1 additive, 2 blended
    int fogClouds = 0;
    int lightning = 0;
    int lightningChance = 0;
    int visualHeight = 0;
    std::array<int, 2> cloudDomeOffset{};
    int cloudBright = 0;
    int cloudVariance = 0;
    int nimbus = 0;
    int windSpeed = 0;
    int windDirection = 0;
    bool blackHorizon = false;
    bool mirror = false;
    // world units, degrees
    int radius = 0;
    int rotation = 0;
    bool hasPixmap = false;
    // Heights: the Gouraud-shaded area's base (world), then offsets from it.
    int shadedBase = 0;
    int shadedHeight = 0;
    int shadedMidpoint = 0;
    int pixmapTop = 0;
    int pixmapBottom = 0;
    Rgb earthTop{};
    Rgb earthBase{};
    Rgb skyTop{};
    Rgb skyBaseSun{};
    Rgb skyBaseOpposite{};
    struct Weather {
        int type = 0;
        int startSlice = 0;
        int endSlice = 0;
        int fade = 0;
        // ticks
        int stayOn = 0;
        int fadeOff = 0;
        int stayOff = 0;
        int fadeOn = 0;
    } weather;
    Rgb ambient{}; // percent
};

Error parseHorizon(std::string_view text, Horizon &out);

// The fog value the game sends its renderer (THRASH state 0xe; thrash.md,
// Fog) for a fog density in percent: for the scenery, and for the sky's
// horizon picture, earth and clouds, which it fogs less by the horizon's
// radius and the fog's effect on the pixmap. 0 for no fog.
// The fog where the camera's car is (0x478e10): the horizon's own colour
// and density, or within the first dynamic fog region whose slices hold
// `slice` (the car's road slice, +0xEC, by 0x4217e0; not its block), the
// start's mixed towards the apex's by the slices from the before one to
// the centre, then the apex's towards the end's to the after one; the
// mix x87 floats, each value truncated (0x4dfd56).
Horizon::FogPoint fogAtSlice(const Horizon &h, int slice);
int sceneryFogValue(int density);
int skyFogValue(const Horizon &h, int density);

// How nfs3.exe lays out the sky (docs/engines/nfs/games/nfs3/formats/hrz.md, What the game
// does with it). The horizon has 16 slots of 22.5 degrees; the slot at a
// heading h (in turns, atan2(x, z) / 2 pi in the track's coordinates) is
// 16 x frac(h + 0.75 + horizonTurn(h)).
float horizonTurn(const Horizon &h); // the rotation, in turns, 0 to 1
// The sky's colour at each slot edge, just above the horizon: the sun
// side's colour at edge 0, stepping an eighth of the way to the far
// side's each edge, and back.
std::array<Horizon::Rgb, 16> skyBaseColours(const Horizon &h);
// A row of the sky as nfs3.exe lays it out each frame (0x479160,
// 0x479500): eight flat quads side by side in one vertical plane at the
// horizon radius along the camera's forward on the ground, each a slot
// (2 pi radius / 16) wide, between the heights bottom and bottom + height
// (the camera's taken away). Its 9 bottom and 9 top corners are
// camera-relative, in the track's coordinates, left to right; quad i is
// slot (firstSlot + i) % 16. The game projects them with the camera, so
// they tilt with its pitch. The sky's shading, the earth's and the
// horizon picture are each such a row (0x479680, 0x4799d0, 0x479b90).
struct SkyRow {
    std::array<std::array<float, 3>, 9> bottom{};
    std::array<std::array<float, 3>, 9> top{};
    int firstSlot = 0;
};
SkyRow skyRow(const Horizon &h, const std::array<float, 3> &forward, float bottom, float height);
// The horizon picture's tile for a slot (0x478bb0): slot mod 8, or with
// the mirror flag, for slots 8-15, 15 - slot drawn flipped.
int horizonTile(const Horizon &h, int slot, bool *flipped = nullptr);
// Whether the game draws no sky and clears to black (0x47a4c0): the black
// horizon flag, or on Lost Canyons (track 5) while the camera's car is on
// road slices 906 to 1064. Otherwise the clear colour is the sky's top.
bool skyBlack(const Horizon &h, int track, int roadSlice);
// The cloud dome's height above the camera at a distance r along the
// ground, for a point of that brightness (with nimbus 100, brighter
// points hang lower), and its grid: 13 x 13 points 450 apart, -3000 to
// +2400 on each axis, each with a brightness (0-255; random, as the
// game's, from `seed`), row by row along z.
float cloudHeight(const Horizon &h, float r, int brightness = 0);
constexpr int kCloudPoints = 13;
constexpr float kCloudStep = 450;
constexpr float kCloudFirst = -3000;
// `seed` is the game's: its tick count at start-up (`0x6fd3b4`, from
// `0x47242c`), which seeds its generator (GameRandom) at each race's
// set-up; the sky's set-up draws the points' random amounts from it before
// anything else does, so a seed gives the game's layout exactly.
std::array<uint8_t, kCloudPoints * kCloudPoints> cloudBrightness(const Horizon &h, uint32_t seed);

// The game's random numbers (`0x4e0d88`, its state six words at
// `0x5644e0`-`0x5644f4`): an add-with-carry sum down the words, the last
// word counting the calls. Seeded (`0x4e0dee`) with the seed plus a
// constant in the first word, and each word the one before plus another.
struct GameRandom {
    explicit GameRandom(uint32_t seed);
    uint32_t next();
    uint32_t state[6];
};
// A cloud point's alpha for its brightness (0x47ae10, 0x47b2e5): the
// brightness itself with the 3dfx and software renderers; with the
// Direct3D, Rage and PowerVR ones (renderer bits 0x1e0) the same for every
// point, the cloud brightness less half the variance (its low byte).
uint8_t cloudAlpha(const Horizon &h, uint8_t brightness, bool direct3d);
// The clouds' drift each game tick, in tiles along x and z.
std::array<float, 2> cloudDrift(const Horizon &h);
// The clouds' texture offset for the camera at x, z (track coordinates),
// in tiles, added to the drift: a tile for each visual height moved, so
// the grid, which follows the camera, shows the clouds passing over it.
std::array<float, 2> cloudOffset(const Horizon &h, float x, float z);

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
bool lightningStarts(const Horizon &h, uint64_t tick, SkyRandom &random);
LightningBolt makeBolt(const Horizon &h, SkyRandom &random);
void tickBolt(LightningBolt &bolt, SkyRandom &random);
// How bright its flash is (0-255): full for half its life, then fading.
int boltFlash(const LightningBolt &bolt);

} // namespace reluva::nfs
