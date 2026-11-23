// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::tpw {

/**
 * A world's park ground (`.lnd`), as SimCoaster's `Data/levels/arabian/terrain/base.lnd`:
 * masks over the ground, cell by cell, and the textures and masks the
 * ground is painted with, at three sizes. See
 * docs/engines/tpw/games/simcoaster/formats/lnd.md.
 */
class Land {
public:
    /** A cell's side, in the models' units (inferred). */
    static constexpr float kCellSize = 2.5f;

    /** A picture of one byte a pixel: a mask. */
    struct Mask {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels; // row by row
    };

    /** The ground's textures and masks at one size. */
    struct Level {
        uint32_t scale = 0; // 64, 32, 16 for the three: not worked out
        std::vector<image::Image> textures;
        std::vector<Mask> masks;
    };

    /**
     * Reads the ground. Fails for a version other than 3, or on a picture
     * past the end or not of its expected kind.
     */
    Error parse(const std::vector<uint8_t> &data);

    /** Its size in cells: across (x) and deep (z). */
    ///@{
    int width() const { return _width; }
    int depth() const { return _depth; }
    ///@}
    /**
     * A cell's four bytes; (x, z) from the file's first cell, in range. Byte
     * 0: 255 where the park has ground, 0 where it has none (its water,
     * its entrance), a few cells between; bytes 1 to 3 not worked out.
     */
    const uint8_t *cell(int x, int z) const { return &_cells[(size_t(z) * size_t(_width) + size_t(x)) * 4]; }
    /** The 10 bytes after the version: not worked out. */
    const std::array<uint8_t, 10> &settings() const { return _settings; }
    /** The textures and masks, largest first. */
    const std::vector<Level> &levels() const { return _levels; }

private:
    int _width = 0;
    int _depth = 0;
    std::vector<uint8_t> _cells;
    std::array<uint8_t, 10> _settings{};
    std::vector<Level> _levels;
};

} // namespace reluva::tpw
