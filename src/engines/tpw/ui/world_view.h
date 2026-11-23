// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/lnd.h"
#include "engines/tpw/ui/model_view.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <string>

namespace reluva::tpw {

/**
 * A world's terrain read from the game's files: the land around the park
 * (`terrain/base.MD2`), the park's ground (`terrain/base.lnd`) and its
 * attribute map (`terrain/base.map`).
 */
struct LoadedWorld {
    LoadedModel surroundings;
    Land land;
    bool hasLand = false;
    AttributeMap attributes;
    bool hasAttributes = false;
    /**
     * The park's ground painted from the land's textures and masks, two
     * pixels a cell, clear where the park has no ground. How the game
     * paints it isn't worked out: this is a guess
     * (docs/engines/tpw/games/simcoaster/formats/lnd.md).
     */
    image::Image ground;

    /**
     * Reads the world in `folder`, as `levels/arabian`. Fails only when
     * its surroundings can't be read.
     */
    Error load(const SearchSet &files, const std::string &folder);
};

/** Where the camera is: it looks at the park's middle from `distance` units. */
struct WorldView {
    float yaw = 0.6f;
    float pitch = 0.6f;
    float distance = 1300;
    bool textured = true;
    bool ground = true; // the park's ground drawn
};

/** Draws the world into an RGBA32 surface, over what is there. */
void drawWorld(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedWorld &world, const WorldView &view);

} // namespace reluva::tpw
