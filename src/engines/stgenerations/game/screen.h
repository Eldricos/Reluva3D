// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/palette.h"
#include "engines/stgenerations/canvas.h"

namespace reluva::stgenerations {

class Platform;

// The game's back buffer. Everything is drawn here in 8-bit palettized
// form and pushed to the Platform once per frame.
//
// 640x480 in 256 colours is assumed from the era and platform; confirm it
// against the original executable's DirectDraw setup.
class Screen {
public:
    static const int kWidth = 640;
    static const int kHeight = 480;

    explicit Screen(Platform* system);

    void init();

    Canvas& getSurface() { return _surface; }
    Palette& getPalette() { return _palette; }

    void clear(uint8_t color = 0);

    // Upload the palette to the backend; call after changing it.
    void applyPalette();

    // Draw a surface centred on screen, converting truecolour images to the palette.
    void drawCentered(const Canvas& image);

    // Draw an image centred in the top areaHeight rows, enlarged by the
    // largest whole factor (up to 3x) that fits.
    void drawImage(const Canvas& image, int areaHeight);

    // Copy the back buffer to the backend and present it.
    void update();

private:
    Platform* _system;
    Canvas _surface;
    Palette _palette;
};

} // namespace reluva::stgenerations

