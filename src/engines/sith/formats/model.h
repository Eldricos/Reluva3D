// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/text.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::sith {

/** A face's type flags: drawn from behind too (0x446548), and see-through (0x43b428). */
///@{
constexpr uint32_t kFaceDoubleSided = 0x1;
constexpr uint32_t kFaceTranslucent = 0x2;
///@}

/**
 * A .3do model: its materials, its meshes (in one or more geometry sets,
 * the levels of detail) and the hierarchy of nodes that places them
 * (docs/engines/sith/games/jk/formats/3do.md).
 */
struct Model {
    /** A polygon of a mesh. */
    struct Face {
        /** Into the model's materials; -1 for none. */
        int material = -1;
        /** kFaceDoubleSided, kFaceTranslucent; 0x4 is set on a few but never read by the game. */
        uint32_t type = 0;
        int geometryMode = 0;
        int lightingMode = 0;
        int textureMode = 0;
        float extraLight = 0;
        /** Its corners: into the mesh's vertices, and its texture vertices (-1 for none). */
        ///@{
        std::vector<int> vertices;
        std::vector<int> texVertices;
        ///@}
        Vec3 normal;
    };

    /** A mesh: a part of the model a node places. */
    struct Mesh {
        std::string name;
        float radius = 0;
        int geometryMode = 0;
        int lightingMode = 0;
        int textureMode = 0;
        std::vector<Vec3> vertices;
        /** Each vertex's light intensity, its fourth column. */
        std::vector<float> vertexLight;
        /** In texels of the face's material (u right, v down). */
        std::vector<std::array<float, 2>> texVertices;
        std::vector<Vec3> vertexNormals;
        std::vector<Face> faces;
    };

    /** A node of the hierarchy: a mesh (or none) placed relative to its parent. */
    struct Node {
        uint32_t flags = 0;
        uint32_t type = 0;
        /** Into the geometry set's meshes; -1 for none. */
        int mesh = -1;
        /** Other nodes; -1 for none. */
        ///@{
        int parent = -1;
        int child = -1;
        int sibling = -1;
        ///@}
        int childCount = 0;
        Vec3 position;
        /** Pitch, yaw and roll, in degrees. */
        Vec3 rotation;
        Vec3 pivot;
        std::string name;
    };

    std::vector<std::string> materials;
    float radius = 0;
    Vec3 insertOffset;
    /** Each geometry set's meshes; the first is the most detailed. */
    std::vector<std::vector<Mesh>> geosets;
    std::vector<Node> nodes;
};

/** Reads a .3do file; fails, saying where, if a line can't be read. */
Error parseModel(const std::vector<uint8_t> &data, Model &out);

} // namespace reluva::sith
