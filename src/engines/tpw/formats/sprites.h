// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A sprite's frames (`.TPC` or `.FPC`, as `esprites/Generic/Kids/SPR_KB.TPC`):
 * a palette of 256 colours with their opacity, then each frame's pixels,
 * run-length coded row by row. See
 * docs/engines/tpw/games/simcoaster/formats/sprites.md.
 */
class SpriteSheet {
public:
    /** A palette colour. */
    struct Colour {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a; // 0 clear, 255 opaque
    };

    struct Frame {
        int width = 0;
        int height = 0;
        int scaleX = 0; // 128 in every frame of the game's: not worked out
        int scaleY = 0;
        int left = 0;                // where the frame's top-left lies from the point it stands on: about −width / 2
        int top = 0;                 // and −height
        std::vector<uint8_t> pixels; // palette numbers, row by row
    };

    /** Reads the frames. Fails on a frame or row running past the end, or one that doesn't fill its width. */
    Error parse(const std::vector<uint8_t> &data);

    const std::array<Colour, 256> &palette() const { return _palette; }
    const std::vector<Frame> &frames() const { return _frames; }

private:
    std::array<Colour, 256> _palette{};
    std::vector<Frame> _frames;
};

/**
 * A sprite's animations (`.ESP`): for each of its slots, the frames of
 * the sheet it plays, so many to a direction, in so many directions. See
 * docs/engines/tpw/games/simcoaster/formats/sprites.md.
 */
class SpriteSet {
public:
    static constexpr size_t kSlots = 20;

    /** An animation: its first frame in the sheet, its frames for each direction, and its directions. */
    struct Animation {
        int first = 0;
        int frames = 0;     // 0 for a slot not used
        int directions = 0; // 1, or 5: away from the viewer, then round to the right, to facing it
    };

    /** Reads the file. Fails without "ESP_FILE2.00", or one cut short. */
    Error parse(const std::vector<uint8_t> &data);

    /** The sheet's name it gives, as "SPR_KB.TPS". */
    const std::string &sheet() const { return _sheet; }
    const std::array<Animation, kSlots> &animations() const { return _animations; }

private:
    std::string _sheet;
    std::array<Animation, kSlots> _animations{};
};

} // namespace reluva::tpw
