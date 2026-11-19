// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Meshes: vertex and skeletal ones, the shared animation sets, imported-model blocks, class defaults for placed meshes, poses and rotations.
//
//   reluva-tests unreal.unit.meshes

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

std::vector<uint8_t> meshData(PackageWriter &w, bool lod, int32_t textureRef, int importBlock = 0) {
    const size_t base = 64;
    Bytes o;
    endProps(o, w);
    for (int i = 0; i < 6; ++i)
        o.f32(0);
    o.u8(1);
    o.f32(0), o.f32(0), o.f32(0), o.f32(10);
    auto packed = [](int x, int y, int z) {
        return uint32_t(x & 0x7ff) | uint32_t(y & 0x7ff) << 11 | uint32_t(z & 0x3ff) << 22;
    };
    // A lazy array: the file offset just past it, the count, the elements.
    auto lazy = [&](int count, const std::vector<uint8_t> &elements) {
        Bytes c;
        c.index(count);
        o.u32(uint32_t(base + o.b.size() + 4 + c.b.size() + elements.size()));
        o.bytes(c.b);
        o.bytes(elements);
    };
    Bytes verts;
    for (int z : {0, 5}) {
        verts.u32(packed(-3, 0, z));
        verts.u32(packed(10, 0, z));
        verts.u32(packed(0, 10, z));
    }
    if (importBlock)
        lazy(0, {});
    else
        lazy(6, verts.b);
    Bytes tris;
    if (!lod) {
        tris.u16(0), tris.u16(1), tris.u16(2);
        tris.u8(0), tris.u8(0), tris.u8(128), tris.u8(0), tris.u8(0), tris.u8(128);
        tris.u32(0), tris.i32(0);
    }
    lazy(lod ? 0 : 1, tris.b);
    o.index(1); // animations
    o.index(w.name("Wave")), o.index(w.name("None")), o.i32(0), o.i32(2), o.index(0), o.f32(10);
    lazy(0, {}); // connects
    for (int i = 0; i < 6; ++i)
        o.f32(0);
    o.u8(1);
    o.f32(0), o.f32(0), o.f32(0), o.f32(10);
    lazy(0, {}); // vertex links
    o.index(1), o.index(textureRef);
    if (importBlock) {
        // Some of the block's other contents (names, counts), then the
        // vertex arrays: (count, count, 1) and the packed vertices.
        o.u32(1), o.u16(4), o.u8('n'), o.u8('a'), o.u8('m'), o.u8('e'), o.u32(3), o.f32(0.5f);
        if (importBlock == 4 || importBlock == 5) {
            // A skeleton: "root", no vertices, placed 10 up, and its one
            // child "b", placed 10 up from it, with the first frame's
            // vertices (by Scale 2) 20 below it, their weights and indices.
            auto bone = [&](const char *name, bool verts, bool child) {
                o.u16(uint16_t(std::strlen(name)));
                for (const char *c = name; *c; ++c)
                    o.u8(uint8_t(*c));
                o.u32(1), o.u32(1), o.u32(8), o.u32(0);
                const uint32_t k = verts ? 3 : 0;
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.vec(-6, 0, -20), o.vec(20, 0, -20), o.vec(0, 20, -20);
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.f32(1), o.f32(1), o.f32(1);
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.u32(0), o.u32(1), o.u32(2);
                for (int m = 0; m < 2; ++m) {
                    o.vec(1, 0, 0), o.vec(0, 1, 0), o.vec(0, 0, 1), o.vec(0, 0, m ? 10.0f : 0.0f);
                    o.u32(m ? 4 : 7);
                }
                // A clip, without keys (5: the child's clips not understood).
                if (importBlock == 5 && !child)
                    o.u32(1), o.u32(2), o.u32(1);
                else
                    o.u32(1), o.u32(1), o.u32(1);
                o.u32(0), o.u32(0), o.u32(32), o.u32(0), o.u32(0), o.u32(32), o.u32(0), o.u32(0);
                o.u32(child ? 1 : 0), o.u32(child ? 1 : 0), o.u32(4); // children
            };
            // After a bone's children: a matrix, 7, 10 floats and two
            // arrays of i32s.
            auto last = [&](int links) {
                o.vec(1, 0, 0), o.vec(0, 1, 0), o.vec(0, 0, 1), o.vec(0, 0, 0);
                o.u32(7);
                for (int k = 0; k < 10; ++k)
                    o.f32(0);
                for (int a = 0; a < 2; ++a) {
                    o.u32(uint32_t(links)), o.u32(uint32_t(links)), o.u32(1);
                    for (int k = 0; k < links; ++k)
                        o.u32(3);
                }
            };
            bone("root", false, true);
            bone("b", true, false);
            last(1);
            last(0);
            // What follows the bones: body parts' names.
            o.u32(1), o.u32(1), o.u32(10), o.u16(8);
            for (char c : std::string("FullBody"))
                o.u8(uint8_t(c));
        } else if (importBlock == 1) {
            o.u32(6), o.u32(6), o.u32(1);
            o.bytes(verts.b);
        } else {
            // 2: one array per frame; 3: only the first frame's (a keyframed mesh).
            for (int f = 0; f < (importBlock == 3 ? 1 : 2); ++f) {
                o.u32(3), o.u32(3), o.u32(1);
                o.bytes(std::vector<uint8_t>(verts.b.begin() + f * 12, verts.b.begin() + f * 12 + 12));
                o.u32(0), o.u32(64);
            }
        }
        o.u32(14), o.u32(14), o.u32(256);
    }
    o.index(0), o.index(0);
    o.i32(3), o.i32(2);
    o.u32(0), o.u32(0);
    o.vec(2, 2, 2), o.vec(0, 0, 0), o.i32(0), o.i32(0), o.i32(0);
    o.i32(0), o.i32(0);
    o.index(0); // texture levels of detail
    if (lod) {
        o.index(0), o.index(0);
        o.index(1), o.u16(0), o.u16(1), o.u16(2), o.u16(0); // a face: wedges, material
        o.index(0);
        o.index(3); // wedges: vertex, u, v
        o.u16(0), o.u8(0), o.u8(0), o.u16(1), o.u8(128), o.u8(0), o.u16(2), o.u8(0), o.u8(128);
        o.index(1), o.u32(polyflag::kMasked), o.i32(0); // a material
        o.index(0);
        o.i32(3), o.i32(0);
        for (int i = 0; i < 6; ++i)
            o.f32(0);
        o.index(0);
        o.i32(3);
    }
    return o.b;
}

