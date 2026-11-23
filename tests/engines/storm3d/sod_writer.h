// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Writes Storm3D models (.sod) for Storm3D's tests, in the layouts
// docs/engines/storm3d/games/armada/formats/sod.md describes, so the tests
// need none of the game's files.

namespace reluva::storm3d::testing {

/** Bytes of a model, written field by field. */
class SodWriter {
public:
    std::vector<uint8_t> bytes;

    SodWriter &header(const char *magic, float version) {
        bytes.insert(bytes.end(), magic, magic + 10);
        return f32(version);
    }
    SodWriter &u8(uint8_t v) {
        bytes.push_back(v);
        return *this;
    }
    SodWriter &u16(uint16_t v) {
        bytes.push_back(uint8_t(v));
        bytes.push_back(uint8_t(v >> 8));
        return *this;
    }
    SodWriter &f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        for (int i = 0; i < 4; ++i)
            bytes.push_back(uint8_t(bits >> (8 * i)));
        return *this;
    }
    SodWriter &str(const std::string &s) {
        u16(uint16_t(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
        return *this;
    }
    SodWriter &floats(std::initializer_list<float> v) {
        for (const float f : v)
            f32(f);
        return *this;
    }
    // A 3 x 3 matrix by rows and a translation.
    SodWriter &transform(float tx, float ty, float tz) { return floats({1, 0, 0, 0, 1, 0, 0, 0, 1, tx, ty, tz}); }
    // A lighting material: ambient, diffuse, specular, power and mode.
    SodWriter &material(const std::string &name) {
        str(name).floats({0.1f, 0.1f, 0.1f, 0.9f, 0.8f, 0.7f, 1, 1, 1, 50});
        return u8(1);
    }
    // A square's four corners, in a mesh's vertex and coordinate order.
    SodWriter &squareGeometry() {
        floats({-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0});
        return floats({0, 1, 1, 1, 1, 0, 0, 0});
    }
};

/**
 * A 1.8 model: a root, a square mesh under it moved along x, a strobe
 * light and an emitter; the mesh's node keyed twice, and its texture
 * moved by a sprite table's animation.
 */
inline std::vector<uint8_t> modelV18(const std::string &texture) {
    SodWriter w;
    w.header("Storm3D_SW", 1.8f);
    w.u16(1).material("hull");
    w.u16(4);
    w.u16(0).str("root").str("").transform(0, 0, 0);
    w.u16(1).str("square").str("root").transform(2, 0, 0);
    w.str("default").str(texture).u16(4).u16(4).u16(1).squareGeometry();
    w.u16(2).str("hull").u16(0).u16(0).u16(1).u16(1).u16(2).u16(2).u16(0).u16(0).u16(2).u16(2).u16(3).u16(3);
    w.u8(1).u16(0);
    w.u16(3).str("redstrobe1").str("square").transform(0, 1, 0);
    w.u16(12).str("steam").str("root").transform(0, 0, 1).str("steammed");
    w.u16(1).str("square").u16(2).f32(1.0f).u16(0).transform(2, 0, 0).transform(2, 1, 0);
    w.u16(1).u8(4).str("square").str("tex1x4").f32(0.5f);
    return w.bytes;
}

/** A 1.6 model: the texture table first, a mesh with no drawing mode, no texture animation's value. */
inline std::vector<uint8_t> modelV16() {
    SodWriter w;
    w.header("Storm3D_SW", 1.6f);
    w.u16(1).str("square").str("Bbase").f32(0).u8(2).u8(1).u8(2);
    w.u16(1).material("hull");
    w.u16(1);
    w.u16(1).str("square").str("").transform(0, 0, 0);
    w.str("Bbase").u16(4).u16(4).u16(1).squareGeometry();
    w.u16(2).str("hull").u16(0).u16(0).u16(1).u16(1).u16(2).u16(2).u16(0).u16(0).u16(2).u16(2).u16(3).u16(3);
    w.u8(0).u16(4).u16(0).u16(1).u16(2).u16(3);
    w.u16(1).str("square").u16(0).f32(4.0f).u16(0);
    w.u16(1).u8(4).str("square").str("tex1x4");
    return w.bytes;
}

/** A 1.2 model: one mesh of one quad, its lighting material in the mesh, nothing after the nodes. */
inline std::vector<uint8_t> modelV12() {
    SodWriter w;
    w.header("StarTrekDB", 1.2f);
    w.u16(1).str("square").str("Bbase").f32(0.75f).u8(2).u8(2).u8(1);
    w.u16(1).material("glow");
    w.u16(1);
    w.u16(1).str("square").str("").transform(0, 0, 0);
    w.str("Bbase").str("glow").u16(4).u16(1).u16(4).squareGeometry();
    w.u16(4).u16(0).u16(0).u16(1).u16(1).u16(2).u16(2).u16(3).u16(3);
    return w.bytes;
}

/** A 1.0 model: its two meshes first, then the tree placing them with 4 x 4 matrices. */
inline std::vector<uint8_t> modelV10() {
    SodWriter w;
    w.header("StarTrekDB", 1.0f);
    w.u16(0).u16(0).u16(2);
    for (const char *name : {"a", "b"}) {
        w.str(name).str("Bbase").u16(4).u16(2).u16(4).squareGeometry();
        w.u16(3).u16(0).u16(0).u16(1).u16(1).u16(2).u16(2);
        w.u16(3).u16(0).u16(0).u16(2).u16(2).u16(3).u16(3);
    }
    w.u16(2);
    w.u16(0).str("a").str("").floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}).str("a");
    w.u16(0).str("b").str("a").floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1}).str("b");
    return w.bytes;
}

} // namespace reluva::storm3d::testing
