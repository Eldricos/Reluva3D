// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/palette.h"
#include "engines/stgenerations/canvas.h"

namespace reluva::stgenerations {

class Platform;

/**
 * The game's back buffer for its 8-bit screens: drawn here, in its
 * palette, and pushed to the Platform once a frame.
 *
 * 640x480 in 256 colours is assumed from the era and platform
 * (docs/engines/stgenerations/UNCONFIRMED.md); confirm it against the
 * original executable's DirectDraw setup.
 */
class Screen {
public:
    static const int kWidth = 640;
    static const int kHeight = 480;

    /** A screen on `system`, which it doesn't own. */
    explicit Screen(Platform *system);

    /** Sets the Platform's screen up 640x480 in 8-bit, and makes the buffer. */
    void init();

    /** The back buffer, and its palette. */
    ///@{
    Canvas &getSurface() { return _surface; }
    Palette &getPalette() { return _palette; }
    ///@}

    /** Fills the back buffer with one colour. */
    void clear(uint8_t color = 0);

    /** Uploads the palette to the Platform; call after changing it. */
    void applyPalette();

    /** Draws a surface centred on screen, converting truecolour images to the palette. */
    void drawCentered(const Canvas &image);

    /**
     * Draws an image centred in the top areaHeight rows, enlarged by the
     * largest whole factor (up to 3x) that fits. A true colour one isn't
     * enlarged, and is centred on the whole screen.
     */
    void drawImage(const Canvas &image, int areaHeight);

    /** Copies the back buffer to the Platform and shows it. */
    void update();

private:
    Platform *_system;
    Canvas _surface;
    Palette _palette;
};

} // namespace reluva::stgenerations
