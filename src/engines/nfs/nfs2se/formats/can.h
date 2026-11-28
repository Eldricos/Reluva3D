// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/nfs2se/formats/trk.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * An animation script (Tracks/Data/pc/trNNSS.can): a camera's or an
 * object's moves, a key every few ticks of the race's clock, from a car or
 * the object (docs/engines/nfs/games/nfs2se/formats/can.md). Scripts 00 and
 * 04 to 05 take the camera to the car as a race starts, 01 and 06 round it
 * as it ends, 02 and 03 throw over an object a car hits.
 */
struct AnimationScript {
    /** Where the camera or object is at one moment. */
    struct Key {
        Point offset{}; // from the car or the object, in the files' unit
        // A quaternion x, y, z, w, 0x4000 being 1 (0x4580d0). A camera's is
        // turned a quarter round x as it's used (0x42da42).
        std::array<int16_t, 4> rotation{};
    };

    uint16_t unknown = 0;     // 11 values among the files; the program doesn't read it
    uint16_t ticksPerKey = 0; // 6 in every file; the program takes 6 outside 1-400 (0x42dc09)
    std::vector<Key> keys;
};

/**
 * Reads a .can file. Fails if its size isn't the 8 bytes of its header and
 * 20 a key, or the header gives another.
 */
Error parseAnimationScript(const std::vector<uint8_t> &data, AnimationScript &out);

} // namespace reluva::nfs::nfs2se
