// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/shader.h"

#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace reluva::idtech3 {

// Raven's surface sprites over a level's surfaces (Jedi Outcast on; see
// docs/engines/idtech3/formats/shader.md): the grass, vines and bubbles a
// shader's sprite stage scatters on its surface's upward (or downward)
// facing triangles. Where they stand is worked out once, as the level
// loads; each frame turns them to the camera, sways them and fades them
// with distance.
class SurfaceSpriteSet {
public:
    void clear();
    // Scatters `parms`' sprites over a surface's triangles: `vertices` as
    // drawn (colours brightened), `indices` into them, three a triangle.
    // `material` draws them, `additive`: its blend is GL_ONE GL_ONE.
    void add(int surface, int material, int fog, float sort, bool additive, const SurfaceSprites &parms,
             const std::vector<gfx::Vertex> &vertices, const uint32_t *indices, size_t indexCount);
    bool empty() const { return _groups.empty(); }
    size_t spriteCount() const { return _sprites.size(); }
    // Every sort they're drawn at.
    std::vector<float> sorts() const;

    // A frame's quads of the groups at sorts sortMin..sortMax whose surface
    // `visible` says can be seen, from `eye` with the camera's `left` and
    // `up`, `timeMs` into the level: their vertices and indices, and for
    // each group drawn its material, fog and index range.
    struct Part {
        int material = 0;
        int fog = -1;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };
    void build(const std::array<float, 3> &eye, const std::array<float, 3> &left, const std::array<float, 3> &up,
               float timeMs, float sortMin, float sortMax, const std::function<bool(int surface)> &visible,
               std::vector<gfx::Vertex> &vertices, std::vector<uint32_t> &indices, std::vector<Part> &parts) const;

private:
    // One triangle's corners, their light (the vertex colour's blue, as
    // the original takes it) and its sprites.
    struct Triangle {
        std::array<std::array<float, 3>, 3> pos{};
        std::array<float, 3> light{};
        float step = 0; // the sprites' spacing, as a fraction of the triangle
        uint32_t firstSprite = 0;
        uint32_t spriteCount = 0;
    };
    // One sprite: where in its triangle (weights of its corners), or for
    // an effect the cell it starts each life in; and its own random draws.
    struct Sprite {
        std::array<float, 3> weights{};
        std::array<float, 2> cell{};
        uint8_t turn = 0;     // which of four sideways directions (vertical)
        float fadeStart = 0;  // how far into the fade it goes
        float sizeRandom = 0; // how much of the variance
        bool flipped = false;
        std::array<float, 2> skew{}; // ssVertSkew's lean, as a fraction of its height
        float lifeOffset = 0;        // an effect's, in milliseconds
        uint32_t seed = 0;
    };
    struct Group {
        int surface = -1;
        int material = 0;
        int fog = -1;
        float sort = 3;
        bool additive = false;
        SurfaceSprites parms;
        uint32_t firstTriangle = 0;
        uint32_t triangleCount = 0;
        std::array<float, 3> mins{};
        std::array<float, 3> maxs{};
    };
    std::vector<Group> _groups;
    std::vector<Triangle> _triangles;
    std::vector<Sprite> _sprites;
};

} // namespace reluva::idtech3
