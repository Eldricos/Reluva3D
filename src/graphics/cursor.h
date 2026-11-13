// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva {

// A game's own mouse pointer, for System::setMouseCursor: a picture in RGBA
// (4 bytes per pixel in memory order R, G, B, A; A = 0 is see-through) and
// the point in it that is the mouse's position (its hotspot), all in the
// game's pixels. The backend scales it as it scales the game's picture.
struct Cursor {
    int w = 0, h = 0;
    int hotX = 0, hotY = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4; }
    uint8_t *pixel(int x, int y) { return rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4; }
    const uint8_t *pixel(int x, int y) const { return rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4; }
    bool operator==(const Cursor &) const = default;
};

namespace cursor {

// A cursor from palette indices, as most games of the 1990s keep them:
// `pixels` is w x h bytes, `pitch` apart, coloured from `palette` (R, G, B
// bytes for each of 256 entries); pixels of the `transparent` index are
// see-through (none if it's -1).
Cursor fromIndexed(const uint8_t *pixels, int w, int h, int pitch, const uint8_t *palette, int transparent, int hotX,
                   int hotY);
// The same from a surface: CLUT8 (coloured from `palette`, 256 entries) or
// RGBA32 (its own alpha; `palette` and `transparent` unused).
Cursor fromSurface(const Surface &surface, const Color *palette, int transparent, int hotX, int hotY);

// The cursor `factor` times its size (1 or more; fractions round to the
// nearest pixel), its hotspot moved with it: sharp pixels, or smoothed.
Cursor scaled(const Cursor &c, float factor, bool smooth = false);

// Draws the cursor onto an RGBA32 surface with its hotspot at (x, y),
// blending by alpha, clipped to the surface: for backends that can't show
// it as the system's pointer.
void draw(Surface &target, const Cursor &c, int x, int y);

} // namespace cursor

} // namespace reluva
