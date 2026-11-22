// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/model.h"

#include "common/bytes.h"

#include <cstring>

namespace reluva::sfc {

namespace {

constexpr size_t kHeader = 44; // to the first chunk, the MDLS tag and length included
constexpr uint32_t kMaxLevels = 16;
constexpr uint32_t kTextured = 0x20; // a material with this flag names a texture and an illumination map

struct Chunk {
    uint32_t tag = 0;
    size_t at = 0; // its body
    size_t size = 0;
};

float readFloat(const uint8_t *p) {
    const uint32_t bits = readLE32(p);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// A run of chunks: a tag, a 32-bit length, the body, padded to four bytes.
Error readChunks(const std::vector<uint8_t> &data, size_t at, size_t end, std::vector<Chunk> &out) {
    while (at < end) {
        if (end - at < 8)
            return Error::fail(".mod: a chunk's header at " + std::to_string(at) + " is past its end");
        Chunk c{readLE32(data.data() + at), at + 8, readLE32(data.data() + at + 4)};
        if (c.size > end - c.at)
            return Error::fail(".mod: the chunk at " + std::to_string(at) + " runs past its end");
        out.push_back(c);
        at = c.at + ((c.size + 3) & ~size_t(3));
    }
    return Error::success();
}

std::string stringAt(const std::vector<uint8_t> &strs, uint32_t offset) {
    if (offset >= strs.size())
        return {};
    const auto *s = reinterpret_cast<const char *>(strs.data() + offset);
    return {s, strnlen(s, strs.size() - offset)};
}

} // namespace

Error Model::read(const std::vector<uint8_t> &data) {
    _points.clear();
    _materials.clear();
    _meshes.clear();
    if (data.size() < kHeader || readLE32(data.data()) != tagLE('M', 'D', 'L', 'S'))
        return Error::fail(".mod: not an MDLS file");
    if (readLE32(data.data() + 4) != data.size() - 8)
        return Error::fail(".mod: its MDLS length isn't the file's");
    _version = readLE32(data.data() + 8);
    _radius = readFloat(data.data() + 12);
    const uint32_t levels = readLE32(data.data() + 16);
    for (size_t i = 0; i < _settings.size(); ++i)
        _settings[i] = readFloat(data.data() + 20 + 4 * i);
    if (levels > kMaxLevels)
        return Error::fail(".mod: " + std::to_string(levels) + " levels of detail");

    std::vector<Chunk> chunks;
    if (Error err = readChunks(data, kHeader, data.size(), chunks))
        return err;
    std::vector<uint8_t> strs;
    for (const Chunk &c : chunks)
        if (c.tag == tagLE('S', 'T', 'R', 'S'))
            strs.assign(data.begin() + static_cast<ptrdiff_t>(c.at),
                        data.begin() + static_cast<ptrdiff_t>(c.at + c.size));

    for (const Chunk &c : chunks) {
        const uint8_t *p = data.data() + c.at;
        if (c.tag == tagLE('P', 'N', 'T', 'S')) {
            // x, y, z, and the name's offset in STRS
            if (c.size % 16)
                return Error::fail(".mod: PNTS isn't whole points");
            for (size_t i = 0; i < c.size; i += 16)
                _points.push_back({{readFloat(p + i), readFloat(p + i + 4), readFloat(p + i + 8)},
                                   stringAt(strs, readLE32(p + i + 12))});
        } else if (c.tag == tagLE('M', 'A', 'T', 'S')) {
            for (size_t i = 0; i < c.size;) {
                if (c.size - i < 4)
                    return Error::fail(".mod: MATS ends inside a material");
                Material m;
                m.flags = readLE32(p + i);
                // Six words follow the flags; eight with a texture: colour,
                // four unread words, the texture's name, one unread word,
                // the illumination map's name.
                const size_t words = (m.flags & kTextured) ? 8 : 6;
                if (c.size - i - 4 < 4 * words)
                    return Error::fail(".mod: MATS ends inside a material");
                m.color = readLE32(p + i + 4);
                if (m.flags & kTextured) {
                    m.texture = stringAt(strs, readLE32(p + i + 4 + 20));
                    m.illumination = stringAt(strs, readLE32(p + i + 4 + 28));
                }
                _materials.push_back(std::move(m));
                i += 4 + 4 * words;
            }
        } else if (c.tag == tagLE('M', 'O', 'D', '0')) {
            if (c.size < 8)
                return Error::fail(".mod: MOD0 shorter than its counts");
            const uint32_t vertices = readLE32(p);
            const uint32_t triangles = readLE32(p + 4);
            std::vector<Chunk> inner;
            if (Error err = readChunks(data, c.at + 8, c.at + c.size, inner))
                return err;
            Mesh mesh;
            for (const Chunk &ic : inner) {
                const uint8_t *q = data.data() + ic.at;
                if (ic.tag == tagLE('V', 'T', 'X', 'S')) {
                    if (ic.size != 12 * size_t(vertices))
                        return Error::fail(".mod: VTXS isn't the vertex count's size");
                    for (size_t i = 0; i < vertices; ++i)
                        mesh.vertices.push_back(
                            {readFloat(q + 12 * i), readFloat(q + 12 * i + 4), readFloat(q + 12 * i + 8)});
                } else if (ic.tag == tagLE('P', 'O', 'L', 'Y')) {
                    // corners, material, the face's normal, then each corner's
                    // vertex, three floats, u, v and a word that's always 0
                    if (ic.size < 20 || readLE32(q) != 3 || ic.size != 20 + 3 * 28)
                        return Error::fail(".mod: a POLY that isn't a triangle");
                    Triangle t;
                    t.material = readLE32(q + 4);
                    t.normal = {readFloat(q + 8), readFloat(q + 12), readFloat(q + 16)};
                    for (size_t k = 0; k < 3; ++k) {
                        const uint8_t *cp = q + 20 + 28 * k;
                        const uint32_t index = readLE32(cp);
                        Corner &corner = t.corners[k];
                        corner.vertex = static_cast<uint16_t>(index & 0xffff);
                        corner.piece = static_cast<uint16_t>(index >> 16);
                        corner.unknown = {readFloat(cp + 4), readFloat(cp + 8), readFloat(cp + 12)};
                        corner.u = readFloat(cp + 16);
                        corner.v = readFloat(cp + 20);
                        if (corner.vertex >= vertices)
                            return Error::fail(".mod: a triangle's vertex past the vertex count");
                    }
                    mesh.triangles.push_back(t);
                }
            }
            if (mesh.vertices.size() != vertices || mesh.triangles.size() != triangles)
                return Error::fail(".mod: MOD0's counts aren't its contents'");
            _meshes.push_back(std::move(mesh));
        }
    }
    if (_meshes.size() != levels)
        return Error::fail(".mod: " + std::to_string(_meshes.size()) + " meshes for " + std::to_string(levels) +
                           " levels of detail");
    for (const Mesh &mesh : _meshes)
        for (const Triangle &t : mesh.triangles)
            if (t.material >= _materials.size())
                return Error::fail(".mod: a triangle's material past the materials");
    return Error::success();
}

} // namespace reluva::sfc
