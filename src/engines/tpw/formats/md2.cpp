// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 1,980 models', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/md2.md.

#include "engines/tpw/formats/md2.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace reluva::tpw {

namespace {

constexpr uint32_t kMagic = 0x1cd15d46;
constexpr uint32_t kVersion = 221;
constexpr uint32_t kAnimationFlag = 0x20;
constexpr size_t kHeaderSize = 0xb8;
// A mesh's type is 1 in its low byte: 0x401 meshes are drawn like the rest.
constexpr uint32_t kMeshType = 1;
// A node's type, links and matrix come first; a mesh's own fields follow.
constexpr size_t kNodeSize = 0x50;
constexpr size_t kMeshSize = 0xa0;
// The other nodes' records: in all the game's models they lie in an array
// of these, as the meshes lie in one of kMeshSize.
constexpr size_t kOtherSize = 0x58;
constexpr size_t kTextureNameSize = 20;
// Nodes deeper than this are taken for a loop the visited set missed.
constexpr int kMaxDepth = 256;

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _d(data) {}

    bool has(size_t at, size_t size) const { return at <= _d.size() && size <= _d.size() - at; }
    uint16_t u16(size_t at) const { return uint16_t(_d[at] | _d[at + 1] << 8); }
    uint32_t u32(size_t at) const { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; }
    float f32(size_t at) const {
        const uint32_t bits = u32(at);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    // Up to `size` bytes, to the first zero.
    std::string text(size_t at, size_t size) const {
        std::string out;
        for (size_t i = at; i < at + size && i < _d.size() && _d[i]; ++i)
            out += char(_d[i]);
        return out;
    }

private:
    const std::vector<uint8_t> &_d;
};

Error readMesh(const Reader &r, size_t node, size_t textureTable, size_t textures, Model::Mesh &mesh) {
    const size_t m = node + kNodeSize;
    const size_t nameAt = r.u32(m + 4);
    mesh.name = r.text(nameAt, 64);
    const size_t positions = r.u16(m + 8);
    const size_t groups = r.u16(m + 10);
    const size_t faces = r.u16(m + 12);
    const size_t vertices = r.u16(m + 14);
    const size_t positionsAt = r.u32(m + 0x10);
    const size_t normalsAt = r.u32(m + 0x14);
    const size_t uvAt = r.u32(m + 0x18);
    const size_t groupsAt = r.u32(m + 0x1c);
    const size_t facesAt = r.u32(m + 0x20);
    const size_t mapAt = r.u32(m + 0x44);
    auto fail = [&](const std::string &why) { return Error::fail("mesh " + mesh.name + ": " + why); };

    // Four positions to a block of 48 bytes: their x, then y, then z, a
    // block padded with zeros when they don't come out even.
    if (!r.has(positionsAt, (positions + 3) / 4 * 48))
        return fail("its positions run past the end");
    mesh.positions.resize(positions);
    for (size_t i = 0; i < positions; ++i) {
        const size_t at = positionsAt + i / 4 * 48 + i % 4 * 4;
        mesh.positions[i] = {r.f32(at), r.f32(at + 16), r.f32(at + 32)};
    }
    // Texture coordinates four to a block of 32 bytes, as the positions:
    // their u, then v.
    if (!r.has(mapAt, vertices * 2) || !r.has(uvAt, (vertices + 3) / 4 * 32) || !r.has(facesAt, faces * 8) ||
        !r.has(groupsAt, groups * 16))
        return fail("its tables run past the end");
    mesh.positionOf.resize(vertices);
    mesh.uv.resize(vertices);
    for (size_t i = 0; i < vertices; ++i) {
        mesh.positionOf[i] = r.u16(mapAt + 2 * i);
        if (mesh.positionOf[i] >= positions)
            return fail("a vertex's position is out of range");
        const size_t at = uvAt + i / 4 * 32 + i % 4 * 4;
        mesh.uv[i] = {r.f32(at), r.f32(at + 16)};
    }
    mesh.faces.resize(faces);
    size_t normals = vertices + faces;
    for (size_t i = 0; i < faces; ++i) {
        Model::Face &f = mesh.faces[i];
        const size_t at = facesAt + 8 * i;
        f.normal = r.u16(at);
        for (size_t k = 0; k < 3; ++k) {
            f.vertex[k] = r.u16(at + 2 + 2 * k);
            if (f.vertex[k] >= vertices)
                return fail("a face's vertex is out of range");
        }
        normals = std::max(normals, size_t(f.normal) + 1);
    }
    // 27 of the game's meshes number their faces' normals past the end of
    // the table, which then runs into what follows it.
    if (r.has(normalsAt, normals * 12)) {
        mesh.normals.resize(normals);
        for (size_t i = 0; i < normals; ++i)
            mesh.normals[i] = {r.f32(normalsAt + 12 * i), r.f32(normalsAt + 12 * i + 4), r.f32(normalsAt + 12 * i + 8)};
    }
    mesh.groups.resize(groups);
    for (size_t i = 0; i < groups; ++i) {
        Model::Group &g = mesh.groups[i];
        const size_t at = groupsAt + 16 * i;
        // A group names its texture by where its entry is in the table, or
        // has none: 0.
        const size_t entry = r.u32(at);
        if (entry == 0)
            g.texture = Model::Group::kNoTexture;
        else if (entry < textureTable || (entry - textureTable) % 8 || (entry - textureTable) / 8 >= textures)
            return fail("a group's texture isn't in the table");
        else
            g.texture = (entry - textureTable) / 8;
        g.firstFace = r.u16(at + 4);
        g.faceCount = r.u16(at + 6);
        g.flags = r.u32(at + 12);
        if (g.firstFace + g.faceCount > faces)
            return fail("a group's faces are out of range");
    }
    for (size_t i = 0; i < 6; ++i)
        mesh.bounds[i] = r.f32(m + 0x28 + 4 * i);
    return Error::success();
}

} // namespace

