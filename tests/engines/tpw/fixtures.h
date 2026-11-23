// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Building the Theme Park World engine's files for its tests, laid out as
// SimCoaster's are (docs/engines/tpw/games/simcoaster/formats/).

#pragma once

#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace reluva::tpw::testing {

using Bytes = std::vector<uint8_t>;
using test::toBytes;

inline void put32(Bytes &b, size_t at, uint32_t v) {
    if (b.size() < at + 4)
        b.resize(at + 4);
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// RefPack of only literals: runs of 4 (0xe0), then the rest with the end
// code. `size` is the header's, the real one unless a test says otherwise.
inline Bytes refPack(const Bytes &data, size_t size) {
    Bytes out = {0x10, 0xfb, uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size)};
    size_t at = 0;
    for (; data.size() - at >= 4; at += 4) {
        out.push_back(0xe0);
        out.insert(out.end(), data.begin() + long(at), data.begin() + long(at) + 4);
    }
    out.push_back(uint8_t(0xfc + (data.size() - at)));
    out.insert(out.end(), data.begin() + long(at), data.end());
    return out;
}

struct WadFile {
    std::string storedName; // as the archive has it: "folder\\name" or just "name"
    Bytes data;
    bool packed = false;
    size_t packedSizeField = 0; // RefPack's header size, if not the data's
};

// An archive: the header, a record per file, the data, then the names.
inline Bytes makeWad(const std::vector<WadFile> &files) {
    Bytes b = {'D', 'W', 'F', 'B'};
    put32(b, 4, 2);
    const size_t records = 0x58;
    put32(b, 0x48, uint32_t(files.size()));
    b.resize(records + files.size() * 0x28);
    std::vector<size_t> nameAt;
    for (size_t i = 0; i < files.size(); ++i) {
        const WadFile &f = files[i];
        const Bytes stored = f.packed ? refPack(f.data, f.packedSizeField ? f.packedSizeField : f.data.size()) : f.data;
        const size_t r = records + i * 0x28;
        put32(b, r, uint32_t(0x100 + i));
        put32(b, r + 12, uint32_t(b.size()));
        put32(b, r + 16, uint32_t(stored.size()));
        put32(b, r + 20, f.packed ? 4 : 0);
        put32(b, r + 24, f.packed ? uint32_t(f.data.size()) : 0);
        b.insert(b.end(), stored.begin(), stored.end());
    }
    const size_t names = b.size();
    for (size_t i = 0; i < files.size(); ++i) {
        put32(b, records + i * 0x28 + 4, uint32_t(b.size()));
        put32(b, records + i * 0x28 + 8, uint32_t(files[i].storedName.size() + 1));
        b.insert(b.end(), files[i].storedName.begin(), files[i].storedName.end());
        b.push_back(0);
    }
    put32(b, 0x4c, uint32_t(names));
    put32(b, 0x50, uint32_t(b.size() - names));
    return b;
}

inline void putFloat(Bytes &b, size_t at, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    put32(b, at, bits);
}

inline void put16(Bytes &b, size_t at, uint16_t v) {
    if (b.size() < at + 2)
        b.resize(at + 2);
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}

// A node's 0x50 bytes: its type, links and a matrix that moves it by
// (x, y, z).
inline void putNode(Bytes &b, size_t at, uint32_t type, size_t parent, size_t next, size_t child, float x, float y,
                    float z) {
    put32(b, at, type);
    put32(b, at + 4, uint32_t(parent));
    put32(b, at + 8, uint32_t(next));
    put32(b, at + 12, uint32_t(child));
    for (size_t i = 0; i < 4; ++i)
        putFloat(b, at + 16 + 20 * i, 1);
    putFloat(b, at + 64, x);
    putFloat(b, at + 68, y);
    putFloat(b, at + 72, z);
}

