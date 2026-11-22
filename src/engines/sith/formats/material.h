// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/colormap.h"
#include "image/image.h"

#include <cstdint>
#include <vector>

namespace reluva::sith {

/**
 * A .mat material: cels, each a plain colour or one of its textures, in 8
 * bits through the level's colormap (docs/engines/sith/games/jk/formats/mat.md).
 */
struct Material {
    /** One of the material's cels. */
    struct Cel {
        /** 0: a plain colour; 8: a texture. */
        uint32_t kind = 0;
        /** The colour's palette index. */
        uint8_t color = 0;
        /** Which of the textures, or -1 for a plain colour. */
        int texture = -1;
    };

    /** A texture and its smaller copies, each half the size of the one before. */
    struct Texture {
        int w = 0;
        int h = 0;
        /** Whether it has transparent pixels (colour 0). */
        bool transparent = false;
        std::vector<std::vector<uint8_t>> mips;
    };

    /** 0: colours only; 2: textures. */
    uint32_t type = 0;
    std::vector<Cel> cels;
    std::vector<Texture> textures;
};

/** Reads a .mat file; fails if it's not one, or it runs past its end. */
Error parseMaterial(const std::vector<uint8_t> &data, Material &out);

/**
 * A texture's mip `level` (0 the largest) as RGBA, through `palette`.
 * @param transparent Whether colour 0 of a transparent texture comes out
 *        with alpha 0.
 */
image::Image materialToImage(const Material::Texture &tex, int level, const Palette &palette, bool transparent);

} // namespace reluva::sith
