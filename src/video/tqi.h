// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::tqi {

/**
 * Electronic Arts' TQI video, the pictures of its movies' `pIQT` chunks
 * (SimCoaster's `.tgq` logo), as docs/common/video/tqi.md describes: every
 * frame a picture of its own, coded as MPEG-1 codes its intra frames.
 */
class Decoder {
public:
    /**
     * Decodes a frame (a `pIQT` chunk's contents) into the picture, which
     * takes the frame's size. Fails if it's cut short, its size isn't whole
     * macroblocks, or its codes go wrong; the picture is then as far as it
     * got.
     */
    Error decode(const uint8_t *data, size_t size);

    /** The picture's size in pixels. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    ///@}
    /** The picture: RGBA32, opaque. */
    const Surface &surface() const { return _picture; }

private:
    int _width = 0;
    int _height = 0;
    Surface _picture;
    std::vector<uint8_t> _y;
    std::vector<uint8_t> _cb;
    std::vector<uint8_t> _cr;
};

} // namespace reluva::tqi
