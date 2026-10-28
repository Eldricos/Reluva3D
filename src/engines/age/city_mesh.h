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
    };
    std::vector<gfx::Vertex> vertices;
    std::vector<Batch> batches;
    size_t skipped = 0; // attributes not drawn yet (tunnels)
};

void buildCityMesh(const Psdl& psdl, CityMesh& out);

} // namespace reluva::age
