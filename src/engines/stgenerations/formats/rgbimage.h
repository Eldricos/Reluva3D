// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "graphics/palette.h"
#include "engines/stgenerations/canvas.h"
#include "graphics/draw.h"

namespace reluva::stgenerations {

class BitmapDecoder;

class Overlay;

/**
 * A picture in true colour (0xAARRGGBB, alpha 0 where it's see-through),
 * for the screens the original runs in 16-bit colour, where every bitmap
 * keeps its own palette: the bridge and space combat.
 */
class RgbImage {
public:
    /** No transparent colour. */
    static const int kOpaque = -1;

    /**
     * Takes an 8-bit bitmap cut into frames (counted from the bottom, as
     * everywhere in the game); pixels of palette index key are see-through.
     * False, leaving the image as it was, if the bitmap isn't 8-bit.
     */
    bool load(const BitmapDecoder &decoder, int frames = 1, int key = kOpaque);
    /**
     * Takes an overlay (.RLE) in the given palette, cut into frames of
     * frameHeight rows counted from the top; the last may be short.
     */
    void load(const Overlay &overlay, const Palette &palette, int frameHeight);

    /** Whether a picture has been loaded. */
    bool isLoaded() const { return _w > 0; }
    /** The width, a frame's height, and how many frames. */
    ///@{
    int getWidth() const { return _w; }
    int getFrameHeight() const { return _frameHeight; }
    int getFrameCount() const { return _frames; }
    ///@}
    /** A pixel of a frame, from its top left; 0 outside. */
    uint32_t getPixel(int frame, int x, int y) const;

    /** Draws a frame with its top left at (x, y) on an ARGB8888 surface. */
    void draw(Canvas &dst, int frame, int x, int y, const Rect *clip = nullptr) const;
    /** Draws a frame stretched to w x h (nearest neighbour). */
    void drawScaled(Canvas &dst, int frame, int x, int y, int w, int h, const Rect *clip = nullptr) const;

private:
    // Where frame's row y starts in _pixels.
    const uint32_t *row(int frame, int y) const;

    int _w = 0;
    int _h = 0;
    int _frameHeight = 0;
    int _frames = 0;
    // Frames numbered from the bottom of the strip, as bitmaps are.
    bool _fromBottom = true;
    std::vector<uint32_t> _pixels;
};

} // namespace reluva::stgenerations