void testMeshes() {
    for (bool lod : {false, true}) {
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, lod ? "LodMesh" : "Mesh");
        const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
        w.addExport(meshClass, 0, "Triangle", meshData(w, lod, 2));
        w.addExport(texClass, 0, "Skin", {});
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err);
        if (err) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            continue;
        }
        CHECK(m.lod == lod && m.frameVerts == 3 && m.animFrames == 2 && m.verts.size() == 6 && m.vertexBytes == 4);
        CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[5] == Vec3f{0, 10, 5}));
        CHECK(m.tris.size() == 1 && m.tris[0].vertex[2] == 2 && m.tris[0].uv[1][0] == 0.5f);
        CHECK(m.tris[0].polyFlags == (lod ? polyflag::kMasked : 0u));
        CHECK(m.textures.size() == 1 && m.textures[0] == 2 && (m.scale == Vec3f{2, 2, 2}) && m.trailingBytes == 0);
        const MeshAnimSeq *wave = m.findAnim("wave");
        CHECK(wave && wave->numFrames == 2 && wave->rate == 10);
    }
}

void testAnimationSet() {
    // A shared animation set: a Mesh with a skeleton ("root" and its child
    // "b", no vertices) and one clip, whose only key turns "b" a quarter
    // turn about Z. A character built on bones of the same names borrows it.
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "Mesh");
    const int32_t lodClass = w.addImport("Engine", "Class", 0, "LodMesh");
    Bytes s;
    endProps(s, w);
    auto bone = [&](const char *name, bool key, bool child) {
        s.u16(uint16_t(std::strlen(name)));
        for (const char *c = name; *c; ++c)
            s.u8(uint8_t(*c));
        s.u32(1), s.u32(1), s.u32(8), s.u32(0);
        for (int a = 0; a < 3; ++a)
            s.u32(0), s.u32(0), s.u32(128);
        for (int m = 0; m < 2; ++m) {
            s.vec(1, 0, 0), s.vec(0, 1, 0), s.vec(0, 0, 1), s.vec(0, 0, m ? 10.0f : 0.0f);
            s.u32(m ? 4 : 7);
        }
        s.u32(1), s.u32(1), s.u32(1);  // a clip
        s.u32(0), s.u32(0), s.u32(32); // no position keys
        s.u32(key ? 1 : 0), s.u32(key ? 1 : 0), s.u32(32);
        if (key) {
            // A quarter turn about Z: x, y 0 (1023.5 of 2047), z sin 45
            // degrees, w - 1 = cos 45 degrees - 1.
            const uint32_t z = uint32_t(std::lround((0.70710678 + 1) / 2 * 1023));
            s.f32(0), s.u32(1023u | 1023u << 11 | z << 22),
                s.u16(uint16_t(int16_t(std::lround((0.70710678 - 1) * 32767))));
        }
        s.u32(0), s.u32(0);
        s.u32(child ? 1 : 0), s.u32(child ? 1 : 0), s.u32(4);
    };
    auto last = [&] {
        s.vec(1, 0, 0), s.vec(0, 1, 0), s.vec(0, 0, 1), s.vec(0, 0, 0);
        s.u32(7);
        for (int k = 0; k < 10; ++k)
            s.f32(0);
        for (int a = 0; a < 2; ++a)
            s.u32(0), s.u32(0), s.u32(1);
    };
    bone("root", false, true);
    bone("b", true, false);
    last();
    last();
    // The character first: its data holds file offsets for the first export.
    w.addExport(lodClass, 0, "Character", meshData(w, true, 2, 4));
    w.addExport(meshClass, 0, "Male", s.b);
    Package p;
    CHECK(!p.openMemory(w.build(), "Shared"));
    AnimationSet set;
    const Error err = readAnimationSet(p, 1, set);
    CHECK(!err && set.frames() == 1 && set.clipFor(0, 1) == 0 && set.clipFor(0, 2) == -1);
    Mesh m;
    CHECK(!readMesh(p, 0, m) && m.bones == 2);
    MeshAnimSeq seq;
    seq.name = "Turned";
    CHECK(!borrowAnimation(p, 0, set, 0, seq, m));
    CHECK(m.animFrames == 3 && m.verts.size() == 9 && m.findAnim("Turned") && m.findAnim("Turned")->startFrame == 2);
    if (m.verts.size() == 9) {
        // (-3, 0, 0) turned a quarter about Z: (0, +-3, 0).
        const Vec3f &v = m.verts[6];
        CHECK(std::fabs(v[0]) < 0.05f && std::fabs(std::fabs(v[1]) - 3) < 0.05f && std::fabs(v[2]) < 0.05f);
    }
}

