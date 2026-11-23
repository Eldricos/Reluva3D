// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/hmp.h"
#include "engines/tpw/formats/md2.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <string>
#include <vector>

namespace reluva::tpw {

/** A model read from the game's files, with its textures. */
struct LoadedModel {
    Model model;
    std::vector<image::Image> textures; // by the model's number for them; empty where not found
    size_t texturesFound = 0;
    /** The height map beside it, of its name (`wateride.hmp`); `hasHeights` false without one. */
    HeightMap heights;
    bool hasHeights = false;
    /** The middle of its placed meshes' box, and half its diagonal, in the viewer's world (z up). */
    soft3d::Vec centre{0, 0, 0};
    float radius = 1;

    /**
     * Reads `name`, finds and decodes its textures, and reads its height
     * map. Fails only when the model can't be read.
     */
    Error load(const SearchSet &files, const std::string &name);
};

/** Where the camera is: it looks at the model's middle from `distance` of its radii. */
struct ModelView {
    float yaw = 0.6f;    // radians about the up axis
    float pitch = 0.45f; // radians up from level
    float distance = 2.4f;
    bool textured = true;
    bool heights = false; // the height map's samples shown as dots
};

/**
 * Draws the model into an RGBA32 surface, over what is there, lit from
 * above, untextured faces grey; with `view.heights`, its height map's
 * samples over it as dots.
 */
void drawModel(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedModel &loaded, const ModelView &view);

} // namespace reluva::tpw
