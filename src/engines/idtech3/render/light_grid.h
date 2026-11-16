// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// Raven's light styles' current colours (RGBA), as its games' code sets
// them each frame; every one full white until then.
constexpr int kStyleCount = 64;
using LightStyleColors = std::array<std::array<uint8_t, 4>, kStyleCount>;
inline LightStyleColors fullLightStyles() {
    LightStyleColors c;
    for (auto &s : c)
        s = {255, 255, 255, 255};
    return c;
}

// The light at a point, for lighting models: an ambient colour, a
// directed colour and the direction it comes from (towards the light),
// colours 0-255.
struct PointLight {
    std::array<float, 3> ambient{};
    std::array<float, 3> directed{};
    std::array<float, 3> direction{0, 0, 1};
};

// A map's light grid (see docs/engines/idtech3/formats/bsp.md): light sampled on
// a regular grid over the world, by default every 64 x 64 x 128 units.
class LightGrid {
public:
    // `cellSize` from the worldspawn's "gridsize" key, if it has one. A
    // grid whose size doesn't match the world's bounds is left empty.
    // `overbright`: its light doubled (brightenLight), as the level's.
    void build(const Bsp &bsp, std::array<float, 3> cellSize = {64, 64, 128}, bool overbright = true);
    bool empty() const { return _cells.empty(); }

    // Blends the eight surrounding grid points, skipping those inside walls
    // (all black), as the original does; adds its minimum ambient light
    // for models unless `forModel` is false (the light as the grid has it,
    // as the renderer's LightForPoint gives the games). Without a grid: a
    // fixed light from above.
    PointLight sample(const std::array<float, 3> &pos, bool forModel = true) const;
    // Each point's styles are scaled by their colours when sampled.
    void setStyles(const LightStyleColors &colors) { _styles = colors; }

private:
    std::array<float, 3> _origin{};
    std::array<float, 3> _size{64, 64, 128};
    std::array<int, 3> _count{};
    // Each point's light for each of its styles, brightened, up to the
    // first unused; a point inside a wall has none.
    struct Point {
        std::array<std::array<uint8_t, 3>, kLightStyles> ambient{};
        std::array<std::array<uint8_t, 3>, kLightStyles> directed{};
        std::array<uint8_t, kLightStyles> styles{kNoStyle, kNoStyle, kNoStyle, kNoStyle};
        std::array<uint8_t, 2> latLong{};
        bool inWall = true;
    };
    LightStyleColors _styles = fullLightStyles();
    std::vector<Point> _points;   // the grid's distinct points
    std::vector<uint32_t> _cells; // each grid point's, in _points
};

} // namespace reluva::idtech3
