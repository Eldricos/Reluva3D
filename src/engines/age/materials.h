// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/age/collision.h"

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// What the city's surfaces are made of, for driving on them
// (city/materials.mtl: "mtl grass { elasticity: ... }" blocks), and which
// texture is which (city/materials.csv: "texture,physics"). See
// docs/engines/age-driving.md.
struct PhysicsMaterial {
    std::string name;
    float elasticity = 0.9f, friction = 0.9f;
    int sound = 0;                          // which surface of the car sound tables
    float drag = 0;                         // slows a wheel on it
    float width = 0, height = 0, depth = 0; // depth: deep water (100) sinks a car
    // ptxindex and ptxthreshold: the wheels' particles (see CollisionSurface).
    std::array<int, 2> particles{-1, -1};
    std::array<float, 2> particleSlip{0.25f, 0.5f};
};

class PhysicsMaterials {
public:
    // Both files; without them every surface is the default.
    Error load(const SearchSet& files);
    Error parse(std::string_view mtl, std::string_view csv);

    // The material by name (default if there's none such), and a texture's
    // (by name, with or without an animation frame: "s_thames-0009").
    const PhysicsMaterial& byName(const std::string& name) const;
    const PhysicsMaterial& forTexture(const std::string& texture) const;
    // As a collision surface: friction relative to the default material's
    // (so roads keep CollisionSurface's), its sound, drag and depth.
    CollisionSurface surface(const PhysicsMaterial& m) const;

private:
    std::vector<PhysicsMaterial> materials_;
    std::map<std::string, size_t> byName_;
    std::map<std::string, std::string> textures_; // lower-case texture -> material
    PhysicsMaterial default_;
};

} // namespace reluva::age
