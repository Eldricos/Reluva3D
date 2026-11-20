// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the Angel Game Engine's unit tests share: files built in the tests
// in the game's formats (.ar, .tex, .pkg, .psdl, .bai, .pathset), and a
// renderer that only keeps the textures' sizes.

#pragma once

#include "common/md5.h"
#include "engines/age/formats/aimap.h"
#include "engines/age/audio/ambient.h"
#include "engines/age/audio/music.h"
#include "engines/age/formats/ar_archive.h"
#include "engines/age/render/render_cache.h"
#include "engines/age/ui/pad_input.h"
#include "engines/age/formats/bai.h"
#include "engines/age/formats/bound.h"
#include "engines/age/game/car.h"
#include "engines/age/game/cops_robbers.h"
#include "engines/age/game/net_session.h"
#include "engines/age/render/city_mesh.h"
#include "engines/age/game/damage.h"
#include "engines/age/render/dashboard.h"
#include "engines/age/audio/engine_sound.h"
#include "engines/age/filesystem.h"
#include "engines/age/ui/front_end.h"
#include "engines/age/games.h"
#include "engines/age/game/gizmos.h"
#include "engines/age/ui/hud_map.h"
#include "engines/age/formats/instances.h"
#include "engines/age/game/lesson.h"
#include "engines/age/formats/materials.h"
#include "engines/age/game/opponent.h"
#include "engines/age/game/options.h"
#include "engines/age/formats/pathset.h"
#include "engines/age/render/ped_model.h"
#include "engines/age/formats/pkg.h"
#include "engines/age/game/props.h"
#include "engines/age/formats/psdl.h"
#include "engines/age/formats/pvs.h"
#include "engines/age/formats/race.h"
#include "engines/age/game/record.h"
#include "engines/age/formats/replay.h"
#include "engines/age/game/road_graph.h"
#include "engines/age/formats/texture.h"
#include "engines/age/game/traffic.h"
#include "engines/age/game/traffic_lights.h"
#include "engines/age/formats/tune.h"
#include "engines/age/ui/view_controls.h"
#include "engines/age/pages/viewers.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::age::test_support {

using namespace reluva;
using namespace reluva::age;
using reluva::test::toBytes;

/** π, as a float. */
constexpr float kPi() {
    return 3.14159265358979f;
}

/** A file to put in an .ar archive (buildAr). */
struct ArFile {
    std::string name;              // ending in '/' for a folder
    std::vector<uint8_t> data;     // contents
    std::vector<uint8_t> deflated; // if not empty: stored as these bytes
};

/** Writes `x` little-endian at `at`, which must be inside `v`. */
inline void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v[at + size_t(i)] = uint8_t(x >> (8 * i));
}

/** `n` rounded up to the .ar archives' 2048-byte blocks. */
inline size_t roundUp(size_t n) {
    return (n + 2047) / 2048 * 2048;
}

/**
 * An archive laid out as the retail ones are: the header in the first
 * 2048 bytes, then the directory and the names, each padded to 2048, then
 * each file's data starting on a 2048-byte boundary.
 */
inline std::vector<uint8_t> buildAr(const std::vector<ArFile> &files) {
    std::string names;
    std::vector<uint32_t> nameAt;
    for (const ArFile &f : files) {
        nameAt.push_back(uint32_t(names.size()));
        names += f.name;
        names += '\0';
    }
    const size_t dirSize = roundUp(files.size() * 16);
    const size_t namesSize = roundUp(names.size());
    std::vector<uint8_t> out(2048 + dirSize + namesSize);
    std::copy(names.begin(), names.end(), out.begin() + std::ptrdiff_t(2048 + dirSize));
    out[0] = 'D', out[1] = 'A', out[2] = 'V', out[3] = 'E';
    put32(out, 4, uint32_t(files.size()));
    put32(out, 8, uint32_t(dirSize));
    put32(out, 12, uint32_t(namesSize));
    for (size_t i = 0; i < files.size(); ++i) {
        const ArFile &f = files[i];
        const std::vector<uint8_t> &body = f.deflated.empty() ? f.data : f.deflated;
        const size_t at = 2048 + i * 16;
        put32(out, at, nameAt[i]);
        put32(out, at + 4, uint32_t(out.size()));
        put32(out, at + 8, uint32_t(f.data.size()));
        put32(out, at + 12, uint32_t(body.size()));
        out.insert(out.end(), body.begin(), body.end());
        out.resize(roundUp(out.size()));
    }
    return out;
}