// A model: two textures; a helper node moved 10 along x, holding a mesh
// moved 5 up: five positions, four vertices, two faces, in two groups,
// the second untextured. With `animated`, a third texture, and the second
// animated: its frames "fr01" and "fr02", then the animation's name "fr".
inline Bytes makeModel(bool animated = false) {
    Bytes b(0xb8);
    put32(b, 0, 0x1cd15d46);
    put32(b, 4, 221);
    put32(b, 8, 203);
    const std::string name = "test.MD2";
    std::copy(name.begin(), name.end(), b.begin() + 0x18);
    put32(b, 0x30, 1);
    put16(b, 0x36, uint16_t(animated ? 3 : 2));
    const float box[6] = {10, 5, 0, 14, 6, 1};
    for (size_t i = 0; i < 6; ++i)
        putFloat(b, 0x80 + 4 * i, box[i]);
    const size_t textures = b.size();
    put32(b, 0x50, uint32_t(textures));
    put32(b, textures, 0x11);
    put32(b, textures + 8, animated ? 0x41 : 0x31);
    const std::vector<std::string> names =
        animated ? std::vector<std::string>{"one.tga", "fr01.tga", "fr02.tga", "fr.tga", "three.tga"}
                 : std::vector<std::string>{"one.tga", "two.tga"};
    const size_t entries = animated ? 3 : 2;
    if (animated)
        b.resize(textures + 24);
    const size_t namesAt = textures + 8 * entries;
    b.resize(namesAt + 20 * names.size());
    for (size_t i = 0; i < names.size(); ++i)
        std::copy(names[i].begin(), names[i].end(), b.begin() + long(namesAt + 20 * i));
    // Where the names end: the first mesh's positions, next.
    put32(b, 0x54, uint32_t(b.size()));

    // Two blocks of four positions, x then y then z; the last three unused.
    const size_t positions = b.size();
    const float pos[5][3] = {{0, 0, 0}, {4, 0, 0}, {4, 1, 1}, {0, 1, 1}, {2, 0, 0}};
    b.resize(positions + 96);
    for (size_t i = 0; i < 5; ++i)
        for (size_t k = 0; k < 3; ++k)
            putFloat(b, positions + i / 4 * 48 + k * 16 + i % 4 * 4, pos[i][k]);
    const size_t normals = b.size();
    b.resize(normals + 6 * 12);
    for (size_t i = 0; i < 6; ++i)
        putFloat(b, normals + 12 * i + 4, 1);
    // One block of four texture coordinates: their u, then v.
    const size_t uv = b.size();
    for (size_t i = 0; i < 4; ++i) {
        putFloat(b, uv + 4 * i, float(i % 2));
        putFloat(b, uv + 16 + 4 * i, float(i / 2));
    }
    const size_t map = b.size();
    const uint16_t positionOf[4] = {0, 1, 2, 3};
    for (size_t i = 0; i < 4; ++i)
        put16(b, map + 2 * i, positionOf[i]);
    const size_t faces = b.size();
    const uint16_t face[2][4] = {{4, 0, 1, 2}, {5, 0, 2, 3}};
    for (size_t i = 0; i < 2; ++i)
        for (size_t k = 0; k < 4; ++k)
            put16(b, faces + 8 * i + 2 * k, face[i][k]);
    const size_t groups = b.size();
    put32(b, groups, uint32_t(textures + 8));
    put16(b, groups + 6, 1);
    put32(b, groups + 16, 0);
    put16(b, groups + 20, 1);
    put16(b, groups + 22, 1);
    put32(b, groups + 28, 1);

    const size_t meshName = b.size();
    for (char c : std::string("box"))
        b.push_back(uint8_t(c));
    b.push_back(0);
    while (b.size() % 4)
        b.push_back(0);
    const size_t root = b.size();
    const size_t mesh = root + 0x58;
    putNode(b, root, 0x200, 0, 0, mesh, 10, 0, 0);
    putNode(b, mesh, 1, root, 0, 0, 0, 5, 0);
    const size_t m = mesh + 0x50;
    put32(b, m + 4, uint32_t(meshName));
    put16(b, m + 8, 5);
    put16(b, m + 10, 2);
    put16(b, m + 12, 2);
    put16(b, m + 14, 4);
    put32(b, m + 0x10, uint32_t(positions));
    put32(b, m + 0x14, uint32_t(normals));
    put32(b, m + 0x18, uint32_t(uv));
    put32(b, m + 0x1c, uint32_t(groups));
    put32(b, m + 0x20, uint32_t(faces));
    put32(b, m + 0x44, uint32_t(map));
    b.resize(mesh + 0xa0);
    put32(b, 0x70, uint32_t(root));
    return b;
}

