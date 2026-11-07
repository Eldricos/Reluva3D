// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Alpha Team's files, built for the tests as the format documents
// (docs/engines/netimmerse/formats/) describe them: .pac archives, .FIN
// models, .DDF fonts. Shared by the unit tests and the engine test.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace reluva::netimmerse::testing {

struct PacFile {
    std::string name;
    std::string data;
    uint64_t modified = 0;
    uint32_t offsetHigh = 0; // the data offset's high half
};

inline void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
inline void set32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// An archive laid out as the retail ones: header, directory, 4 zero
// bytes, then the files in the directory's order.
inline std::vector<uint8_t> buildPac(const std::vector<PacFile>& files) {
    std::vector<uint8_t> dir;
    std::vector<size_t> offsetAt;
    for (const PacFile& f : files) {
        dir.insert(dir.end(), f.name.begin(), f.name.end());
        dir.push_back(0);
        put32(dir, f.offsetHigh);
        offsetAt.push_back(dir.size());
        put32(dir, 0); // offset, set below
        put32(dir, uint32_t(f.data.size()));
        put32(dir, uint32_t(f.modified));
        put32(dir, uint32_t(f.modified >> 32));
    }
    put32(dir, 0); // no subdirectories
    const uint32_t dataStart = uint32_t(20 + dir.size());
    uint32_t at = dataStart;
    for (size_t i = 0; i < files.size(); ++i) {
        set32(dir, offsetAt[i], at);
        at += uint32_t(files[i].data.size());
    }
    std::vector<uint8_t> out = {'P', 'A', 'C', 'K'};
    put32(out, at); // the archive's size
    put32(out, 0);
    put32(out, dataStart);
    put32(out, uint32_t(files.size()));
    out.insert(out.end(), dir.begin(), dir.end());
    for (const PacFile& f : files)
        out.insert(out.end(), f.data.begin(), f.data.end());
    return out;
}

inline std::filesystem::path writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