void testImportedMeshBlock() {
    // The Fallen's layout, both ways of keeping the vertices.
    for (int block : {1, 2}) {
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, block));
        w.addExport(texClass, 0, "Skin", {});
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err);
        if (err) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            continue;
        }
        CHECK(m.importBlock > 0 && m.frameVerts == 3 && m.animFrames == 2 && m.verts.size() == 6);
        CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[5] == Vec3f{0, 10, 5}) && m.tris.size() == 1);
        CHECK((m.scale == Vec3f{2, 2, 2}) && m.trailingBytes == 0 && m.findAnim("Wave"));
        CHECK(m.keyFrames == 0 && m.importAxes && m.importAxesGuessed); // no float copy to fit
    }
    {
        // Only the first frame's vertices: used for both frames.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 3));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        CHECK(!readMesh(p, 0, m) && m.keyFrames == 1 && m.verts.size() == 6 && m.verts[3] == m.verts[0]);
    }
    {
        // No packed vertices, a bone: its pose, for both frames.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 4));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err && m.bones == 2 && m.verts.size() == 6);
        if (!err && m.verts.size() == 6)
            CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[2] == Vec3f{0, 10, 0}) && m.verts[5] == m.verts[2] &&
                  m.importAxes && !m.importAxesGuessed);
        const auto lines = describeBones(p, 0);
        CHECK(!lines.empty() && lines.back().find("all in one tree") != std::string::npos);
    }
    {
        // A bone whose clips aren't understood is still found, and placed
        // under its parent.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 5));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err && m.bones == 2 && m.verts.size() == 6 && (m.verts[0] == Vec3f{-3, 0, 0}));
        const auto lines = describeBones(p, 0);
        CHECK(!lines.empty() && lines.back().find("not understood") != std::string::npos);
    }
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "Mesh");
    w.addExport(meshClass, 0, "Triangle", meshData(w, false, 0, 1));
    Package p;
    CHECK(!p.openMemory(w.build(), "Meshes"));
    // A Mesh with the block and its triangles where they usually are reads
    // too (The Fallen's own have none there, and are refused for now).
    Mesh m;
    CHECK(!readMesh(p, 0, m) && m.importBlock > 0 && m.tris.size() == 1 && m.verts.size() == 6);
}

