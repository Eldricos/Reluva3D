// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * A Storm3D model (`.sod`): a tree of nodes, some with meshes, the
 * lighting materials they use, and the nodes' keyframed movements. Read
 * in every layout Star Trek: Armada's files have, `StarTrekDB` 1.0 to 1.5
 * and `Storm3D_SW` 1.6 to 1.8. See
 * docs/engines/storm3d/games/armada/formats/sod.md.
 */
struct SodModel {
    /** What a node is, by the names the files give each kind. */
    enum NodeType : uint16_t {
        kNull = 0,    // structure: root, lod0, hardpoints (hp20), the ship's systems (damage, crew...)
        kMesh = 1,    // geometry
        kSprite = 3,  // a sprite table's sprite node by its name (redstrobe, crew1)
        kEmitter = 12 // a sprite table's particle emitter, named in emitter
    };

    /** A node's place: a 3 x 3 matrix by rows, then a translation. */
    using Transform = std::array<float, 12>;

    /** Before 1.7, a table of names and textures at the start; its use isn't known. */
    struct TextureRef {
        std::string name;
        std::string texture;
        float value = 0;                // 0 but in 1.2's (0.75)
        std::array<uint8_t, 3> bytes{}; // 2 1 2 in most
    };

    /** A lighting material: three colours, a power, and a mode byte. */
    struct LightingMaterial {
        std::string name;
        std::array<float, 3> ambient{};
        std::array<float, 3> diffuse{};
        std::array<float, 3> specular{};
        float power = 0;
        uint8_t mode = 0; // 0, 1 or 2
    };

    /** A triangle: its corners' vertex and texture coordinate indices. */
    struct Face {
        std::array<uint16_t, 3> vertex{};
        std::array<uint16_t, 3> uv{};
    };

    /** The faces of a mesh lit by one lighting material. */
    struct Group {
        std::string material;
        std::vector<Face> faces;
    };

    /** A mesh's geometry. */
    struct Mesh {
        std::string drawMode; // from 1.7: "default", "additive", "alphathreshold", "wormhole"; else ""
        std::string texture;  // a texture's name, without folder or extension
        std::vector<std::array<float, 3>> vertices;
        std::vector<std::array<float, 2>> uvs;
        std::vector<Group> groups;        // before 1.4 one group, its polygons cut into triangles
        uint8_t flag = 0;                 // from 1.4: 0 or 1, after the groups
        std::vector<uint16_t> vertexList; // from 1.4: a list of the mesh's vertices, in one model
    };

    struct Node {
        uint16_t type = kNull;
        std::string name;
        std::string parent; // "" for a root
        Transform transform{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        int mesh = -1;       // into meshes, or -1
        std::string emitter; // a kEmitter's
    };

    /** A node's movement: a transform for each keyframe, evenly over the length. */
    struct Animation {
        std::string node;
        float length = 0; // in seconds
        uint16_t flags = 0;
        std::vector<Transform> keys;
    };

    /** From 1.6: a node's texture moved by a sprite table's animation. */
    struct TextureAnimation {
        uint8_t kind = 0; // 4 in every one
        std::string node;
        std::string animation; // a sprite table's: tex1x4, shldieanm
        float value = 0;       // from 1.8
    };

    std::string magic; // "StarTrekDB" or "Storm3D_SW"
    float version = 0;
    std::vector<TextureRef> textureRefs;
    std::vector<LightingMaterial> materials;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Animation> animations;
    std::vector<TextureAnimation> textureAnimations;

    /**
     * Reads a whole file.
     * @return a failure if it isn't a model, is cut short, has bytes left
     *         over, or a face names a vertex the mesh doesn't have
     */
    Error parse(const std::vector<uint8_t> &data);

    /** The first node of that name, or nullptr. */
    const Node *node(std::string_view name) const;
};

} // namespace reluva::storm3d
