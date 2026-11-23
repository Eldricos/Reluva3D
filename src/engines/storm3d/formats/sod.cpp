// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/sod.h"

#include "common/bytes.h"

#include <cmath>
#include <cstring>

namespace reluva::storm3d {

namespace {

constexpr size_t kHeaderSize = 14;

// A reader of the file that remembers the first time it ran out.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _data(data) {}

    bool ok() const { return _ok; }
    size_t pos() const { return _pos; }
    size_t left() const { return _data.size() - _pos; }
    void seek(size_t pos) { _pos = pos; }

    uint8_t u8() { return need(1) ? _data[_pos++] : 0; }
    uint16_t u16() {
        if (!need(2))
            return 0;
        const uint16_t v = readLE16(&_data[_pos]);
        _pos += 2;
        return v;
    }
    float f32() {
        if (!need(4))
            return 0;
        float v = 0;
        const uint32_t bits = readLE32(&_data[_pos]);
        std::memcpy(&v, &bits, 4);
        _pos += 4;
        return v;
    }
    // A string: its length in 2 bytes, then its characters, no zero.
    std::string str() {
        const uint16_t n = u16();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char *>(&_data[_pos]), n);
        _pos += n;
        return s;
    }
    template<size_t N>
    std::array<float, N> floats() {
        std::array<float, N> out{};
        for (float &f : out)
            f = f32();
        return out;
    }
    // Whether `n` more items of `size` bytes are there, so a count read
    // from a damaged file isn't used to allocate.
    bool fits(size_t n, size_t size) { return need(n * size); }

private:
    bool need(size_t n) {
        if (_ok && n <= left())
            return true;
        _ok = false;
        return false;
    }

    const std::vector<uint8_t> &_data;
    size_t _pos = 0;
    bool _ok = true;
};

// 1.0's matrix is 4 x 4 by rows, the translation in its last row.
SodModel::Transform fromMatrix4(const std::array<float, 16> &m) {
    return {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]};
}

// Before 1.4: a texture, a lighting material, the counts, then polygons of
// 3 or 4 corners, each a count and its corners' vertex and coordinate
// indices; cut into triangles here, as a fan.
bool readPolygonMesh(Reader &r, bool withMaterial, SodModel::Mesh &mesh) {
    mesh.texture = r.str();
    SodModel::Group group;
    if (withMaterial)
        group.material = r.str();
    const uint16_t vertices = r.u16();
    const uint16_t polygons = r.u16();
    const uint16_t uvs = r.u16();
    if (!r.fits(vertices, 12) || !r.fits(uvs, 8))
        return false;
    for (uint16_t i = 0; i < vertices; ++i)
        mesh.vertices.push_back(r.floats<3>());
    for (uint16_t i = 0; i < uvs; ++i)
        mesh.uvs.push_back(r.floats<2>());
    for (uint16_t p = 0; p < polygons && r.ok(); ++p) {
        const uint16_t corners = r.u16();
        if (!r.fits(corners, 4))
            return false;
        std::vector<std::pair<uint16_t, uint16_t>> c(corners);
        for (auto &[v, uv] : c) {
            v = r.u16();
            uv = r.u16();
        }
        for (size_t k = 2; k < c.size(); ++k)
            group.faces.push_back(
                {{c[0].first, c[k - 1].first, c[k].first}, {c[0].second, c[k - 1].second, c[k].second}});
    }
    mesh.groups.push_back(std::move(group));
    return r.ok();
}

// From 1.4: (from 1.7 a drawing mode,) a texture, the counts, the
// vertices and coordinates, then groups of triangles by lighting material,
// and a flag and a list of vertices.
bool readMesh(Reader &r, int tenths, SodModel::Mesh &mesh) {
    if (tenths >= 17)
        mesh.drawMode = r.str();
    mesh.texture = r.str();
    const uint16_t vertices = r.u16();
    const uint16_t uvs = r.u16();
    const uint16_t groups = r.u16();
    if (!r.fits(vertices, 12) || !r.fits(uvs, 8))
        return false;
    for (uint16_t i = 0; i < vertices; ++i)
        mesh.vertices.push_back(r.floats<3>());
    for (uint16_t i = 0; i < uvs; ++i)
        mesh.uvs.push_back(r.floats<2>());
    for (uint16_t g = 0; g < groups && r.ok(); ++g) {
        SodModel::Group group;
        const uint16_t faces = r.u16();
        group.material = r.str();
        if (!r.fits(faces, 12))
            return false;
        group.faces.resize(faces);
        for (SodModel::Face &f : group.faces)
            for (int k = 0; k < 3; ++k) {
                f.vertex[size_t(k)] = r.u16();
                f.uv[size_t(k)] = r.u16();
            }
        mesh.groups.push_back(std::move(group));
    }
    mesh.flag = r.u8();
    const uint16_t listed = r.u16();
    if (!r.fits(listed, 2))
        return false;
    for (uint16_t i = 0; i < listed; ++i)
        mesh.vertexList.push_back(r.u16());
    return r.ok();
}

} // namespace

