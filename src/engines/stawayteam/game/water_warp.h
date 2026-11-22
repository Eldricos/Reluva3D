// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/formats/text_object.h"
#include "image/image.h"

#include <array>
#include <string>

namespace reluva::stawayteam {

/**
 * One of a water warp's four waves (`CWaterWaveFormData`): a cosine over
 * the texture's 128 rows or columns, moving along them
 * (docs/engines/stawayteam/systems/maps.md#water).
 */
struct WaterWave {
    int numWaves = 0;             // whole cosines across the 128
    float amplitude = 0.0f;       // pixels each way
    float pixelsPerSecond = 0.0f; // how fast it moves along; less than 0 back
    std::array<int, 128> shift{}; // the cosine, in whole pixels (0x5f3cf0)

    /** Works out `shift` from the other three. */
    void tabulate();
};

/**
 * A `Water Warp Effects/<name>.txt` (`CWaterWarpEffectFrame`): its texture,
 * the waves that shift its rows (H1, H2) and its columns (V1, V2), and the
 * order they're put on it.
 */
struct WaterWarp {
    enum class Order { kHorizontal, kVertical, kHorizontalThenVertical, kVerticalThenHorizontal };

    WaterWave v1;
    WaterWave v2;
    WaterWave h1;
    WaterWave h2;
    Order order = Order::kHorizontalThenVertical;
    std::string texture; // its Static Texture, of Cache/Textures
};

/** Reads a water warp from its object. */
WaterWarp readWaterWarp(const TextObject &o);

/**
 * The 128 by 128 picture `texture` warped to at `seconds` of the game's
 * time (0x5f5450), into `out`.
 */
void warpWater(const WaterWarp &warp, const image::Image &texture, double seconds, image::Image &out);

} // namespace reluva::stawayteam
