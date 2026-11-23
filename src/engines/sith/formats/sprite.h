// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/text.h"

#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A .spr sprite: a material drawn flat, facing the camera, as blaster
 * bolts, explosions and splashes are (docs/engines/sith/games/jk/formats/spr.md).
 */
struct Sprite {
    std::string material;
    int type = 0;
    /** Its size, in the game's units. */
    ///@{
    float width = 0;
    float height = 0;
    ///@}
    int geometryMode = 0;
    int lightingMode = 0;
    int textureMode = 0;
    float extraLight = 0;
    /** Where it's drawn, about its thing. */
    Vec3 offset;
};

/** Reads a .spr file; fails if its line hasn't all eleven fields. */
Error parseSprite(const std::vector<uint8_t> &data, Sprite &out);

/**
 * A .par particle cloud: a material drawn at each of its points, all of
 * one size (docs/engines/sith/games/jk/formats/par.md).
 */
struct Particles {
    float size = 0;
    std::string material;
    int lightingMode = 0;
    float radius = 0;
    Vec3 insertOffset;
    std::vector<Vec3> points;
    /** Each point's intensity, its fourth column. */
    std::vector<float> pointLight;
};

/** Reads a .par file; fails, saying where, if a line can't be read. */
Error parseParticles(const std::vector<uint8_t> &data, Particles &out);

} // namespace reluva::sith