// A sound's 0x28-byte header and its data: the format word is the rate,
// 16 bits, then the kind (0x20 MPEG, 0x01 stereo).
inline void putSound(Bytes &b, const std::string &name, uint8_t kind, uint16_t rate, const Bytes &data,
                     uint32_t pcmBytes) {
    const size_t at = b.size();
    put32(b, at, 0x28);
    put32(b, at + 4, uint32_t(data.size()));
    for (size_t i = 0; i < 16; ++i)
        b.push_back(i < name.size() ? uint8_t(name[i]) : 0);
    b.insert(b.end(), {uint8_t(rate), uint8_t(rate >> 8), 16, kind});
    put32(b, at + 0x1c, 0);
    put32(b, at + 0x20, pcmBytes);
    put32(b, at + 0x24, 0);
    b.insert(b.end(), data.begin(), data.end());
}

// A category's banks: their count, a record each, then their names.
inline Bytes makeBankMap(const std::vector<std::string> &names) {
    Bytes b(0x1c);
    put32(b, 0x18, uint32_t(names.size()));
    b.resize(b.size() + 11 * names.size(), 0x99);
    for (const std::string &name : names) {
        put32(b, b.size(), uint32_t(name.size() + 1));
        b.insert(b.end(), name.begin(), name.end());
        b.push_back(0);
    }
    return b;
}

struct MapSound {
    uint32_t entry; // from 1, as stored
    uint16_t upTo;
    uint32_t millis;
    uint32_t bank; // from 1
};
struct MapGroup {
    uint8_t volume;
    uint16_t weight;
    std::vector<MapSound> sounds;
    // Links to groups 1, 2... in turn, each its range of 0 to 100.
    std::vector<std::pair<uint8_t, uint8_t>> links = {};
};

// A category's effects: the table, then each effect's groups' headers,
// then each group's sounds and links.
inline Bytes makeEffectMap(const std::vector<std::pair<uint32_t, std::vector<MapGroup>>> &effects) {
    Bytes b(0x34);
    put32(b, 0x1c, uint32_t(effects.size()));
    for (const auto &[id, groups] : effects) {
        const size_t row = b.size();
        put32(b, row, id);
        put32(b, row + 4, uint32_t(groups.size()));
        put32(b, row + 12, 1000);
        put32(b, row + 16, 0);
    }
    for (const auto &[id, groups] : effects) {
        for (const MapGroup &g : groups) {
            const size_t at = b.size();
            b.resize(at + 42);
            put32(b, at, uint32_t(g.sounds.size()));
            put32(b, at + 4, uint32_t(g.links.size()));
            b[at + 12] = b[at + 13] = g.volume;
            b[at + 20] = b[at + 21] = 50;
            put16(b, at + 30, g.weight);
            put32(b, at + 38, 0xf06c0447);
        }
        for (const MapGroup &g : groups) {
            for (const MapSound &s : g.sounds) {
                const size_t at = b.size();
                put32(b, at, s.entry);
                put16(b, at + 4, s.upTo);
                put16(b, at + 6, 0);
                put32(b, at + 8, s.millis);
                put32(b, at + 12, s.bank);
            }
            for (size_t l = 0; l < g.links.size(); ++l) {
                const size_t at = b.size();
                put32(b, at, uint32_t(l + 1));
                put32(b, at + 4, uint32_t(g.links[l].first) << 16 | uint32_t(g.links[l].second) << 24);
            }
        }
    }
    return b;
}

} // namespace reluva::tpw::testing