inline std::string asString(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

// 2000-08-21 12:00:00 UTC.
inline constexpr uint64_t kFileTime = 116444736000000000ULL + 966859200ULL * 10000000ULL;

inline void putFloat(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}

// A model written as docs/engines/netimmerse/formats/fin.md describes: a LOD node
// (roots) over a node with a collision box, holding a textured triangle.
struct FinWriter {
    std::vector<uint8_t> b;
    void str(const std::string& s) {
        put32(b, uint32_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void object(const std::string& type, uint32_t id) {
        str(type);
        put32(b, id);
    }
    void objectNet(const std::string& name, uint8_t flags = 0) {
        str(name);
        put32(b, 0); // no extra data
        b.push_back(flags);
    }
    void avObject(const std::string& name, std::array<float, 3> t, std::vector<uint32_t> props, bool box,
                  uint8_t flags = 0) {
        objectNet(name, flags);
        for (float f : t)
            putFloat(b, f);
        for (float f : {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 2.f}) // rotation, scale 2
            putFloat(b, f);
        for (int i = 0; i < 3; ++i)
            putFloat(b, 0); // velocity
        put32(b, uint32_t(props.size()));
        for (uint32_t p : props)
            put32(b, p);
        put32(b, 2);
        put32(b, box ? 0x1000 : 0);
        if (box) {
            // A union of a box and a half-space.
            put32(b, 4);
            put32(b, 2);
            put32(b, 1);
            for (int i = 0; i < 15; ++i)
                putFloat(b, 1);
            b.push_back(0);
            put32(b, 5);
            for (int i = 0; i < 4; ++i)
                putFloat(b, 0);
        }
    }
    void nodeTail(std::vector<uint32_t> children) {
        put32(b, 2);
        put32(b, 0);
        b.push_back(1);
        put32(b, uint32_t(children.size()));
        for (uint32_t c : children)
            put32(b, c);
        put32(b, 0); // effects
    }
};

inline std::vector<uint8_t> buildFin() {
    FinWriter w;
    const std::string header = "Dweezil 23\n";
    w.b.assign(header.begin(), header.end());
    w.object("NiImage", 0x500);
    w.objectNet("");
    w.str("Brick.tga");
    put32(w.b, 7);
    w.object("NiTextureProperty", 0x400);
    w.objectNet("");
    put32(w.b, 0);
    put32(w.b, 1);
    put32(w.b, 0x500);
    w.object("NiMaterialProperty", 0x300);
    w.objectNet("");
    for (int i = 0; i < 12; ++i)
        putFloat(w.b, 0.5f);
    putFloat(w.b, 0.25f);
    putFloat(w.b, 0.75f);
    w.object("NiTriShape", 0x200);
    w.avObject("Tri", {0, 0, 0}, {0x300, 0x400}, false);
    w.b.insert(w.b.end(), {3, 0}); // vertices
    put32(w.b, 0xaaaa);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        putFloat(w.b, f);
    put32(w.b, 0); // no normals
    for (float f : {0.3f, 0.3f, 0.f, 0.8f})
        putFloat(w.b, f);
    w.b.insert(w.b.end(), {1, 0, 1, 0}); // a triangle, one uv set
    put32(w.b, 0xbbbb);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        putFloat(w.b, f);
    put32(w.b, 0); // no colours
    put32(w.b, 0xcccc);
    for (int i = 0; i < 4; ++i)
        putFloat(w.b, 0);
    w.b.insert(w.b.end(), {0, 0, 1, 0, 2, 0});
    // A skin: a shape with two vertices on bone 0x100 and one with no
    // influence (as no retail file has, but allowed), no triangles.
    w.object("Ni3dsSkin", 0x600);
    w.avObject("Skin", {0, 0, 0}, {}, false);
    w.b.insert(w.b.end(), {3, 0});
    put32(w.b, 0xdddd);
    for (int i = 0; i < 9; ++i)
        putFloat(w.b, 0);
    put32(w.b, 0); // no normals
    for (int i = 0; i < 4; ++i)
        putFloat(w.b, 0);
    w.b.insert(w.b.end(), {0, 0, 0, 0}); // no triangles, no uv sets
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0);
    w.b.insert(w.b.end(), {1, 1});
    for (int v = 0; v < 3; ++v) {
        const uint16_t n = v < 2 ? 1 : 0;
        w.b.insert(w.b.end(), {uint8_t(n), 0});
        if (n) {
            putFloat(w.b, 1);
            for (float f : {float(v), 2.f, 3.f})
                putFloat(w.b, f);
            put32(w.b, 0x100);
        }
    }
    w.object("NiNode", 0x100);
    w.avObject("Holder", {5, 0, 0}, {}, true, 1); // hidden
    w.nodeTail({0x200, 0x600});
    w.str("Top Level Object");
    w.object("NiLODNode", 0x50);
    w.avObject("Lod", {0, 0, 0}, {}, false);
    w.nodeTail({0x100});
    w.b.insert(w.b.end(), 5, 0);
    put32(w.b, 1);
    w.b.insert(w.b.end(), 20 + 1, 0);
    w.str("End Of File");
    return w.b;
}

// A font with "A" and "B" on one 4x4 page: "A" 2x2 at the top-left, sitting
// on the baseline, drawn one pixel right of the pen (its edges 1 and 3).
// A font of "A" and "B" (B on page `bPage`): page 0 drawn 4 above and 1
// below the baseline, page 1 (if `pages` is 2) at twice that.
inline std::vector<uint8_t> ddfFile(int bPage, int pages = 1) {
    const size_t records = 78 + 4 * size_t(pages);
    std::vector<uint8_t> d(records + 2 * 18);
    std::memcpy(d.data(), "FNT2Arial", 9);
    auto s16 = [&](size_t at, int v) {
        d[at] = uint8_t(v);
        d[at + 1] = uint8_t(v >> 8);
    };
    const int sizes[5] = {4, 4, 'A', 'B', pages};
    for (int i = 0; i < 5; ++i)
        s16(68 + size_t(i) * 2, sizes[i]);
    for (int p = 0; p < pages; ++p)
        s16(78 + 4 * size_t(p), 4 * (p + 1)), s16(80 + 4 * size_t(p), p + 1);
    const int glyphs[2][9] = {{0, 0, 2, 0, -1, 3, 3, 0, 0}, {2, 0, 3, -1, 0, 2, 2, 0, bPage}};
    for (int g = 0; g < 2; ++g)
        for (int i = 0; i < 9; ++i)
            s16(records + size_t(g) * 18 + size_t(i) * 2, glyphs[g][i]);
    return d;
}

// An animated model: a 3ds node over a bone, a flip-book, an alpha
// animator and the game's actor data with a track for each node.
inline std::vector<uint8_t> buildAnimatedFin() {
    FinWriter w;
    const std::string header = "Dweezil 23\n";
    w.b.assign(header.begin(), header.end());
    auto floats = [&](std::initializer_list<float> fs) {
        for (float f : fs)
            putFloat(w.b, f);
    };
    w.object("NiFlipTextures", 0x700);
    w.objectNet("", 2); // the first byte of the out-of-bounds type (clamp)
    w.b.insert(w.b.end(), 3, 0);
    floats({0.25f, 160, 1920});
    put32(w.b, 0x400);
    w.object("Ni3dsAlphaAnimator", 0x710);
    w.objectNet("");
    w.b.insert(w.b.end(), {0, 0, 0, 1, 1, 0, 1});
    put32(w.b, 2); // cycle
    put32(w.b, 0);
    floats({1, 0, 0, 3200});
    put32(w.b, 0x300);
    put32(w.b, 2); // two keys of type 1: time, alpha
    put32(w.b, 1);
    floats({0, 1, 3200, 0});
    w.object("DDActorSharedData", 0x720);
    w.str("U0001");
    w.str("");
    w.str("Thing");
    w.str("");
    w.b.insert(w.b.end(), {1, 0});
    floats({0.7f, 0});
    put32(w.b, 1); // a clip, with a sound
    w.str("spin");
    floats({1, 2});
    w.b.push_back(1);
    w.str("whirr");
    // No node; looping, placed, muted beyond its maximum distance; no delay.
    w.b.insert(w.b.end(), {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0});
    floats({0.5f, 20, 1000, 300}); // volume, falloff, distances
    put32(w.b, 2); // its lists: two numbers, then none, none
    w.b.insert(w.b.end(), {0, 0, 1, 0});
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 2); // tracks
    // The node: a rotation key (type 1: time, angle, axis, quaternion, two
    // numbers), two position keys (type 1), no scale or visibility keys.
    put32(w.b, 1);
    put32(w.b, 1);
    floats({4800, 3.14159f, 0, 0, 1, 0, 0, 0, 1, 0, 0});
    put32(w.b, 2);
    put32(w.b, 1);
    floats({4800, 1, 2, 3, 9600, 4, 5, 6});
    put32(w.b, 0);
    put32(w.b, 0);
    // The bone: Euler angles (one key, type 4, 46 bytes, then a track per
    // axis), a scale key, a visibility key.
    put32(w.b, 1);
    put32(w.b, 4);
    w.b.insert(w.b.end(), 46, 0);
    put32(w.b, 1);
    put32(w.b, 2); // X: one key of type 2 (time, value, two tangents)
    floats({0, 0.5f, 0, 0});
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0); // no position keys
    put32(w.b, 1);
    put32(w.b, 2); // a scale key of type 2: time, value, tangents in and out
    floats({0, 2, 0.5f, 0.25f});
    put32(w.b, 1);
    putFloat(w.b, 4800);
    w.b.push_back(0);
    put32(w.b, 1); // the footprint: one corner
    floats({1, 2, 0.1f});
    // A point light: location, direction, switch, spot angle and
    // exponent, dimmer, three colours, attenuation distance, curve and
    // switch, type, no nodes.
    w.object("NiLight", 0x730);
    w.avObject("Lamp", {0, 0, 0}, {}, false);
    floats({0, 0, 0, 0, 0, 0});
    w.b.push_back(1);
    floats({-0.5f, 0, 3, 0.1f, 0.1f, 0.1f, 1, 0.5f, 0, 1, 0.5f, 0, 32000, 0});
    w.b.push_back(1);
    put32(w.b, 2);
    put32(w.b, 0);
    // A morph shape of one vertex: two targets, two keys.
    w.object("Ni3dsMorphShape", 0x740);
    w.avObject("Weed", {0, 0, 0}, {}, false);
    w.b.insert(w.b.end(), {1, 0});
    put32(w.b, 0xeeee);
    floats({0, 0, 0});
    put32(w.b, 0); // no normals
    floats({0, 0, 0, 1});
    w.b.insert(w.b.end(), {0, 0, 0, 0}); // no triangles or uv sets
    put32(w.b, 0);
    put32(w.b, 0);
    put32(w.b, 0);
    w.b.insert(w.b.end(), {1, 0, 0, 0, 1, 0, 0, 1}); // animation type, flags
    put32(w.b, 2);
    put32(w.b, 0);
    floats({1, 0, 0, 3200});
    w.b.insert(w.b.end(), {0, 1}); // normals updated
    put32(w.b, 2); // targets
    put32(w.b, 2); // keys
    floats({0, 0, 0, 1}); // its bounding sphere
    put32(w.b, 5); // NiBaryMorphKey
    for (float t : {0.f, 3200.f}) {
        floats({t, 1, 0.5f, 0, 0, -1, -1});
        put32(w.b, 2);
        floats({1, t > 0 ? 1.f : 0.f, 0, 0.5f, 0, 0.5f}); // weights, in, out
    }
    floats({1, 2, 3, 0, 0, 1}); // the targets' vertices
    w.object("DDUnit", 0x750);
    w.avObject("Unit", {0, 0, 0}, {}, false);
    w.nodeTail({});
    put32(w.b, 0x720);
    w.object("Ni3dsBone", 0x20);
    w.avObject("Bone", {0, 0, 0}, {}, false);
    w.nodeTail({});
    w.b.insert(w.b.end(), 48, 0);
    w.str("Top Level Object");
    w.object("Ni3dsAnimationNode", 0x10);
    w.avObject("Root", {0, 0, 0}, {}, false);
    w.nodeTail({0x20});
    w.b.insert(w.b.end(), 48, 0);
    w.str("End Of File");
    return w.b;
}

} // namespace reluva::netimmerse::testing
