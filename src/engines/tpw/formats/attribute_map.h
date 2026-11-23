// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::tpw {

/**
 * A world's attribute map (`base.map`, "TP2M"), as SimCoaster's
 * `Data/levels/arabian/terrain/base.map`: a byte for each tile of the
 * world. Not the sound banks' `.map` files, which share the extension.
 * See docs/engines/tpw/games/simcoaster/formats/attribute-map.md.
 */
class AttributeMap {
public:
    /** Reads the map. Fails without "TP2M" and its MAP chunk, or on a chunk past the end. */
    Error parse(const std::vector<uint8_t> &data);

    /** Its size in tiles. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    ///@}
    /** A tile's byte, (x, y) in range: its bits not worked out. */
    uint8_t at(int x, int y) const { return _tiles[size_t(y) * size_t(_width) + size_t(x)]; }

private:
    int _width = 0;
    int _height = 0;
    std::vector<uint8_t> _tiles;
};

} // namespace reluva::tpw
