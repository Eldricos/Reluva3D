// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::cinepak {

// Cinepak ('cvid'), the video codec of many 1990s games' AVI and QuickTime
// files (docs/common/video/cinepak.md). A frame is made of horizontal strips;
// each 4x4 block is drawn from codebooks of 2x2 YUV cells, one cell
// stretched over the whole block (V1) or four of them (V4). Codebooks carry
// over from frame to frame, and down from one strip to the next unless the
// frame says otherwise; blocks an inter frame skips keep the last picture.
class Decoder {
public:
    Decoder(int width, int height);

    // Decodes a frame onto the picture. An empty frame (a dropped one)
    // leaves it as it was; false if the frame is too short to be one.
    // Damage inside a frame stops that part of it, keeping what was drawn.
    bool decode(const uint8_t* data, uint32_t size);

    int width() const { return width_; }
    int height() const { return height_; }
    // The picture: RGBA32, opaque.
    const Surface& surface() const { return picture_; }

private:
    struct Cell {
        uint8_t y[4];
        int8_t u, v;
    };
    struct Strip {
        Cell v1[256], v4[256];
    };

    static void decodeCodebook(Cell* codebook, int chunk, const uint8_t* data, const uint8_t* end);
    bool decodeVectors(const Strip& strip, int chunk, int top, int bottom, const uint8_t* data, const uint8_t* end);

    int width_, height_;
    Surface picture_;
    std::vector<Strip> strips_;
};

} // namespace reluva::cinepak
