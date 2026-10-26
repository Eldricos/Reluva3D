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

} // namespace reluva::nfs
