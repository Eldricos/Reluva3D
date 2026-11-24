// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/sprites.h"
#include "graphics/soft3d/rasterizer.h"

#include <cstdint>
#include <string>

namespace reluva::tpw {

/** A sprite read from the game: its frames and its animations. */
struct LoadedSprite {
    SpriteSheet sheet;
    SpriteSet set;

    /**
     * Reads a sprite by its name without its ending, as
     * `esprites/Generic/Kids/SPR_KB`: its `.ESP` and its `.TPC`.
     */
    Error load(const SearchSet &files, const std::string &name);
};

/** Which frame to draw, and whether mirrored. */
struct SpritePose {
    size_t frame = 0;
    bool mirror = false;
};

/**
 * The frame of an animation for a heading seen from the viewer, in
 * radians (0 walking away, π/2 to the right, π towards it), so many
 * frames on from its start, round and round: its five directions drawn
 * as they are on the right, and mirrored on the left.
 */
SpritePose spritePose(const SpriteSet::Animation &animation, float heading, uint32_t step);

/**
 * Draws a frame onto an RGBA32 surface, blended by its palette's
 * opacity, the point it stands on at (x, y), `scale` screen pixels to a
 * pixel of the frame; mirrored left for right if asked.
 */
void drawSpriteFrame(Surface &screen, const SpriteSheet &sheet, size_t frame, float x, float y, float scale,
                     bool mirror);
/**
 * The same, hidden where the scene drawn by `depth` is nearer than
 * `invZ` (1 / view-space depth, the sprite's as a whole).
 */
void drawSpriteFrame(Surface &screen, const SpriteSheet &sheet, size_t frame, float x, float y, float scale,
                     bool mirror, const soft3d::Rasterizer &depth, float invZ);

} // namespace reluva::tpw
