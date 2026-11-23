// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::tpw {

/**
 * A height map (`.hmp`), as SimCoaster's `Data/levels/arabian/rides/wateride/wateride.hmp`:
 * how high a model stands over the park's tiles, beside the model of its
 * name. See docs/engines/tpw/games/simcoaster/formats/hmp.md.
 */
class HeightMap {
public:
    /** Samples along a tile's side. */
    static constexpr int kSamplesPerTile = 5;
    /** A tile's side, in the models' units. */
    static constexpr float kTileSize = 10;
    /** The height of one step of a sample, in the models' units (inferred). */
    static constexpr float kHeightStep = 0.4f;

    /** Reads the map. Fails without its magic number, or on a table past the end. */
    Error parse(const std::vector<uint8_t> &data);

    /** Its size in tiles: across (x) and deep (z). */
    ///@{
    int width() const { return _width; }
    int depth() const { return _depth; }
    ///@}
    /** A sample's height in steps, 0 to 255; (x, z) in samples from the map's corner, in range. */
    uint8_t sample(int x, int z) const { return _samples[size_t(z) * size_t(_width * kSamplesPerTile) + size_t(x)]; }
    /**
     * A height for each tile, in steps, row by row: its highest sample's
     * for most tiles, more for some (inferred meaning).
     */
    const std::vector<uint8_t> &tileHeights() const { return _tileHeights; }
    /** A 0 or 1 for each tile, row by row: not worked out. */
    const std::vector<uint8_t> &tileFlags() const { return _tileFlags; }
    /** The box of the model it is for, as the model's header gives it: least x, y, z, then most. */
    const std::array<float, 6> &bounds() const { return _bounds; }

private:
    int _width = 0;
    int _depth = 0;
    std::vector<uint8_t> _samples;
    std::vector<uint8_t> _tileHeights;
    std::vector<uint8_t> _tileFlags;
    std::array<float, 6> _bounds{};
};

} // namespace reluva::tpw
