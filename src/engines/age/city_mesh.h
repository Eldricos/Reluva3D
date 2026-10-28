// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"
#include "engines/age/psdl.h"

#include <string>
#include <vector>

namespace reluva::age {

// A city's streets and buildings as triangles, generated from its PSDL
// blocks (see docs/formats/age/psdl.md for what each attribute makes),
// grouped by texture.
struct CityMesh {
    struct Batch {
        std::string texture;           // "" for none
        std::vector<uint32_t> indices; // every three a triangle
        std::vector<uint16_t> blocks;  // each triangle's PSDL block
        bool water = false;            // the .water planes: drawn, not collided with
        // 0: always drawn; 1: near (a road whose texture has a low-detail
        // version), 2: that road far off in its low-detail texture (not
        // collided with).
        uint8_t detail = 0;
    };
    std::vector<gfx::Vertex> vertices;
    std::vector<Batch> batches;
};

void buildCityMesh(const Psdl& psdl, CityMesh& out);

// city/<map>.water: the water's height, then blocks of the city (empty
// ones, under its bridges) where it's drawn. See psdl.md.
struct CityWater {
    float height = 0;
    std::vector<uint16_t> blocks;
};
Error parseCityWater(const std::string& text, CityWater& out);

// The water over those blocks, at its height, with the PSDL's texture
// `texture` (1-based, as its texture attributes count).
void addCityWater(const Psdl& psdl, const CityWater& water, uint32_t texture, CityMesh& out);

} // namespace reluva::age
