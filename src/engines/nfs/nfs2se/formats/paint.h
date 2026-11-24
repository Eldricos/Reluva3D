// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * A colour a car can be painted (GameData/CarModel/pc/carhsvt.txt, the
 * same as the first of nfs2sen.exe's table at 0x4c3634;
 * docs/engines/nfs/games/nfs2se/formats/geo.md, The paint).
 */
struct PaintColour {
    int hue = 0;        // 0-255, a whole turn
    int saturation = 0; // 0-256
    int value = 0;      // 0-256
    // The shade, of 127, the darker shades rise to the colour by; above
    // it, they go on toward white.
    int threshold = 0;
};

/** Reads carhsvt.txt: commented text, four numbers a colour. Fails on a count not a whole number of colours. */
Error parsePaintColours(std::string_view text, std::vector<PaintColour> &out);

/** The colour's red, green and blue, 0-256 each (0x444af8). */
std::array<int, 3> paintRgb(const PaintColour &colour);

/** Whether a picture's colour is the paint's: green, from 0x80 up, with little red and blue (0x444fef). */
bool isPaint(uint8_t r, uint8_t g, uint8_t b);

/**
 * The colour the game makes of a paint colour of a car's picture, painted
 * `colour` (0x445030): by the green's shade above 0x80, black rising to the
 * colour, then on toward white; whitened by the red and blue it had. The
 * colours that aren't the paint's (isPaint) are left as they are.
 */
std::array<uint8_t, 3> repaint(uint8_t r, uint8_t g, uint8_t b, const PaintColour &colour);

} // namespace reluva::nfs::nfs2se