// A class's data up to its defaults (script.md): no fields or script,
// within Object (import -1), config "System".

void classRecord(Bytes &b, PackageWriter &w, const char *name) {
    b.index(0), b.index(0);         // super, next
    b.index(0), b.index(0);         // script text, children
    b.index(w.name(name));          // friendly name
    b.i32(-1), b.i32(-1), b.i32(0); // line, text position, script size
    b.u64(0), b.u64(~0ull);         // probe and ignore masks
    b.u16(0xffff), b.i32(0);        // label table, state flags
    b.i32(0);                       // class flags
    for (int i = 0; i < 16; ++i)    // GUID
        b.u8(0);
    b.index(0), b.index(0);    // dependencies, package imports
    b.index(-1);               // within
    b.index(w.name("System")); // config name
}

void testClassDefaultsAndMeshActors() {
    // A class "Lamp" without script whose defaults draw it as a mesh,
    // twice the size. A placed Lamp sets only its location.
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
    const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
    const int32_t meshRef = w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2));
    w.addExport(texClass, 0, "Skin", {});
    Bytes cls;
    classRecord(cls, w, "Lamp");
    tag(cls, w, "DrawType", PropertyType::kByte, 1);
    cls.u8(2);
    tag(cls, w, "Mesh", PropertyType::kObject, 1);
    cls.index(meshRef);
    tag(cls, w, "DrawScale", PropertyType::kFloat, 4);
    cls.f32(2);
    endProps(cls, w);
    const int32_t lampClass = w.addExport(0, 0, "Lamp", cls.b);
    Bytes actor;
    tag(actor, w, "Location", PropertyType::kStruct, 12, 0, "Vector");
    actor.vec(1, 2, 3);
    endProps(actor, w);
    w.addExport(lampClass, 0, "Lamp0", actor.b);

    PackageSet set;
    set.addMemory("Level", ".unr", w.build());
    const Package *p = set.get("Level");
    CHECK(p != nullptr);
    if (!p)
        return;
    ClassDefaults defaults(set);
    const PropertyList *d = defaults.defaults(*p, int(lampClass) - 1);
    CHECK(d && d->items.size() == 3 && d->byte("DrawType") == 2 && d->number("DrawScale") == 2);
    MeshActorStats stats;
    const std::vector<MeshActor> actors = findMeshActors(set, *p, defaults, &stats);
    CHECK(actors.size() == 1 && stats.meshes == 1 && stats.fromDefaults == 1);
    if (actors.size() == 1) {
        CHECK(actors[0].mesh.package == p && actors[0].mesh.exportIndex == int(meshRef) - 1);
        CHECK(actors[0].drawScale == 2 && (actors[0].location == Vec3f{1, 2, 3}));
    }
}