/** "textures/borg/borgfloor1\n" 12 times: the text kDeflated holds. */
inline std::string repeatedText() {
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "textures/borg/borgfloor1\n";
    return text;
}

/** repeatedText(), raw-deflated by zlib. */
inline const std::vector<uint8_t> kDeflated = {0x2b, 0x49, 0xad, 0x28, 0x29, 0x2d, 0x4a, 0x2d, 0xd6, 0x4f,
                                               0xca, 0x2f, 0x4a, 0x07, 0x13, 0x69, 0x39, 0xf9, 0xf9, 0x45,
                                               0x86, 0x5c, 0x25, 0xa3, 0x12, 0xe8, 0x12, 0x00};

/** Builds a file's bytes: each call appends, little-endian, and returns the builder, so calls chain. */
struct Bytes {
    std::vector<uint8_t> v;
    /** Appends a value, or a string's bytes without a terminating 0. */
    ///@{
    Bytes &u8(uint32_t x) {
        v.push_back(uint8_t(x));
        return *this;
    }
    Bytes &u16(uint32_t x) { return u8(x).u8(x >> 8); }
    Bytes &u32(uint32_t x) { return u16(x & 0xffff).u16(x >> 16); }
    Bytes &f32(float f) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        return u32(x);
    }
    Bytes &str(const std::string &s) {
        v.insert(v.end(), s.begin(), s.end());
        return *this;
    }
    ///@}
    /** A name as models store them: a length byte counting the zero after it. */
    Bytes &name(const std::string &s) { return u8(uint32_t(s.size() + 1)).str(s).u8(0); }
    /** Appends bytes as they are. */
    Bytes &bytes(const std::vector<uint8_t> &b) {
        v.insert(v.end(), b.begin(), b.end());
        return *this;
    }
};

/** Appends a .pkg FILE section: the tag, the name, the contents' size and the contents. */
inline Bytes &fileSection(Bytes &b, const std::string &name, const std::vector<uint8_t> &contents) {
    return b.str("FILE").name(name).u32(uint32_t(contents.size())).bytes(contents);
}

/** A .tex file's header; the pixels follow it. */
inline std::vector<uint8_t> texHeader(int w, int h, int format, int levels, uint32_t flags) {
    return Bytes().u16(uint32_t(w)).u16(uint32_t(h)).u16(uint32_t(format)).u16(uint32_t(levels)).u16(1).u32(flags).v;
}

/**
 * A .pkg mesh section's contents: one group of one triangle list. The
 * vertices have normals if `fvf` has 0x10.
 */
inline std::vector<uint8_t> meshSection(uint32_t fvf, uint32_t shader, const std::vector<uint16_t> &indices,
                                        uint32_t vertexCount = 3) {
    Bytes b;
    b.u32(1).u32(vertexCount).u32(uint32_t(indices.size())).u32(1).u32(fvf);
    b.u32(1).u32(shader).u32(3).u32(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        b.f32(float(i)).f32(float(i) * 2).f32(-float(i));
        if (fvf & 0x10)
            b.f32(0).f32(1).f32(0);
        b.f32(0.5f).f32(float(i) / 4);
    }
    b.u32(uint32_t(indices.size()));
    for (uint16_t i : indices)
        b.u16(i);
    return b.v;
}

/**
 * A .psdl city of one block: 6 vertices, heights 10, 20 and 1, by default
 * two textures and two perimeter edges, then `tail` (its roads) after the
 * bounds.
 * @param attributes The block's attribute words.
 */
inline std::vector<uint8_t> buildPsdl(const std::vector<uint16_t> &attributes, uint32_t version = 2,
                                      const std::vector<std::pair<uint16_t, uint16_t>> &perimeter = {{0, 0}, {1, 5}},
                                      const std::vector<std::string> &textures = {"r2_f", "swalk_f"},
                                      const std::vector<uint8_t> &tail = {}) {
    Bytes b;
    b.str("PSD0").u32(version).u32(6);
    const float v[6][3] = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}, {0, 0.15f, -2}, {10, 0.15f, -2}};
    for (const auto &p : v)
        b.f32(p[0]).f32(p[1]).f32(p[2]);
    b.u32(3).f32(10).f32(20).f32(1);
    b.u32(uint32_t(textures.size() + 1)); // counts are one more than stored
    for (const std::string &t : textures)
        b.name(t);
    b.u32(2).u32(1); // blocks (plus one), the first road block
    b.u32(uint32_t(perimeter.size())).u32(uint32_t(attributes.size()));
    for (const auto &[vertex, beyond] : perimeter)
        b.u16(vertex).u16(beyond);
    for (uint16_t w : attributes)
        b.u16(w);
    b.u8(0).u8(4);   // flags of blocks 0 and 1
    b.u8(205).u8(0); // the second bytes
    for (float f : {-1.f, -2.f, -3.f, 4.f, 5.f, 6.f, 1.f, 2.f, 3.f, 9.f})
        b.f32(f);
    return b.bytes(tail).v;
}

