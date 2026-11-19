// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/palette.h"
#include "engines/stgenerations/canvas.h"
#include "graphics/draw.h"

namespace reluva::stgenerations {

class BitmapDecoder;

// One of the game's interface bitmaps: a strip of frames of frameHeight
// rows (0 for one frame), drawn in the screen's palette.
struct FramedImage {
    // The bitmap's own indices; keys are compared with these.
    Canvas surface;
    // Its colours in the screen's palette (Windows matched a DIB's colours to the screen's).
    uint8_t remap[256];
    // The index of the first pixel in the file: the bottom left.
    uint8_t firstPixel = 0;
    int frameHeight = 0;

    enum Key {
        kOpaque = -1,
        // See-through where a pixel matches the first one in the file, its
        // bottom left (0x425A00): the inventory icons' purple, the power
        // arrow's red, the health bar's magenta.
        kFirstPixel = -2
    };

    // Take a decoded bitmap; a negative frame height asks for that many frames.
    void set(const BitmapDecoder &decoder, const Palette &palette, int frameHeight);

    int getFrameCount() const;
    int getHeight() const { return frameHeight > 0 ? frameHeight : surface.h; }

    // Copy a frame to (x, y), skipping pixels equal to key (an index, or a
    // Key). Frames count up from the bottom of the strip.
    void draw(Canvas &dst, int frame, int x, int y, int key = kOpaque, const Rect *clip = nullptr) const;
};

} // namespace reluva::stgenerations
