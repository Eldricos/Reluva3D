// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/hmp.h"
#include "engines/tpw/formats/md2.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <array>
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

/** A point of the game's space (y up) in the viewer's world (z up). */
soft3d::Vec toViewerWorld(const gfx::Vec3 &p);

/** The light drawModel uses: from above, a little to one side, in the viewer's world. */
soft3d::Vec defaultLight();

/**
 * Draws a triangle given in the viewer's world, lit by its own slope
 * against `light` (a direction in the viewer's world, either way):
 * textured, its texels whose alpha is under half left out, or grey
 * without a texture.
 */
void drawTexturedTriangle(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera,
                          const std::array<soft3d::Vec, 3> &corners, const std::array<std::array<float, 2>, 3> &uv,
                          const image::Image *texture, const soft3d::Vec &light);

/** Draws a model's meshes as drawModel does, into a drawing already begun, lit by `light`. */
void drawMeshes(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera, const LoadedModel &loaded,
                bool textured, const soft3d::Vec &light);

/**
 * Draws the model into an RGBA32 surface, over what is there, lit from
 * above, untextured faces grey; with `view.heights`, its height map's
 * samples over it as dots.
 */
void drawModel(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedModel &loaded, const ModelView &view);

} // namespace reluva::tpw
