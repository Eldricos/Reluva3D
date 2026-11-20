// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/age/game/collision.h"

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

/**
 * What the city's surfaces are made of, for driving on them
 * (city/materials.mtl: "mtl grass { elasticity: ... }" blocks), and which
 * texture is which (city/materials.csv: "texture,physics"). See
 * docs/engines/age/games/mm2/systems/collisions.md.
 */
struct PhysicsMaterial {
    std::string name;
    float elasticity = 0.9f;
    float friction = 0.9f;
    int sound = 0;  // which surface of the car sound tables
    float drag = 0; // slows a wheel on it
    float width = 0;
    float height = 0;
    float depth = 0; // deep water (100) sinks a car
    // ptxindex and ptxthreshold: the wheels' particles (see CollisionSurface).
    std::array<int, 2> particles{-1, -1};
    std::array<float, 2> particleSlip{0.25f, 0.5f};
};

/**
 * The city's physics materials, by name and by texture. A name or texture
 * not listed is "_default", the roads' material.
 */
class PhysicsMaterials {
public:
    /**
     * Reads both files. Fails if there's no materials.mtl; without
     * materials.csv every texture is the default.
     */
    Error load(const SearchSet &files);
    /**
     * Reads the two files' text. Fails on a material not closed. A texture
     * listed as "none", or as a material that isn't there, is the default.
     */
    Error parse(std::string_view mtl, std::string_view csv);

    /**
     * The material by name (default if there's none such), and a texture's
     * (by name, with or without an animation frame: "s_thames-0009"). Names
     * are matched ignoring case.
     */
    ///@{
    const PhysicsMaterial &byName(const std::string &name) const;
    const PhysicsMaterial &forTexture(const std::string &texture) const;
    ///@}
    /**
     * As a collision surface: friction relative to the default material's
     * (so roads keep CollisionSurface's), its sound, drag and depth.
     */
    CollisionSurface surface(const PhysicsMaterial &m) const;

private:
    std::vector<PhysicsMaterial> _materials;
    std::map<std::string, size_t> _byName;
    std::map<std::string, std::string> _textures; // lower-case texture -> material
    PhysicsMaterial _default;
};

} // namespace reluva::age
