// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/lnd.h"
#include "engines/tpw/formats/omp.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/sky.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A world's terrain read from the game's files: the land around the park
 * (`terrain/base.MD2`), the park's ground (`terrain/base.lnd`), its
 * attribute map (`terrain/base.map`), its placed objects (`scape.omp`) and
 * its sky (`sky/`).
 */
struct LoadedWorld {
    LoadedModel surroundings;
    Land land;
    bool hasLand = false;
    AttributeMap attributes;
    bool hasAttributes = false;
    /** Its placed objects (`scape.omp`), the sound and particle emitters, it seems. */
    ObjectMap objects;
    bool hasObjects = false;
    /** Its sky, fog and light; `hasSky` false without its sky pictures. */
    Sky sky;
    bool hasSky = false;
    /**
     * The park's ground painted from the land's textures and masks, two
     * pixels a cell, clear where the park has no ground. How the game
     * paints it isn't worked out: this is a guess
     * (docs/engines/tpw/games/simcoaster/formats/lnd.md).
     */
    image::Image ground;
    /** Its model's meshes that are the park's fence as a new park has it ("basic hoard01" and on). */
    std::vector<size_t> fenceMeshes;
    /** The billboard outside the gates, its model's `signpost`: its box, least x, y, z then most, y up. */
    std::optional<std::array<float, 6>> billboard;

    /**
     * Reads the world in `folder`, as `levels/arabian`. Fails only when
     * its surroundings can't be read.
     */
    Error load(const SearchSet &files, const std::string &folder);
};

/**
 * Where the camera is: it looks at the park's middle, or at `target` if
 * `aimed`, from `distance` units.
 */
struct WorldView {
    bool aimed = false;
    gfx::Vec3 target{}; // in the world's units, y up
    float yaw = 0.6f;
    float pitch = 0.6f;
    float distance = 1300;
    bool textured = true;
    bool ground = true;   // the park's ground drawn
    bool objects = true;  // the placed objects drawn as markers
    bool sky = true;      // the sky drawn behind, the world lit by its light
    bool skyShown = true; // with `sky`, the sky's picture drawn; else black behind, its light kept
    bool fence = true;    // the world's own fence, its model's `basic hoard` meshes, drawn
};

/** The camera drawWorld draws through, for a screen of w × h. */
soft3d::OrbitCamera worldCamera(const LoadedWorld &world, const WorldView &view, int w, int h);

/**
 * Draws the world into an RGBA32 surface: its sky, or over what is there
 * without; with
 * `view.objects`, each placed object as a dot over it and its radius as a
 * ring of dots, coloured by its kind.
 */
void drawWorld(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedWorld &world, const WorldView &view);
/** Likewise, through `camera` rather than the view's own. */
void drawWorld(Surface &screen, soft3d::Rasterizer &rasterizer, const LoadedWorld &world, const WorldView &view,
               const soft3d::OrbitCamera &camera);

} // namespace reluva::tpw
