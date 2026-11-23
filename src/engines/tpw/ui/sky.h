// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "graphics/gfx3d.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <string>

namespace reluva::tpw {

/**
 * A world's sky, from its `sky/` folder (`sky_rgb.tga`, `sky.tga`,
 * `sky_cyl.tga`), and its fog's colour and light, from its `Standard.sam`.
 * See docs/engines/tpw/games/simcoaster/systems/sky.md.
 */
struct Sky {
    image::Image colours; // sky_rgb: the dome's colours, its middle overhead (inferred)
    image::Image clouds;  // sky: a cloud texture, laid over the colours
    image::Image horizon; // sky_cyl: a band round the horizon, clear towards its top
    Color fog{0, 0, 0};   // below the horizon
    /** Which way the world's light falls, in the game's space (y up): LightNormal. */
    gfx::Vec3 light{0.4f, -0.8f, 0.4f};
    bool hasPictures = false;

    /**
     * Reads the sky of the world in `folder`, as `levels/arabian`: its
     * pictures, and its settings from its `Standard.sam` over those of
     * `levels/Standard.sam`. Fails only when none of its pictures can be read.
     */
    Error load(const SearchSet &files, const std::string &folder);
};

/**
 * Fills the surface with the sky as the camera sees it: how the game
 * draws it isn't known, and this is a guess (sky.md).
 */
void drawSky(Surface &screen, const soft3d::OrbitCamera &camera, const Sky &sky);

} // namespace reluva::tpw