Error SodModel::parse(const std::vector<uint8_t> &data) {
    *this = SodModel{};
    if (data.size() < kHeaderSize)
        return Error::fail("too short for a model");
    magic.assign(reinterpret_cast<const char *>(data.data()), 10);
    if (magic != "StarTrekDB" && magic != "Storm3D_SW")
        return Error::fail("not a model: it starts '" + magic + "'");
    Reader r(data);
    r.seek(10);
    version = r.f32();
    const int tenths = int(std::lround(version * 10));
    if (tenths < 10 || tenths > 18)
        return Error::fail("version " + std::to_string(version) + " isn't one known (1.0 to 1.8)");

    if (tenths < 17) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && r.ok(); ++i) {
            TextureRef t;
            t.name = r.str();
            t.texture = r.str();
            t.value = r.f32();
            for (uint8_t &b : t.bytes)
                b = r.u8();
            textureRefs.push_back(std::move(t));
        }
    }
    const uint16_t materialCount = r.u16();
    for (uint16_t i = 0; i < materialCount && r.ok(); ++i) {
        LightingMaterial m;
        m.name = r.str();
        m.ambient = r.floats<3>();
        m.diffuse = r.floats<3>();
        m.specular = r.floats<3>();
        m.power = r.f32();
        m.mode = r.u8();
        materials.push_back(std::move(m));
    }

    const uint16_t nodeCount = r.u16();
    if (tenths == 10) {
        // The meshes, by name, then the tree that places them.
        std::vector<std::string> meshNames;
        for (uint16_t i = 0; i < nodeCount && r.ok(); ++i) {
            meshNames.push_back(r.str());
            Mesh mesh;
            if (!readPolygonMesh(r, false, mesh))
                break;
            meshes.push_back(std::move(mesh));
        }
        const uint16_t treeCount = r.u16();
        for (uint16_t i = 0; i < treeCount && r.ok(); ++i) {
            Node n;
            n.type = r.u16();
            n.name = r.str();
            n.parent = r.str();
            n.transform = fromMatrix4(r.floats<16>());
            const std::string meshName = r.str();
            for (size_t m = 0; m < meshNames.size(); ++m)
                if (!meshName.empty() && meshNames[m] == meshName)
                    n.mesh = int(m);
            nodes.push_back(std::move(n));
        }
    } else {
        for (uint16_t i = 0; i < nodeCount && r.ok(); ++i) {
            Node n;
            n.type = r.u16();
            n.name = r.str();
            n.parent = r.str();
            n.transform = r.floats<12>();
            if (n.type == kMesh) {
                Mesh mesh;
                const bool read = tenths < 14 ? readPolygonMesh(r, true, mesh) : readMesh(r, tenths, mesh);
                if (!read)
                    break;
                n.mesh = int(meshes.size());
                meshes.push_back(std::move(mesh));
            } else if (n.type == kEmitter) {
                n.emitter = r.str();
            } else if (n.type != kNull && n.type != kSprite) {
                return Error::fail("node " + n.name + " is of type " + std::to_string(n.type) + ", which isn't known");
            }
            nodes.push_back(std::move(n));
        }
    }

    if (tenths >= 14) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && r.ok(); ++i) {
            Animation a;
            a.node = r.str();
            const uint16_t keys = r.u16();
            a.length = r.f32();
            a.flags = r.u16();
            if (!r.fits(keys, 48))
                break;
            for (uint16_t k = 0; k < keys; ++k)
                a.keys.push_back(r.floats<12>());
            animations.push_back(std::move(a));
        }
    }
    if (tenths >= 16) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && r.ok(); ++i) {
            TextureAnimation t;
            t.kind = r.u8();
            t.node = r.str();
            t.animation = r.str();
            if (tenths >= 18)
                t.value = r.f32();
            textureAnimations.push_back(std::move(t));
        }
    }

    if (!r.ok())
        return Error::fail("cut short");
    if (r.left() != 0)
        return Error::fail(std::to_string(r.left()) + " bytes left over at " + std::to_string(r.pos()));
    for (const Mesh &m : meshes)
        for (const Group &g : m.groups)
            for (const Face &f : g.faces)
                for (int k = 0; k < 3; ++k)
                    if (f.vertex[size_t(k)] >= m.vertices.size() || f.uv[size_t(k)] >= m.uvs.size())
                        return Error::fail("a face names a vertex or coordinate its mesh doesn't have");
    return Error::success();
}

const SodModel::Node *SodModel::node(std::string_view name) const {
    for (const Node &n : nodes)
        if (n.name == name)
            return &n;
    return nullptr;
}

} // namespace reluva::storm3d
