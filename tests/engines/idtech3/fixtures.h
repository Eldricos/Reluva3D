// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small game files built in code for the id Tech 3 tests (a map, a model
// and a map for collision) and helpers, shared by the unit tests and the
// engine test.

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace reluva::idtech3::fixtures {

// A tiny map: one triangle in one leaf of a two-leaf tree, one brush,
// one lightmap and vis data for two clusters.
inline std::vector<uint8_t> buildBsp(int version = 46, int badIndex = -1, int32_t lightmap = 0, int32_t fog = -1,
                                     int32_t type = 1) {
    std::vector<uint8_t> lumps[17];
    auto i32 = [](std::vector<uint8_t>& v, int32_t x) {
        for (int i = 0; i < 4; ++i)
            v.push_back(uint8_t(uint32_t(x) >> (8 * i)));
    };
    auto f32 = [&](std::vector<uint8_t>& v, float f) {
        int32_t x;
        std::memcpy(&x, &f, 4);
        i32(v, x);
    };
    auto name = [](std::vector<uint8_t>& v, const char* s) {
        std::vector<uint8_t> n(64, 0);
        std::memcpy(n.data(), s, std::strlen(s));
        v.insert(v.end(), n.begin(), n.end());
    };
    const std::string ents = "{ \"classname\" \"worldspawn\" }";
    lumps[0].assign(ents.begin(), ents.end());
    lumps[0].push_back(0);
    name(lumps[1], "textures/borg/floor");
    i32(lumps[1], 0); i32(lumps[1], 1);
    f32(lumps[2], 0); f32(lumps[2], 0); f32(lumps[2], 1); f32(lumps[2], 16);
    i32(lumps[3], 0); i32(lumps[3], -1); i32(lumps[3], -2);
    for (int k = 0; k < 6; ++k)
        i32(lumps[3], k < 3 ? -64 : 64);
    for (int leaf = 0; leaf < 2; ++leaf) {
        i32(lumps[4], leaf); i32(lumps[4], 0);
        for (int k = 0; k < 6; ++k)
            i32(lumps[4], k < 3 ? -64 : 64);
        i32(lumps[4], 0); i32(lumps[4], leaf); i32(lumps[4], 0); i32(lumps[4], leaf);
    }
    i32(lumps[5], 0);
    i32(lumps[6], 0);
    for (float f : {-64.f, -64.f, -64.f, 64.f, 64.f, 64.f})
        f32(lumps[7], f);
    i32(lumps[7], 0); i32(lumps[7], 1); i32(lumps[7], 0); i32(lumps[7], 1);
    i32(lumps[8], 0); i32(lumps[8], 1); i32(lumps[8], 0);
    i32(lumps[9], 0); i32(lumps[9], 0);
    for (int v = 0; v < 3; ++v) {
        f32(lumps[10], v == 1 ? 32.f : 0.f); f32(lumps[10], v == 2 ? 32.f : 0.f); f32(lumps[10], 16);
        for (int k = 0; k < 4; ++k)
            f32(lumps[10], 0.5f);
        f32(lumps[10], 0); f32(lumps[10], 0); f32(lumps[10], 1);
        for (uint8_t c : {255, 128, 64, 255})
            lumps[10].push_back(c);
    }
    i32(lumps[11], 0); i32(lumps[11], 1); i32(lumps[11], badIndex >= 0 ? badIndex : 2);
    // Surface: shader 0, no fog, planar, vertices 0-2, indices 0-2, lightmap 0.
    for (int32_t x : {0, fog, type, 0, 3, 0, 3, lightmap, 0, 0, 16, 16})
        i32(lumps[13], x);
    for (int k = 0; k < 12; ++k)
        f32(lumps[13], k == 11 ? 1.f : 0.f);
    i32(lumps[13], 0); i32(lumps[13], 0);
    lumps[14].assign(kLightmapSize * kLightmapSize * 3, 7);
    i32(lumps[16], 2); i32(lumps[16], 1);
    lumps[16].push_back(0x01); // cluster 0 sees itself
    lumps[16].push_back(0x03); // cluster 1 sees both

    std::vector<uint8_t> out = {'I', 'B', 'S', 'P'};
    i32(out, version);
    int32_t offset = 8 + 17 * 8;
    for (const auto& l : lumps) {
        i32(out, offset);
        i32(out, int32_t(l.size()));
        offset += int32_t(l.size());
    }
    for (const auto& l : lumps)
        out.insert(out.end(), l.begin(), l.end());
    return out;
}

// A two-frame MD3 with one tag and one triangle; frame 1 is frame 0
// moved up 64 units.
inline std::vector<uint8_t> buildMd3(int badIndex = -1) {
    std::vector<uint8_t> out;
    auto i32 = [&](int32_t x) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(uint32_t(x) >> (8 * i)));
    };
    auto i16 = [&](int16_t x) {
        out.push_back(uint8_t(uint16_t(x)));
        out.push_back(uint8_t(uint16_t(x) >> 8));
    };
    auto f32 = [&](float f) {
        int32_t x;
        std::memcpy(&x, &f, 4);
        i32(x);
    };
    auto name = [&](const char* s, size_t n) {
        std::vector<uint8_t> b(n, 0);
        std::memcpy(b.data(), s, std::strlen(s));
        out.insert(out.end(), b.begin(), b.end());
    };
    const int frames = 2, tags = 1;
    const int ofsFrames = 108, ofsTags = ofsFrames + 56 * frames, ofsSurf = ofsTags + 112 * frames * tags;
    out.insert(out.end(), {'I', 'D', 'P', '3'});
    i32(15);
    name("models/test/box.md3", 64);
    i32(0); i32(frames); i32(tags); i32(1); i32(0);
    i32(ofsFrames); i32(ofsTags); i32(ofsSurf); i32(0);
    for (int f = 0; f < frames; ++f) {
        for (float v : {-8.f, -8.f, 0.f + 64 * f, 8.f, 8.f, 16.f + 64 * f, 0.f, 0.f, 0.f, 12.f})
            f32(v);
        name(f ? "up" : "rest", 16);
    }
    for (int f = 0; f < frames; ++f) {
        name("tag_top", 64);
        for (float v : {0.f, 0.f, 16.f + 64 * f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f})
            f32(v);
    }
    // Surface: header (108), shader (68), triangle (12), st (3 * 8), xyz (2 * 3 * 8).
    const int ofsShaders = 108, ofsTris = ofsShaders + 68, ofsSt = ofsTris + 12, ofsXyz = ofsSt + 24,
              ofsEnd = ofsXyz + 48;
    out.insert(out.end(), {'I', 'D', 'P', '3'});
    name("box", 64);
    i32(0); i32(frames); i32(1); i32(3); i32(1);
    i32(ofsTris); i32(ofsShaders); i32(ofsSt); i32(ofsXyz); i32(ofsEnd);
    name("models/test/box", 64);
    i32(0);
    i32(0); i32(1); i32(badIndex >= 0 ? badIndex : 2);
    for (float v : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f})
        f32(v);
    for (int f = 0; f < frames; ++f)
        for (int v = 0; v < 3; ++v) {
            i16(int16_t(v == 1 ? 64 * 8 : 0)); // x 8 for vertex 1 (1/64 units)
            i16(int16_t(v == 2 ? 64 * 8 : 0));
            i16(int16_t(64 * 64 * f));
            i16(int16_t(v == 0 ? 0 : (64 << 8) | 64)); // straight up; lat 64 lng 64 points along +y
        }
    return out;
}

