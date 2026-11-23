// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A model (`.MD2`), as SimCoaster's `Data/levels/arabian/features/atmdesk/atmdesk.MD2`:
 * a tree of nodes, some of them meshes of textured triangles. Bullfrog's own
 * format, not Quake II's. See docs/engines/tpw/games/simcoaster/formats/md2.md.
 */
class Model {
public:
    /** What the file holds; only a full model has meshes. */
    enum class Kind : uint8_t {
        kModel,
        kAnimation, // keyframes for a model of the same counts: not read yet
        kSummary,   // the header alone, with its box
    };

    /** A texture the meshes name, by its file name as stored (".tga"). */
    struct Texture {
        std::string name;   // an animated texture's first frame's
        uint32_t flags = 0; // 0x40 animated; the rest not worked out: 1 and 3 most often, with 0x10 and 0x20 on some
        /** An animated texture's frames, its name among them, and the animation's own name after them. */
        std::vector<std::string> frames;
        std::string animation;
    };

    /** A triangle. */
    struct Face {
        std::array<uint16_t, 3> vertex{}; // into the mesh's vertices
        uint16_t normal = 0;              // into the mesh's normals, past its vertices' own
    };

    /** A run of faces drawn with one texture. */
    struct Group {
        static constexpr size_t kNoTexture = SIZE_MAX;
        size_t texture = 0; // into textures(), or kNoTexture
        size_t firstFace = 0;
        size_t faceCount = 0;
        uint32_t flags = 0; // 1 for some groups of see-through textures: not worked out
    };

    /**
     * A mesh. Each vertex has its own texture coordinates and normal, and
     * shares a position with the others at the same point.
     */
    struct Mesh {
        std::string name;
        /** Its node's matrix composed with its parents': to the model's space. */
        gfx::Mat4 transform{};
        size_t node = 0; // its node, in nodes()
        std::vector<gfx::Vec3> positions;
        std::vector<uint16_t> positionOf; // each vertex's position
        std::vector<std::array<float, 2>> uv;
        /** The vertices' normals, then the faces'; empty where they run past the end. */
        std::vector<gfx::Vec3> normals;
        std::vector<Face> faces;
        std::vector<Group> groups;
        std::array<float, 6> bounds{}; // the positions' box, before the transform: least x, y, z, then most
    };

    /**
     * A node of the model's tree: a mesh, a helper or another kind. An
     * animation names nodes by their place in nodes().
     */
    struct Node {
        uint32_t type = 0; // 1 or 0x401 a mesh, 0x200 a helper; the others not worked out
        int parent = -1;   // in nodes(), or -1 for a root
        gfx::Mat4 local{}; // in its parent's space
        int mesh = -1;     // in meshes(), or -1
        std::string name;  // a mesh's or a helper's: "Camera01"
    };

    /**
     * Reads the model. Fails without the magic number, for a version other
     * than the game's (221), or on a table or index past its end.
     */
    Error parse(const std::vector<uint8_t> &data);

    Kind kind() const { return _kind; }
    /** The name in its header, as "atmdesk.MD2". */
    const std::string &name() const { return _name; }
    /** The whole model's box, in its space, as the header gives it: least x, y, z, then most. */
    const std::array<float, 6> &bounds() const { return _bounds; }
    /** The header's flags word: 0x20 for an animation; the others not worked out. */
    uint32_t flags() const { return _flags; }
    const std::vector<Texture> &textures() const { return _textures; }
    const std::vector<Mesh> &meshes() const { return _meshes; }
    /** How many of the tree's nodes aren't meshes: helpers and others, not read. */
    size_t otherNodes() const { return _otherNodes; }
    /** Every node of the tree, each before its children, children before the next of their parent's. */
    const std::vector<Node> &nodes() const { return _nodes; }
    /**
     * The nodes as an animation numbers them (indices into nodes()): the
     * meshes' first, then the others', each by its record's place in the
     * file; kNoNode for a number no node of the tree has.
     */
    const std::vector<size_t> &animationOrder() const { return _animationOrder; }

    static constexpr size_t kNoNode = SIZE_MAX;

private:
    void numberForAnimations(const std::vector<size_t> &offsets);

    Kind _kind = Kind::kModel;
    std::string _name;
    std::array<float, 6> _bounds{};
    uint32_t _flags = 0;
    std::vector<Texture> _textures;
    std::vector<Mesh> _meshes;
    std::vector<Node> _nodes;
    std::vector<size_t> _animationOrder;
    size_t _otherNodes = 0;
};

} // namespace reluva::tpw
