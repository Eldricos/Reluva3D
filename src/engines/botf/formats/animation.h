// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/image.h"

#include <cstdint>
#include <vector>

// The .ani animations in stbof.res: the planets, suns and stellar objects
// the galaxy screen shows turning (docs/engines/botf/formats/ani.md).

namespace reluva::botf {

/** An animation's frames, each its own picture, in the order they're shown. */
struct Animation {
    /** A frame's size, in pixels. */
    ///@{
    int frameWidth = 0;
    int frameHeight = 0;
    ///@}
    std::vector<image::Image> frames; // RGBA, transparent where the game draws nothing
};

/**
 * Reads an .ani file: a 16-byte header, then a 16-bit TGA holding the frames
 * left to right, then down.
 * @return An error if the header and the TGA don't agree.
 */
Error readAnimation(const std::vector<uint8_t> &data, Animation &out);

} // namespace reluva::botf