Error Model::parse(const std::vector<uint8_t> &data) {
    *this = {};
    const Reader r(data);
    if (!r.has(0, kHeaderSize) || r.u32(0) != kMagic)
        return Error::fail("not a model (.MD2)");
    if (r.u32(4) != kVersion)
        return Error::fail("a model of version " + std::to_string(r.u32(4)) + ", not " + std::to_string(kVersion));
    _name = r.text(0x18, 20);
    _flags = r.u32(0x30);
    for (size_t i = 0; i < 6; ++i)
        _bounds[i] = r.f32(0x80 + 4 * i);
    const size_t textures = r.u16(0x36);
    const size_t textureTable = r.u32(0x50);
    const size_t root = r.u32(0x70);
    if (textureTable == 0) {
        _kind = _flags & kAnimationFlag ? Kind::kAnimation : Kind::kSummary;
        return Error::success();
    }
    // Each texture's 8-byte entry, then all their names.
    if (!r.has(textureTable, textures * (8 + kTextureNameSize)))
        return Error::fail("the textures run past the end");
    _textures.resize(textures);
    for (size_t i = 0; i < textures; ++i) {
        _textures[i].flags = r.u32(textureTable + 8 * i);
        _textures[i].name = r.text(textureTable + 8 * textures + kTextureNameSize * i, kTextureNameSize);
    }

    std::set<size_t> visited;
    std::vector<size_t> offsets;
    // Each node's matrix places it in its parent's space.
    auto walk = [&](auto &self, size_t node, const gfx::Mat4 &parent, int parentIndex, int depth) -> Error {
        for (; node; node = r.u32(node + 8)) {
            if (depth > kMaxDepth || !visited.insert(node).second)
                return Error::fail("the nodes loop");
            if (!r.has(node, kNodeSize))
                return Error::fail("a node runs past the end");
            gfx::Mat4 local;
            for (size_t i = 0; i < 16; ++i)
                local[i] = r.f32(node + 16 + 4 * i);
            const gfx::Mat4 transform = gfx::multiply(parent, local);
            const int index = int(_nodes.size());
            _nodes.push_back({r.u32(node), parentIndex, local, -1, {}});
            offsets.push_back(node);
            // Every node's name is where the word after its matrix says.
            if (r.has(node, kNodeSize + 8))
                _nodes.back().name = r.text(r.u32(node + kNodeSize + 4), 64);
            if ((r.u32(node) & 0xff) == kMeshType) {
                if (!r.has(node, kMeshSize))
                    return Error::fail("a mesh runs past the end");
                Mesh mesh;
                mesh.transform = transform;
                mesh.node = size_t(index);
                _nodes.back().mesh = int(_meshes.size());
                if (Error err = readMesh(r, node, textureTable, textures, mesh))
                    return err;
                _meshes.push_back(std::move(mesh));
            } else {
                ++_otherNodes;
            }
            if (Error err = self(self, r.u32(node + 12), transform, index, depth + 1))
                return err;
        }
        return Error::success();
    };
    if (Error err = walk(walk, root, gfx::identity(), -1, 0))
        return err;
    numberForAnimations(offsets);
    return Error::success();
}

// An animation numbers the nodes by their records' places in the file: the
// meshes' array, then the others'. Nodes out of the tree keep their numbers.
void Model::numberForAnimations(const std::vector<size_t> &offsets) {
    size_t firstMesh = SIZE_MAX;
    size_t lastMesh = 0;
    size_t firstOther = SIZE_MAX;
    for (size_t i = 0; i < _nodes.size(); ++i) {
        if (_nodes[i].mesh >= 0) {
            firstMesh = std::min(firstMesh, offsets[i]);
            lastMesh = std::max(lastMesh, offsets[i]);
        } else {
            firstOther = std::min(firstOther, offsets[i]);
        }
    }
    const size_t meshSlots = _meshes.empty() ? 0 : (lastMesh - firstMesh) / kMeshSize + 1;
    for (size_t i = 0; i < _nodes.size(); ++i) {
        const size_t number = _nodes[i].mesh >= 0 ? (offsets[i] - firstMesh) / kMeshSize
                                                  : meshSlots + (offsets[i] - firstOther) / kOtherSize;
        if (number >= _animationOrder.size())
            _animationOrder.resize(number + 1, kNoNode);
        if (_animationOrder[number] == kNoNode)
            _animationOrder[number] = i;
    }
}

} // namespace reluva::tpw