// A small map for collision: a floor in the world (x and y -512..512, z
// -64..0) and one brush model, *1, a door 16 x 128 x 128 standing on its
// own origin.
inline std::shared_ptr<Bsp> collisionMap() {
    auto bsp = std::make_shared<Bsp>();
    bsp->shaders.push_back({"textures/test/solid", 0, contents::Solid});
    auto addBox = [&](Vec3f mins, Vec3f maxs) {
        const int first = int(bsp->brushSides.size());
        for (int axis = 0; axis < 3; ++axis) {
            BspPlane hi, lo;
            hi.normal[size_t(axis)] = 1;
            hi.dist = maxs[size_t(axis)];
            lo.normal[size_t(axis)] = -1;
            lo.dist = -mins[size_t(axis)];
            bsp->brushSides.push_back({int(bsp->planes.size()), 0});
            bsp->planes.push_back(hi);
            bsp->brushSides.push_back({int(bsp->planes.size()), 0});
            bsp->planes.push_back(lo);
        }
        bsp->brushes.push_back({first, 6, 0});
    };
    addBox({-512, -512, -64}, {512, 512, 0});
    addBox({-8, -64, 0}, {8, 64, 128});
    // One node, far below everything: the floor's leaf in front, an empty one behind.
    BspPlane split;
    split.normal = {0, 0, 1};
    split.dist = -10000;
    bsp->planes.push_back(split);
    BspNode node;
    node.plane = int(bsp->planes.size()) - 1;
    node.children = {-1, -2};
    bsp->nodes.push_back(node);
    BspLeaf floorLeaf;
    floorLeaf.numLeafBrushes = 1;
    bsp->leafs.push_back(floorLeaf);
    BspLeaf empty;
    empty.cluster = -1;
    bsp->leafs.push_back(empty);
    bsp->leafBrushes.push_back(0);
    BspModel world;
    world.mins = {-512, -512, -64};
    world.maxs = {512, 512, 0};
    world.numBrushes = 1;
    BspModel door;
    door.mins = {-8, -64, 0};
    door.maxs = {8, 64, 128};
    door.firstBrush = 1;
    door.numBrushes = 1;
    bsp->models = {world, door};
    return bsp;
}

// Whether two floats are within a thousandth.
inline bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

} // namespace reluva::idtech3::fixtures
