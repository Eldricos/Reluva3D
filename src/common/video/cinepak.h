// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Cinepak ("cvid") video, as Windows' and QuickTime's codec writes it.
// See docs/formats/video.md.

#include "common/image.h"
#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::video {

class CinepakDecoder {
public:
    // Decodes a frame onto the last one (inter frames change only parts of
    // it); the picture is RGBA, opaque. An empty frame leaves it as it was.
    Error decode(const uint8_t* data, size_t size);
    const image::Image& picture() const { return picture_; }
    void reset();

private:
    // A codebook entry: a 2x2 block of colours.
    struct Entry {
        uint8_t rgb[4][3];
    };
    struct Strip {
        std::array<Entry, 256> v4{}, v1{};
    };
    Error decodeStrip(const uint8_t* p, size_t size, int top, int bottom, int width, Strip& strip);
    void decodeVectors(int kind, const uint8_t* q, const uint8_t* end, int top, int bottom, int width,
                       const Strip& strip);
    void drawV1(const Entry& e, int x, int y);
    void drawV4(const Entry& a, const Entry& b, const Entry& c, const Entry& d, int x, int y);

    image::Image picture_;
    std::vector<Strip> strips_;
};

} // namespace reluva::video