/** A car of about a real car's size: four wheels of radius 0.33 and a box hull of 8 corners. */
inline CarSetup testCar() {
    CarSetup s;
    s.name = "test";
    s.wheels = {Vec3{-0.75f, 0.31f, -1.2f}, Vec3{0.75f, 0.31f, -1.2f}, Vec3{-0.75f, 0.31f, 1.4f},
                Vec3{0.75f, 0.31f, 1.4f}};
    s.wheelRadius = {0.33f, 0.33f, 0.33f, 0.33f};
    for (float x : {-0.9f, 0.9f})
        for (float z : {-1.9f, 2.0f})
            for (float y : {0.3f, 1.4f})
                s.hull.push_back({x, y, z});
    return s;
}

/**
 * A straight road for buildBai (docs/engines/age/games/mm2/formats/bai.md),
 * from one junction to another: `lanes` lanes on side 1 (one-way, first to
 * last) and `back` on side 0, `n` points along it.
 */
struct BaiRoad {
    Vec3 from;
    Vec3 to;
    // junctions at the first and the last point
    uint32_t j0;
    uint32_t j1;
    int lanes = 1;
    int back = 0;
    int n = 3;
    uint16_t control = 1; // at both ends
};

/**
 * One side of a road in a .bai file: its header and lane widths, then a
 * row of points for each lane and for the sidewalk, curb and outer edge.
 */
inline void baiSide(Bytes &b, const BaiRoad &r, int lanes, int side) {
    b.u16(uint32_t(lanes)).u16(0).u16(0).u16(1).u16(0);
    for (int i = 0; i < (lanes + 1) * r.n; ++i)
        b.f32(0);
    for (int i = 0; i < lanes; ++i)
        b.f32(2.5f + 5 * float(i));
    for (int i = 0; i < 11; ++i)
        b.u32(0xcdcdcdcd);
    const Vec3 along = r.to - r.from;
    const Vec3 across = normalized(Vec3{along[2], 0, -along[0]}); // to the left of the way
    auto row = [&](float offset, bool reversed) {
        for (int i = 0; i < r.n; ++i) {
            const float t = float(reversed ? r.n - 1 - i : i) / float(r.n - 1);
            const Vec3 p = r.from + along * t + across * offset;
            b.f32(p[0]).f32(p[1]).f32(p[2]);
        }
    };
    const float sign = side == 0 ? 1.0f : -1.0f;
    for (int i = 0; i < lanes; ++i) // side 0's lanes are stored last to first
        row(sign * (2.5f + 5 * float(i)), side == 0);
    row(sign * 9, false);  // sidewalk
    row(sign * 8, false);  // curb
    row(sign * 10, false); // outer edge
}

