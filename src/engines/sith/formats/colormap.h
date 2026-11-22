// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::sith {

/** A palette of 256 colours, as the colormaps and pictures hold them. */
using Palette = std::array<Color, 256>;

/**
 * A .cmp colormap: a level's palette, its light levels and, in most, a
 * table for blending two colours (docs/engines/sith/games/jk/formats/cmp.md).
 */
struct Colormap {
    /** How many light levels it has: 0 the darkest, the last as bright as the palette. */
    static constexpr int kLightLevels = 64;

    /** The three floats after the flags: 1, 1, 1 but in the red and yellow maps. */
    std::array<float, 3> tint{};
    Palette palette{};
    /** For each light level, what each colour becomes: kLightLevels rows of 256. */
    std::vector<uint8_t> light;
    /** 256 rows of 256, when the file has it; empty when not. */
    std::vector<uint8_t> blend;

    /** Colour `index` at light `level` (0 to kLightLevels - 1). */
    uint8_t lit(int level, uint8_t index) const { return light[size_t(level) * 256 + index]; }
};

/** Reads a .cmp file; fails if it's not one, or is cut short. */
Error parseColormap(const std::vector<uint8_t> &data, Colormap &out);

} // namespace reluva::sith
