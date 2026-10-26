// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::netimmerse {

// A LEGO Alpha Team model (.FIN): a NetImmerse stream of objects, each
// written as its class name, its address when saved (its id, which links
// between objects use) and its fields. See docs/formats/netimmerse/fin.md.
//
// The objects are found by their headers, then each class that's
// understood is read; a class that isn't (animation, skins, morphs) is
// skipped without losing the rest. Every object records whether its fields
// were read to exactly its end, which `reluva-tool fin-check` counts.

// NiAVObject: anything placed in the scene.
struct FinAvObject {
    std::string name;
    std::array<float, 3> translation{};
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1}; // row by row
    float scale = 1;
    std::vector<uint32_t> properties; // ids
};

// NiNode and its subclasses (NiBillboardNode, NiLODNode, the 3ds animation
// nodes and bones, the game's DDUnit and DDEnv).
struct FinNode : FinAvObject {
    std::vector<uint32_t> children; // ids
    // NiLODNode: one child per level of detail, the first the nearest.
    bool levelsOfDetail = false;
};

// NiTriShape (and NiEnvMappedTriShape): a triangle mesh with its data
// inline.
struct FinShape : FinAvObject {
    uint16_t vertexCount = 0;
    std::vector<float> vertices;      // x, y, z per vertex
    std::vector<float> normals;       // empty, or x, y, z per vertex
    std::array<float, 4> bound{};     // centre and radius
    uint16_t uvSets = 0;
    std::vector<float> uvw;           // uvSets sets of u, v, w per vertex
    std::vector<float> colors;        // empty, or r, g, b, a (0..1) per vertex
    std::vector<uint16_t> triangles;  // three indices each
};

struct FinMaterial {
    std::array<float, 3> ambient{}, diffuse{}, specular{}, emissive{};
    float glossiness = 0, alpha = 1;
};

// NiAlphaProperty. Blend factors as NetImmerse numbers them (6 source
// alpha, 7 one minus source alpha...).
struct FinAlpha {
    bool blend = false;
    uint32_t source = 6, destination = 7;
};

struct FinObjectInfo {
    std::string type;
    uint32_t id = 0;
    uint32_t offset = 0, size = 0; // the fields, after the class name and id
    bool read = false;             // a class the reader understands
    bool exact = false;            // its fields read to exactly its end
};

class FinFile {
public:
    Error parse(const std::vector<uint8_t>& data);

    const std::vector<FinObjectInfo>& objects() const { return objects_; }
    // Objects saved as top-level ones, in order: the model's roots.
    const std::vector<uint32_t>& roots() const { return roots_; }

    // By id.
    std::map<uint32_t, FinNode> nodes;
    std::map<uint32_t, FinShape> shapes;
    std::map<uint32_t, FinShape> coronas; // DDCorona: glows, with vertices but no triangles
    std::map<uint32_t, FinMaterial> materials;
    std::map<uint32_t, FinAlpha> alphas;
    std::map<uint32_t, std::vector<uint32_t>> textures;  // NiTextureProperty: its images' ids (frames)
    std::map<uint32_t, uint32_t> multiTextures;          // NiMultiTextureProperty: its image's id (a lightmap)
    std::map<uint32_t, std::string> images;              // NiImage: the file name
    std::map<uint32_t, uint32_t> vertexColorModes;       // NiVertexColorProperty
    std::map<uint32_t, std::array<uint8_t, 2>> zBuffers; // NiZBufferProperty: test, write

    // What kind of object an id is, or "" if none.
    std::string typeOf(uint32_t id) const;

private:
    std::vector<FinObjectInfo> objects_;
    std::vector<uint32_t> roots_;
    std::map<uint32_t, size_t> byId_;
};

} // namespace reluva::netimmerse
