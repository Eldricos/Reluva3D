// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/image.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::stawayteam {

/**
 * A picture in the game's 16-bit frame format: a .frm16 file, and each frame
 * of a sequence or model (docs/engines/stawayteam/formats/frm16.md).
 */
struct Frame {
    int width = 0;
    int height = 0;
    // The point in the frame that goes where it's drawn: the file's own
    // numbers, which the game turns round (0x4b3a77).
    int originX = 0;
    int originY = 0;
    uint32_t flags = 0;           // 0x20 for raw pixels, 0x2024 for run-length coded ones; not otherwise known
    std::vector<uint16_t> pixels; // RGB 5:6:5, width x height, top row first
    std::vector<uint8_t> alpha;   // for each pixel: 0 not drawn, 255 opaque, between them blended
};

/**
 * Reads a frame from the start of `data`.
 * @param used Set to the bytes it takes up, for a frame inside another file.
 * @return An error if it isn't a 16-bit frame or is damaged.
 */
Error readFrame(const uint8_t *data, size_t size, Frame &out, size_t *used = nullptr);

/** The frame as an RGBA picture, its alpha kept. */
image::Image frameImage(const Frame &frame);

} // namespace reluva::stawayteam