/** A .bai road map of the roads, joined at the junctions (indexes into `junctions`). */
inline std::vector<uint8_t> buildBai(const std::vector<BaiRoad> &roads, const std::vector<Vec3> &junctions) {
    Bytes b;
    b.str("CAI1").u16(uint32_t(junctions.size())).u16(uint32_t(roads.size()));
    for (size_t k = 0; k < roads.size(); ++k) {
        const BaiRoad &r = roads[k];
        b.u16(uint32_t(k)).u16(uint32_t(r.n)).u16(0).u16(1).u16(uint32_t(100 + k)).f32(10).f32(15);
        baiSide(b, r, r.back, 0);
        baiSide(b, r, r.lanes, 1);
        for (int i = 0; i < r.n; ++i)
            b.f32(0);
        for (int i = 0; i < r.n; ++i) {
            const Vec3 p = r.from + (r.to - r.from) * (float(i) / float(r.n - 1));
            b.f32(p[0]).f32(p[1]).f32(p[2]);
        }
        for (int i = 0; i < 12 * r.n; ++i)
            b.f32(0);
        for (int e = 0; e < 2; ++e) { // the last point's junction first
            const uint32_t j = e == 0 ? r.j1 : r.j0;
            b.u32(j).u16(0xcdcd).u16(r.control).u16(0);
            uint32_t slot = 0;
            for (size_t o = 0; o < k; ++o)
                slot += roads[o].j0 == j || roads[o].j1 == j;
            b.u32(slot);
            // On the curb on the right of the way in, and a metre across.
            const Vec3 in = normalized(e == 0 ? r.to - r.from : r.from - r.to);
            const Vec3 right{-in[2], 0, in[0]};
            const Vec3 a = (e == 0 ? r.to : r.from) + right * 8.0f;
            const Vec3 bb = a - right;
            b.f32(a[0]).f32(a[1]).f32(a[2]).f32(bb[0]).f32(bb[1]).f32(bb[2]);
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j) {
        std::vector<uint32_t> at;
        for (size_t k = 0; k < roads.size(); ++k)
            if (roads[k].j0 == j || roads[k].j1 == j)
                at.push_back(uint32_t(k));
        b.u16(uint32_t(j)).u16(uint32_t(200 + j)).f32(junctions[j][0]).f32(junctions[j][1]).f32(junctions[j][2]);
        b.u16(uint32_t(at.size()));
        for (uint32_t k : at)
            b.u32(k);
    }
    b.u32(0); // the blocks' roads, not read
    return b.v;
}

/** A loop of two one-way roads round a 200 m by 20 m rectangle. */
inline std::vector<uint8_t> loopBai() {
    return buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 1, 0, 5}, {{200, 0, -20}, {0, 0, -20}, 1, 0, 1, 0, 5}},
                    {{-5, 0, -10}, {205, 0, -10}});
}

/** A path for buildPathset: its name (cut or padded to 32 bytes), points and end word. */
struct TestPath {
    std::string name;
    std::vector<Vec3> points;
    uint32_t end;
};

/** A .pathset file of the paths. */
inline std::vector<uint8_t> buildPathset(const std::vector<TestPath> &paths) {
    Bytes b;
    b.str("PTH1").u32(uint32_t(paths.size())).u32(0);
    for (const TestPath &p : paths) {
        std::string name = p.name;
        name.resize(32, '\0');
        b.str(name).u32(uint32_t(p.points.size())).u32(0);
        for (const Vec3 &v : p.points)
            b.u32(7).f32(v[0]).f32(v[1]).f32(v[2]);
        b.u32(p.end);
    }
    return b.v;
}

/** Whether two matrices are the same, each element within 1e-4. */
inline bool sameMatrix(const Mat3 &a, const Mat3 &b) {
    const Vec3 ca[3] = {a.x, a.y, a.z};
    const Vec3 cb[3] = {b.x, b.y, b.z};
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            if (std::fabs(ca[i][size_t(k)] - cb[i][size_t(k)]) > 1e-4f)
                return false;
    return true;
}

/** A renderer that only remembers the textures' sizes; meshes fail to be made. */
struct SizeRenderer : gfx::Renderer3D {
    std::vector<std::pair<int, int>> sizes;
    Error createTexture(int w, int h, const uint8_t *, const gfx::TextureParams &, gfx::TextureId &out) override {
        sizes.emplace_back(w, h);
        out = gfx::TextureId(sizes.size());
        return Error::success();
    }
    void destroyTexture(gfx::TextureId) override {}
    Error createMesh(const std::vector<gfx::Vertex> &, const std::vector<uint32_t> &, gfx::MeshId &) override {
        return Error::fail("no");
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t> &, gfx::MeshId &) override {
        return Error::fail("no");
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t> &) override { return Error::fail("no"); }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return 640; }
    int height() const override { return 480; }
    std::string deviceName() const override { return "sizes"; }
    void beginFrame(const gfx::Mat4 &, const gfx::Vec3 &, Color) override {}
    void draw(const gfx::DrawCall &) override {}
    void setView(const gfx::Mat4 &, const gfx::Vec3 &) override {}
    void setDynamicLights(const std::vector<gfx::DynamicLight> &) override {}
    void setViewport(int, int, int, int) override {}
    void setDepthRange(float) override {}
    void clearDepth() override {}
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float) override {}
    void endFrame(const Surface *) override {}
    void captureNextFrame() override {}
    bool takeCapture(std::vector<uint8_t> &) override { return false; }
};

} // namespace reluva::age::test_support
