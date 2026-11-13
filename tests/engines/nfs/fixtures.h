// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the Need for Speed engine's unit tests build their data with: an
// install and a CD laid out as the game's (empty files), and the game's
// formats written by hand (archives, shapes, models, tracks, sound banks,
// movies). No game data.

#pragma once

#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/fce.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/formats/hrz.h"
#include "engines/nfs/host/force_effects.h"
#include "engines/nfs/host/sound3d.h"
#include "engines/nfs/host/thrash_native.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/render/particles.h"
#include "engines/nfs/games.h"
#include "engines/nfs/formats/refpack.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/formats/viv.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "tests/support/test.h"

using namespace reluva;
using namespace reluva::nfs;
namespace stdfs = std::filesystem;

namespace reluva::nfs::testing {

inline void touch(const stdfs::path &file, const std::string &contents = "x") {
    stdfs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << contents;
}

// An install as the game's installer lays it out (mixed case), without the
// CD's music and movies.
inline void makeInstall(const stdfs::path &dir) {
    touch(dir / "nfs3.exe");
    touch(dir / "GameData/Tracks/trk000/tr00.frd");
    touch(dir / "GameData/Tracks/trk001/tr01.frd");
    touch(dir / "GameData/Tracks/Tutor/readme");
    touch(dir / "GameData/CarModel/Diab/car.viv");
    touch(dir / "GameData/CarModel/f355/car.viv");
    touch(dir / "GameData/CarModel/Empty/notes.txt");
    touch(dir / "GameData/CarModel/Traffic/0000/car.viv");
}

// The CD, in its lower-case spelling, with the music and movies.
inline void makeCd(const stdfs::path &dir) {
    touch(dir / "nfs3.exe");
    touch(dir / "gamedata/tracks/trk000/tr00.frd");
    touch(dir / "gamedata/audio/pc/alpirock.mus");
    touch(dir / "gamedata/audio/pc/alpirock.map");
    touch(dir / "gamedata/audio/pc/show1.MUS");
    touch(dir / "fedata/movies/titleav.mad");
}

using Bytes = std::vector<uint8_t>;

inline void put16(Bytes &b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

inline void put32(Bytes &b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

inline void put32be(Bytes &b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(uint8_t(v >> s));
}

inline void putText(Bytes &b, const char *s, size_t n) {
    b.insert(b.end(), s, s + n);
}

inline Bytes makeViv(const std::vector<std::pair<std::string, std::string>> &files) {
    uint32_t dirSize = 16;
    for (const auto &f : files)
        dirSize += 8 + uint32_t(f.first.size()) + 1;
    const uint32_t first = (dirSize + 3) & ~3u;
    Bytes b = {'B', 'I', 'G', 'F'};
    put32be(b, 0); // total size, filled in below
    put32be(b, uint32_t(files.size()));
    put32be(b, first);
    uint32_t offset = first;
    for (const auto &f : files) {
        put32be(b, offset);
        put32be(b, uint32_t(f.second.size()));
        putText(b, f.first.c_str(), f.first.size() + 1);
        offset += (uint32_t(f.second.size()) + 3) & ~3u;
    }
    for (const auto &f : files) {
        b.resize((b.size() + 3) & ~size_t(3));
        putText(b, f.second.data(), f.second.size());
    }
    const uint32_t total = uint32_t(b.size());
    for (int i = 0; i < 4; ++i)
        b[4 + i] = uint8_t(total >> (24 - 8 * i));
    return b;
}

// A shape block: the type, the offset to the next block (0: last), then
// width, height, centre and position.
inline void putShapeHeader(Bytes &b, uint8_t type, uint32_t next, int w, int h, int cx, int cy, int x, int y) {
    b.push_back(type);
    b.push_back(uint8_t(next));
    b.push_back(uint8_t(next >> 8));
    b.push_back(uint8_t(next >> 16));
    put16(b, unsigned(w));
    put16(b, unsigned(h));
    put16(b, unsigned(cx) & 0xffff);
    put16(b, unsigned(cy) & 0xffff);
    put16(b, unsigned(x) & 0xffff);
    put16(b, unsigned(y) & 0xffff);
}

inline Bytes makeShapeBank(const std::vector<std::pair<std::string, Bytes>> &shapes) {
    Bytes b = {'S', 'H', 'P', 'I'};
    put32(b, 0);
    put32(b, uint32_t(shapes.size()));
    putText(b, "GIMX", 4);
    uint32_t offset = 16 + 8 * uint32_t(shapes.size());
    for (const auto &s : shapes) {
        putText(b, s.first.c_str(), 4);
        put32(b, offset);
        offset += uint32_t(s.second.size());
    }
    for (const auto &s : shapes)
        b.insert(b.end(), s.second.begin(), s.second.end());
    const uint32_t total = uint32_t(b.size());
    for (int i = 0; i < 4; ++i)
        b[4 + i] = uint8_t(total >> (8 * i));
    return b;
}

inline void putFloat(Bytes &b, size_t at, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    for (int i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}

inline void putU32(Bytes &b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}

inline void putF(Bytes &b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, v);
}

inline void putPolygon(Bytes &b, std::array<uint16_t, 4> v, uint16_t texture) {
    for (uint16_t i : v)
        put16(b, i);
    put16(b, texture);
    put16(b, 0);
    b.push_back(0x10);
    b.push_back(0xf9);
}

// A one-block track (frd.md): the header, the block, its polygons and
// object layers, the extra object groups and the texture table.
inline Bytes makeTrack() {
    Bytes b(28, 0);
    put32(b, 0); // one block, stored as one less
    for (float f : {10.f, 1.f, 20.f})
        putF(b, f); // centre
    for (int i = 0; i < 12; ++i)
        putF(b, float(i)); // corners
    const uint32_t nV = 5;
    for (uint32_t v : {nV, 4u, 4u, 4u, nV, 1u})
        put32(b, v);
    for (uint32_t i = 0; i < nV; ++i)
        for (float f : {float(i), 0.f, -float(i)})
            putF(b, f);
    for (uint32_t i = 0; i < nV; ++i)
        put32(b, 0xff102030u + i);
    for (int i = 0; i < 300; ++i) {
        put16(b, i == 0 ? 0 : 0xffff);
        put16(b, 0xffff);
    }
    // Records: start, positions, polygons, road vectors, refs, polygon
    // objects, sounds, lights.
    for (uint32_t n : {7u, 1u, 1u, 1u, 0u, 0u, 1u, 1u})
        put32(b, n);
    b.insert(b.end(), 8 + 8 + 12 + 16 + 16, 0xab);
    // Polygons: list 4 (the road) has two, list 6 (lanes) one, the rest 0.
    for (int list = 0; list < 7; ++list) {
        const uint32_t n = list == 4 ? 2 : list == 6 ? 1 : 0;
        put32(b, n);
        if (!n)
            continue;
        put32(b, n);
        if (list == 4) {
            putPolygon(b, {1, 2, 3, 4}, 0);
            putPolygon(b, {1, 3, 4, 4}, 1);
        } else {
            putPolygon(b, {2, 3, 4, 1}, 1);
        }
    }
    // Object layers: the first holds a type-1 object of one polygon and a
    // type-4 entry; the rest are empty.
    put32(b, 1);
    put32(b, 2);
    put32(b, 1);
    put32(b, 1);
    putPolygon(b, {0, 0, 1, 1}, 0);
    put32(b, 4);
    for (int i = 0; i < 3; ++i)
        put32(b, 0);
    // Extra objects: 4 * 1 + 1 groups; the first holds a still object and
    // a moving one.
    put32(b, 2);
    put32(b, 4);
    put32(b, 0);
    put32(b, 0xffffffe0);
    for (float f : {100.f, 2.f, 300.f})
        putF(b, f);
    put32(b, 0);
    put32(b, 3);
    for (int i = 0; i < 3; ++i)
        for (float f : {float(i), 1.f, 0.f})
            putF(b, f);
    for (int i = 0; i < 3; ++i)
        put32(b, 0xffffffff);
    put32(b, 1);
    putPolygon(b, {0, 1, 2, 2}, 0);
    put32(b, 3); // moving: type, 8 + 12 + 8 bytes, key count, 2 bytes, keys
    b.insert(b.end(), 28, 0);
    put16(b, 2);
    put16(b, 6);
    for (int k = 0; k < 2; ++k) {
        put32(b, uint32_t(k) << 16);
        put32(b, 0);
        put32(b, 0);
        for (int q : {0, 0, k * 100, 16384})
            put16(b, unsigned(q));
    }
    put32(b, 1);
    for (float f : {0.f, 0.f, 0.f})
        putF(b, f);
    put32(b, 0xffffffff);
    put32(b, 0);
    for (int g = 1; g < 5; ++g)
        put32(b, 0);
    // Textures: width, height, u32, 8 corners, u32, lane, picture.
    put32(b, 2);
    for (int t = 0; t < 2; ++t) {
        put16(b, 64);
        put16(b, 32);
        put32(b, 0);
        for (int k = 0; k < 8; ++k)
            putF(b, k * 0.125f);
        put32(b, 0);
        b.push_back(t == 1);
        put16(b, uint16_t(40 + t));
    }
    return b;
}

// A straight road along +z, a point every 4: two lanes each side, 4
// wide (the bytes 8), walls 10 away, road strips in lanes 5-8.
inline TrackCollision straightRoad(int points, uint8_t codes = 0x44) {
    TrackCollision col;
    for (int i = 0; i < points; ++i) {
        TrackCollision::RoadPoint p;
        p.position = {0, 0, 4.0f * static_cast<float>(i)};
        p.normal = {0, 1, 0};
        p.forward = {0, 0, 1};
        p.right = {1, 0, 0};
        p.leftWall = p.rightWall = 10;
        p.extra = {0x22, 8, 8};
        p.unknown = 0x3c0u | static_cast<uint32_t>(codes) << 16;
        col.road.push_back(p);
    }
    return col;
}

// A PT header: tags (tag, length, big-endian value), then 0xFF.
inline Bytes ptHeader(const std::vector<std::pair<uint8_t, uint32_t>> &tags) {
    Bytes b = {'P', 'T', 0, 0, 0x06, 1, 25, 0xfd};
    for (auto [tag, value] : tags) {
        const int n = value > 0xffffff ? 4 : value > 0xffff ? 3 : value > 0xff ? 2 : 1;
        b.push_back(tag);
        b.push_back(uint8_t(n));
        for (int i = n - 1; i >= 0; --i)
            b.push_back(uint8_t(value >> (8 * i)));
    }
    b.push_back(0xff);
    return b;
}

// Bits as a MAD frame holds them: 16-bit little-endian words, each from its
// top bit.
struct MadBits {
    std::vector<bool> bits;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i)
            bits.push_back((v >> i) & 1);
    }
    void code(const char *s) {
        for (; *s; ++s)
            bits.push_back(*s == '1');
    }
    Bytes bytes() const {
        Bytes out;
        for (size_t w = 0; w * 16 < bits.size(); ++w) {
            unsigned v = 0;
            for (size_t i = w * 16; i < w * 16 + 16; ++i)
                v = v << 1 | (i < bits.size() && bits[i]);
            put16(out, v);
        }
        return out;
    }
};

inline Bytes madFrame(char kind, const MadBits &bits, int qscale = 1, int width = 16) {
    Bytes f = {'M', 'A', 'D', uint8_t(kind)};
    const Bytes body = bits.bytes();
    put32(f, uint32_t(24 + body.size()));
    put32(f, 0);
    put32(f, 4369067); // 66.67 ms, in 1/65536 ms
    put16(f, uint16_t(width));
    put16(f, 16);
    f.push_back(0);
    f.push_back(uint8_t(qscale));
    put16(f, 0);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

} // namespace reluva::nfs::testing
