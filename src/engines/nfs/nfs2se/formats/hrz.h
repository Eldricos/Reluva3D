// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <string_view>

namespace reluva::nfs::nfs2se {

/** Red, green, blue, 0-255. */
using Rgb = std::array<int, 3>;

/**
 * A track's sky (docs/engines/nfs/games/nfs2se/formats/hrz.md): commented
 * text, each value named by the comment before it, which the names here
 * follow. The software program's (Tracks/pc/trNN.hrz) and the 3dfx
 * program's (Tracks/se/3trNN.hrz) differ after their first values.
 */
struct Sky {
    /** Which program's sky: the two have other values after their first. */
    enum Kind {
        kSoftware, ///< 26 values
        kGlide     ///< 27 values: a cloud dome, and shaded earth and sky
    };
    Kind kind = kSoftware;

    // Both.
    bool blackHorizon = false;
    bool mirror = false;
    int radius = 0;       // the horizon's
    int angle = 0;        // its turn, in degrees
    int flatDistance = 0; // to the horizon for flat projection (no more than the radius)
    int base = 0;         // the shaded area's base, in the world's height
    int height = 0;       // the shaded area's, from the base

    // The software program's.
    int pixmapBase = 0; // percent of the height
    Rgb skyColour{};
    Rgb baseColour{};
    // Depth cueing: a B-spline through these (distance, light) points
    // (0x45be20). A distance is a depth bucket, half a unit each from 10
    // units out; a light of L moves the colours L/16 toward colourValue.
    std::array<int, 4> depthDistances{}; // 0-1100
    std::array<int, 4> depthLights{};    // 0-15
    int colourSystem = 0;                // 0 RGB, 1 HSV
    Rgb colourValue{};                   // what the distance fades toward

    // The 3dfx program's.
    int cloudDomeOffset = 0;
    int cloudDomeHeight = 0;
    int midpoint = 0;     // of the shaded area, from the base
    int pixmapTop = 0;    // from the base
    int pixmapBottom = 0; // from the base
    Rgb earthTop{};
    Rgb earthBase{};
    Rgb skyTop{};
    Rgb skyBaseSun{};      // toward the sun
    Rgb skyBaseOpposite{}; // away from it
};

/** Reads a sky file of either kind, by its count of values. */
Error parseSky(std::string_view text, Sky &out);

/**
 * A track's lights (Tracks/se/trNN.lgt), commented text as the skies are.
 * Neither program reads them; all eight hold the same values.
 */
struct Lights {
    std::array<std::array<int, 2>, 4> depthCueing{}; // pairs, as the file has them
    std::array<Rgb, 16> palette{};                   // the light palettes, 0-127
    int maximum = 0;                                 // intensity
    int minimum = 0;
    std::array<int, 7> effectLight{}; // type, then two colours
};

/** Reads a .lgt file: 65 values. */
Error parseLights(std::string_view text, Lights &out);

} // namespace reluva::nfs::nfs2se