void testAnimationPose() {
    // A sequence of 4 frames starting at frame 2, posed by how far through it.
    Mesh mesh;
    mesh.animFrames = 10;
    MeshAnimSeq seq;
    seq.startFrame = 2;
    seq.numFrames = 4;
    seq.rate = 10;
    MeshPose p = poseAtFraction(mesh, &seq, 0.6f, false); // 2.4 frames in
    CHECK(p.frame == 4 && p.next == 5 && std::abs(p.lerp - 0.4f) < 1e-4f);
    p = poseAtFraction(mesh, &seq, 0.9f, true); // the last frame, on round to the first
    CHECK(p.frame == 5 && p.next == 2 && std::abs(p.lerp - 0.6f) < 1e-4f);
    p = poseAtFraction(mesh, &seq, 0.9f, false); // played once: held there
    CHECK(p.frame == 5 && p.next == 5 && p.lerp == 0);
    p = poseAtFraction(mesh, &seq, -0.1f, false); // tweening in: the first frame
    CHECK(p.frame == 2 && p.next == 3 && p.lerp == 0);
}

// A dialogue table's rows: the header and spacer rows left out, quoted
// fields unquoted, Windows-1252 dashes plain; keys matched with '?'.

void testRotatorFromAxes() {
    // Back from a rotation's axes to the rotation, all three turns at once.
    for (const Rotator &r :
         {Rotator{0, 0, 0}, Rotator{2000, 16384, 0}, Rotator{-3000, -12000, 5000}, Rotator{8000, 30000, -9000}}) {
        const Rotator back = rotatorFromAxes(rotationAxes(r));
        CHECK(std::abs(back.pitch - r.pitch) <= 2 && std::abs(back.yaw - r.yaw) <= 2 &&
              std::abs(back.roll - r.roll) <= 2);
    }
    // Slerped: the ends, halfway round a quarter turn, and the shorter way
    // across the 0/65536 seam.
    const Rotator a{1000, 2000, 3000}, b{-2000, 12000, 0};
    const Rotator s0 = slerpRotators(a, b, 0), s1 = slerpRotators(a, b, 1);
    CHECK(std::abs(s0.pitch - a.pitch) <= 2 && std::abs(s0.yaw - a.yaw) <= 2 && std::abs(s0.roll - a.roll) <= 2);
    CHECK(std::abs(s1.pitch - b.pitch) <= 2 && std::abs(s1.yaw - b.yaw) <= 2 && std::abs(s1.roll - b.roll) <= 2);
    const Rotator half = slerpRotators({0, 0, 0}, {0, 16384, 0}, 0.5f);
    CHECK(half.pitch == 0 && std::abs(half.yaw - 8192) <= 2 && half.roll == 0);
    const Rotator seam = slerpRotators({0, 64536, 0}, {0, 1000, 0}, 0.5f);
    CHECK(std::abs(seam.yaw) <= 2 || std::abs(seam.yaw - 65536) <= 2);
}

} // namespace

static const reluva::test::Suite kSuite(
    "unreal.unit.meshes",
    {
        {"meshes", [](const std::filesystem::path &) { testMeshes(); }},
        {"animation set", [](const std::filesystem::path &) { testAnimationSet(); }},
        {"imported mesh block", [](const std::filesystem::path &) { testImportedMeshBlock(); }},
        {"class defaults and mesh actors", [](const std::filesystem::path &) { testClassDefaultsAndMeshActors(); }},
        {"animation pose", [](const std::filesystem::path &) { testAnimationPose(); }},
        {"rotator from axes", [](const std::filesystem::path &) { testRotatorFromAxes(); }},
    });
