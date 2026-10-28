// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Angel Game Engine's readers against files built here,
// following docs/formats/age/, so no game data is needed.

#include "engines/age/aimap.h"
#include "engines/age/ar_archive.h"
#include "engines/age/bai.h"
#include "engines/age/bound.h"
#include "engines/age/car.h"
#include "engines/age/city_mesh.h"
#include "engines/age/engine_sound.h"
#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/gizmos.h"
#include "engines/age/instances.h"
#include "engines/age/opponent.h"
#include "engines/age/pathset.h"
#include "engines/age/ped_model.h"
#include "engines/age/pkg.h"
#include "engines/age/psdl.h"
#include "engines/age/race.h"
#include "engines/age/road_graph.h"
#include "engines/age/texture.h"
#include "engines/age/traffic.h"
#include "engines/age/traffic_lights.h"
#include "engines/age/tune.h"
#include "engines/age/viewers.h"
#include "zip_writer.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::age;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

using test::toBytes;

constexpr float kPi() { return 3.14159265358979f; }

struct ArFile {
    std::string name;              // ending in '/' for a folder
    std::vector<uint8_t> data;     // contents
    std::vector<uint8_t> deflated; // if not empty: stored as these bytes
};

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v[at + size_t(i)] = uint8_t(x >> (8 * i));
}

size_t roundUp(size_t n) { return (n + 2047) / 2048 * 2048; }

// An archive laid out as the retail ones are: the header in the first
// 2048 bytes, then the directory and the names, each padded to 2048, then
// each file's data starting on a 2048-byte boundary.
std::vector<uint8_t> buildAr(const std::vector<ArFile>& files) {
    std::string names;
    std::vector<uint32_t> nameAt;
    for (const ArFile& f : files) {
        nameAt.push_back(uint32_t(names.size()));
        names += f.name;
        names += '\0';
    }
    const size_t dirSize = roundUp(files.size() * 16), namesSize = roundUp(names.size());
    std::vector<uint8_t> out(2048 + dirSize + namesSize);
    std::copy(names.begin(), names.end(), out.begin() + std::ptrdiff_t(2048 + dirSize));
    out[0] = 'D', out[1] = 'A', out[2] = 'V', out[3] = 'E';
    put32(out, 4, uint32_t(files.size()));
    put32(out, 8, uint32_t(dirSize));
    put32(out, 12, uint32_t(namesSize));
    for (size_t i = 0; i < files.size(); ++i) {
        const ArFile& f = files[i];
        const std::vector<uint8_t>& body = f.deflated.empty() ? f.data : f.deflated;
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

// "textures/borg/borgfloor1\n" 12 times, raw-deflated by zlib.
std::string repeatedText() {
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "textures/borg/borgfloor1\n";
    return text;
}
const std::vector<uint8_t> kDeflated = {0x2b, 0x49, 0xad, 0x28, 0x29, 0x2d, 0x4a, 0x2d, 0xd6, 0x4f,
                                        0xca, 0x2f, 0x4a, 0x07, 0x13, 0x69, 0x39, 0xf9, 0xf9, 0x45,
                                        0x86, 0x5c, 0x25, 0xa3, 0x12, 0xe8, 0x12, 0x00};

void testArchive(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    const auto path = tmp / "test.ar";
    const std::string text = repeatedText();
    test::writeFile(path, buildAr({{"tune/", {}, {}},
                                   {"tune/CVS/", {}, {}},
                                   {"tune/vpbug.info", toBytes("BaseName=vpbug\r\n"), {}},
                                   {"city/sf.cinfo", toBytes(text), kDeflated},
                                   {"empty.txt", {}, {}}}));
    ArArchive ar;
    CHECK(!ar.open(path));
    CHECK(ar.entries().size() == 3); // the folders are left out
    CHECK(ar.list() == (std::vector<std::string>{"tune/vpbug.info", "city/sf.cinfo", "empty.txt"}));
    CHECK(ar.has("TUNE\\VPBUG.INFO") && !ar.has("tune/") && !ar.has("tune"));
    CHECK(ar.entry("city/sf.cinfo")->deflated() && !ar.entry("tune/vpbug.info")->deflated());
    std::vector<uint8_t> data;
    CHECK(!ar.read("tune/vpbug.info", data) && data == toBytes("BaseName=vpbug\r\n"));
    CHECK(!ar.read("City/SF.cinfo", data) && data == toBytes(text));
    CHECK(!ar.read("empty.txt", data) && data.empty());
    CHECK(ar.read("missing.txt", data));

    // Damaged deflate data: the file opens, reading it fails.
    std::vector<uint8_t> bad = kDeflated;
    bad[0] ^= 0xff;
    test::writeFile(path, buildAr({{"a.txt", toBytes(text), bad}}));
    CHECK(!ar.open(path) && ar.read("a.txt", data));

    // Not an archive; a directory running past the end; a file running
    // past the end; a name without its terminating zero.
    test::writeFile(path, std::vector<uint8_t>(4096, 0));
    CHECK(ar.open(path));
    test::writeFile(path, toBytes("DAVE"));
    CHECK(ar.open(path));
    auto archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 4, 1000); // 1000 entries don't fit the 2048-byte directory
    test::writeFile(path, archive);
    CHECK(ar.open(path));
    archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 2048 + 12, 1 << 20); // stored size
    test::writeFile(path, archive);
    CHECK(ar.open(path));
    archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 2048, 2047); // the name starts at the last byte of the names, which isn't zero...
    archive[2048 + 2048 + 2047] = 'x';
    test::writeFile(path, archive);
    CHECK(ar.open(path));
}

void testMount(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::vector<std::string> warnings;
    SearchSet set;
    std::filesystem::create_directories(tmp);
    CHECK(mountGame(set, tmp, warnings)); // no archives

    test::writeFile(tmp / "mm2core.ar", buildAr({{"tune/a.txt", toBytes("core"), {}},
                                                 {"tune/b.txt", toBytes("core"), {}},
                                                 {"tune/c.txt", toBytes("core"), {}}}));
    test::writeFile(tmp / "VPMOD.AR", buildAr({{"tune/b.txt", toBytes("mod"), {}},
                                               {"tune/c.txt", toBytes("mod"), {}}}));
    test::writeFile(tmp / "broken.ar", toBytes("not an archive"));
    test::writeFile(tmp / "readme.txt", toBytes("not in an archive"));
    test::writeFile(tmp / "tune" / "c.txt", toBytes("loose"));
    set.clear();
    CHECK(!mountGame(set, tmp, warnings));
    CHECK(warnings.size() == 1); // broken.ar
    CHECK(set.size() == 3);      // two archives and the loose files
    std::vector<uint8_t> data;
    // Later names (in lower case: vpmod after mm2core) win; loose files
    // over every archive.
    CHECK(!set.read("tune/a.txt", data) && data == toBytes("core"));
    CHECK(!set.read("tune/b.txt", data) && data == toBytes("mod"));
    CHECK(!set.read("TUNE/C.TXT", data) && data == toBytes("loose"));
    CHECK(!set.read("readme.txt", data));
}

void testDetection(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "MM2CORE.AR", buildAr({}));
    CHECK(detectFromTable("age", detectionTable(), tmp).empty()); // not the retail file
    CHECK(detectFromTable("age", fallbackTable(), tmp).empty());  // no program
    test::writeFile(tmp / "MIDTOWN2.EXE", toBytes("MZ"));
    const auto found = detectFromTable("age", fallbackTable(), tmp);
    CHECK(found.size() == 1 && found[0].gameId == "mm2");
    CHECK(findGame("mm2") && !findGame("mm1"));
}

void testFileLines() {
    bool isText = false;
    auto lines = fileLines(toBytes("type: a\r\nvehCarSim {\r\n\tMass 1000\r\n}"), isText);
    CHECK(isText);
    CHECK(lines == (std::vector<std::string>{"type: a", "vehCarSim {", "    Mass 1000", "}"}));
    lines = fileLines(toBytes("caf\xe9\n"), isText);
    CHECK(isText && lines.size() == 1 && lines[0] == "caf\xc3\xa9"); // Latin-1 shown as UTF-8

    std::vector<uint8_t> binary = toBytes("PKG3FILE");
    binary.push_back(0);
    binary.push_back(0x7f);
    lines = fileLines(binary, isText);
    CHECK(!isText && lines.size() == 1);
    CHECK(lines[0].rfind("00000000  50 4b 47 33 46 49 4c 45  00 7f", 0) == 0);
    CHECK(lines[0].substr(lines[0].size() - 10) == "PKG3FILE..");
}

// Little-endian writers for building files.
struct Bytes {
    std::vector<uint8_t> v;
    Bytes& u8(uint32_t x) {
        v.push_back(uint8_t(x));
        return *this;
    }
    Bytes& u16(uint32_t x) { return u8(x).u8(x >> 8); }
    Bytes& u32(uint32_t x) { return u16(x & 0xffff).u16(x >> 16); }
    Bytes& f32(float f) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        return u32(x);
    }
    Bytes& str(const std::string& s) {
        v.insert(v.end(), s.begin(), s.end());
        return *this;
    }
    // A name as models store them: a length byte counting the zero after it.
    Bytes& name(const std::string& s) { return u8(uint32_t(s.size() + 1)).str(s).u8(0); }
    Bytes& bytes(const std::vector<uint8_t>& b) {
        v.insert(v.end(), b.begin(), b.end());
        return *this;
    }
};

std::vector<uint8_t> texHeader(int w, int h, int format, int levels, uint32_t flags) {
    return Bytes().u16(uint32_t(w)).u16(uint32_t(h)).u16(uint32_t(format)).u16(uint32_t(levels)).u16(1).u32(flags).v;
}

void testTex() {
    // 2x2 palette texture, two levels (2x2 and 1x1): index 1 is
    // B=10 G=20 R=30 with alpha 40, which only format 14 uses.
    Bytes pal;
    pal.bytes(texHeader(2, 2, texformat::Palette, 2, 0x10001));
    for (int i = 0; i < 256; ++i)
        pal.u8(i == 1 ? 10 : 0).u8(i == 1 ? 20 : 0).u8(i == 1 ? 30 : 0).u8(i == 1 ? 40 : 255);
    pal.u8(0).u8(1).u8(1).u8(0).u8(1);
    Tex tex;
    CHECK(!decodeTex(pal.v, tex));
    CHECK(tex.image.w == 2 && tex.image.h == 2 && tex.levels == 2 && tex.flags == 0x10001);
    CHECK(tex.image.rgba == (std::vector<uint8_t>{0, 0, 0, 255, 30, 20, 10, 255, 30, 20, 10, 255, 0, 0, 0, 255}));
    pal.v[4] = texformat::PaletteAlpha;
    CHECK(!decodeTex(pal.v, tex) && tex.image.pixel(1, 0)[3] == 40);
    pal.v.pop_back(); // the 1x1 level cut short
    CHECK(decodeTex(pal.v, tex));

    // 24-bit and 32-bit: R, G, B(, A) in the file, unlike the palettes.
    Bytes rgb;
    rgb.bytes(texHeader(1, 1, texformat::Rgb, 1, 0)).u8(1).u8(2).u8(3);
    CHECK(!decodeTex(rgb.v, tex) && tex.image.rgba == (std::vector<uint8_t>{1, 2, 3, 255}));
    Bytes rgba;
    rgba.bytes(texHeader(1, 1, texformat::Rgba, 1, 0)).u8(1).u8(2).u8(3).u8(4);
    CHECK(!decodeTex(rgba.v, tex) && tex.image.rgba == (std::vector<uint8_t>{1, 2, 3, 4}));

    CHECK(decodeTex(texHeader(1, 1, 99, 1, 0), tex)); // unknown format
    CHECK(decodeTex(texHeader(0, 1, texformat::Rgb, 1, 0), tex));
    CHECK(decodeTex(toBytes("short"), tex));
}

// A mesh section's contents: one group of one triangle list.
std::vector<uint8_t> meshSection(uint32_t fvf, uint32_t shader, const std::vector<uint16_t>& indices,
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

Bytes& fileSection(Bytes& b, const std::string& name, const std::vector<uint8_t>& contents) {
    return b.str("FILE").name(name).u32(uint32_t(contents.size())).bytes(contents);
}

void testPkg() {
    Bytes shaders; // two paint jobs of one shader, byte colours (R, G, B, A)
    shaders.u32(0x82).u32(1);
    shaders.name("red_body").u8(255).u8(0).u8(0).u8(255).u8(128).u8(0).u8(0).u8(255).u8(0).u8(0).u8(0).u8(0).f32(0.5f);
    // A glow: no texture, no diffuse or ambient, emissive (10, 20, 30).
    shaders.u8(0).u32(0).u32(0).u8(10).u8(20).u8(30).u8(255).f32(0);
    Bytes xrefs;
    xrefs.u32(1);
    for (int i = 0; i < 12; ++i)
        xrefs.f32(float(i));
    std::string field = "sp_light_red_f";
    field.resize(32, '\0');
    field.replace(16, 3, "max"); // what's left over after the name
    xrefs.str(field);
    Bytes offset;
    offset.f32(1).f32(2).f32(3);

    Bytes pkg;
    pkg.str("PKG3");
    fileSection(pkg, "BODY_H", meshSection(0x112, 0, {0, 1, 2}));
    fileSection(pkg, "H", meshSection(0x102, 0, {2, 1, 0}));
    fileSection(pkg, "shaders", shaders.v);
    fileSection(pkg, "xrefs", xrefs.v);
    fileSection(pkg, "offset", offset.v);
    Pkg model;
    CHECK(!readPkg(pkg.v, model));
    CHECK(model.meshes.size() == 2);
    const PkgMesh* body = model.mesh("BODY_H");
    CHECK(body && body->part() == "BODY" && body->level() == "H" && body->fvf == 0x112);
    CHECK(body && body->groups.size() == 1 && body->groups[0].indices == (std::vector<uint16_t>{0, 1, 2}));
    if (body && !body->groups.empty()) {
        const PkgVertex& v = body->groups[0].vertices[2];
        CHECK(v.pos == (Vec3{2, 4, -2}) && v.normal == (Vec3{0, 1, 0}) && v.uv[0] == 0.5f && v.uv[1] == 0.5f);
    }
    const PkgMesh* h = model.mesh("H");
    CHECK(h && h->part().empty() && h->level() == "H" && h->groups[0].vertices[1].normal == (Vec3{0, 0, 0}));
    CHECK(model.paintJobs == 2 && model.shadersPerJob == 1 && model.shaders.size() == 2);
    const PkgShader* red = model.shader(0, 0);
    CHECK(red && red->texture == "red_body" && red->diffuse == (std::array<float, 4>{1, 0, 0, 1}));
    CHECK(red && red->ambient[0] > 0.5f && red->ambient[0] < 0.51f && red->shininess == 0.5f);
    const PkgShader* glow = model.shader(1, 0);
    CHECK(glow && glow->texture.empty() && glow->diffuse[3] == 0 && glow->emissive[2] > 0.11f && glow->emissive[2] < 0.12f);
    CHECK(!model.shader(0, 1) && !model.shader(2, 0));
    CHECK(model.xrefs.size() == 1 && model.xrefs[0].name == "sp_light_red_f" && model.xrefs[0].matrix[11] == 11);
    CHECK(model.hasOffset && model.offset == (Vec3{1, 2, 3}));

    // Float colours: diffuse, ambient, specular, emissive, then shininess.
    Bytes floats;
    floats.u32(1).u32(1).name("s_wood");
    for (int i = 0; i < 16; ++i)
        floats.f32(float(i) / 16);
    floats.f32(0.25f);
    Bytes pkg2;
    pkg2.str("PKG3");
    fileSection(pkg2, "shaders", floats.v);
    CHECK(!readPkg(pkg2.v, model) && model.shaders.size() == 1);
    CHECK(model.shaders[0].specular[0] == 0.5f && model.shaders[0].emissive[3] == 15.0f / 16 &&
          model.shaders[0].shininess == 0.25f);

    // Damage: another version, an index past the vertices, totals that
    // disagree, a section running past the end, data left over.
    Bytes old;
    old.str("PKG2");
    CHECK(readPkg(old.v, model));
    Bytes bad;
    bad.str("PKG3");
    fileSection(bad, "H", meshSection(0x112, 0, {0, 1, 3}));
    CHECK(readPkg(bad.v, model));
    auto totals = meshSection(0x112, 0, {0, 1, 2});
    totals[4] = 4; // four vertices claimed
    bad = Bytes();
    bad.str("PKG3");
    fileSection(bad, "H", totals);
    CHECK(readPkg(bad.v, model));
    bad = Bytes();
    bad.str("PKG3");
    fileSection(bad, "H", meshSection(0x112, 0, {0, 1, 2}));
    bad.v.resize(bad.v.size() - 1);
    CHECK(readPkg(bad.v, model));
    auto extra = meshSection(0x112, 0, {0, 1, 2});
    extra.push_back(0);
    bad = Bytes();
    bad.str("PKG3");
    fileSection(bad, "H", extra);
    CHECK(readPkg(bad.v, model));

    // A part's placement: bounds, pivot, origin.
    Bytes mtx;
    for (int i = 0; i < 12; ++i)
        mtx.f32(float(i));
    Mtx m;
    CHECK(!readMtx(mtx.v, m) && m.min == (Vec3{0, 1, 2}) && m.pivot == (Vec3{6, 7, 8}) && m.origin == (Vec3{9, 10, 11}));
    mtx.v.pop_back();
    CHECK(readMtx(mtx.v, m));
}

void testNames() {
    CHECK(kindOf("geometry/vpbug.pkg") == "vpbug");
    CHECK(kindOf("texture/SW_stc_tan.tex") == "sw");
    CHECK(isLightPart("HEADLIGHT0") && isLightPart("SRN3") && isLightPart("TLIGHT") && isLightPart("REDGLOWNIGHT") &&
          isLightPart("WALK_DAY") && isLightPart("NOWALK_NIGHT"));
    CHECK(!isLightPart("BODY") && !isLightPart("WHL0") && !isLightPart("BREAK01") && !isLightPart(""));
}

// A small city: 6 vertices, heights 10, 20 and 1, two textures, one block.
std::vector<uint8_t> buildPsdl(const std::vector<uint16_t>& attributes, uint32_t version = 2) {
    Bytes b;
    b.str("PSD0").u32(version).u32(6);
    const float v[6][3] = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}, {0, 0.15f, -2}, {10, 0.15f, -2}};
    for (const auto& p : v)
        b.f32(p[0]).f32(p[1]).f32(p[2]);
    b.u32(3).f32(10).f32(20).f32(1);
    b.u32(3).u8(5).str("r2_f").u8(0).u8(8).str("swalk_f").u8(0); // counts are one more than stored
    b.u32(2).u32(0);
    b.u32(2).u32(uint32_t(attributes.size())); // block 1: two perimeter edges
    b.u16(0).u16(0).u16(1).u16(5);
    for (uint16_t w : attributes)
        b.u16(w);
    b.u8(0).u8(4);   // flags of blocks 0 and 1
    b.u8(205).u8(0); // the second bytes
    for (float f : {-1.f, -2.f, -3.f, 4.f, 5.f, 6.f, 1.f, 2.f, 3.f, 9.f})
        b.f32(f);
    return b.v;
}

void testPsdl() {
    // Texture 1 (r2_f); a roof at height 0 (10) over 0, 1, 2, 3; a facade
    // from height 0 to 1 (10 to 20) along 0-1, repeated 2 x 3, marked last;
    // then a stray texture attribute, as the retail cities have.
    const std::vector<uint16_t> attrs = {10 << 3, 1, 12 << 3 | 3, 0, 0, 1, 2, 3,
                                         0x80 | 11 << 3 | 6, 0, 1, 2, 3, 0, 1, 10 << 3, 2};
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl(attrs), psdl));
    CHECK(psdl.vertices.size() == 6 && psdl.heights.size() == 3 && psdl.textures.size() == 2);
    CHECK(psdl.texture(1) == "r2_f" && psdl.texture(2) == "swalk_f" && psdl.texture(0).empty() && psdl.texture(3).empty());
    CHECK(psdl.blocks.size() == 2 && psdl.blocks[1].perimeter.size() == 2 && psdl.blocks[1].perimeter[1].second == 5);
    CHECK(psdl.blocks[1].attributes.size() == 4 && psdl.blocks[1].flags == 4 && psdl.blocks[0].extra == 205);
    if (psdl.blocks.size() == 2 && psdl.blocks[1].attributes.size() > 1) {
        const PsdlAttribute& roof = psdl.blocks[1].attributes[1];
        CHECK(roof.type == psdlattr::Roof && roof.count == 3 && roof.words == (std::vector<uint16_t>{0, 0, 1, 2, 3}));
    }
    CHECK(psdl.min == (Vec3{-1, -2, -3}) && psdl.center == (Vec3{1, 2, 3}) && psdl.radius == 9);

    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    CHECK(mesh.batches.size() == 1 && mesh.batches[0].texture == "r2_f");
    CHECK(mesh.batches[0].indices.size() == 4 * 3); // two roof triangles, two facade triangles
    float lowest = 1e9f, highest = -1e9f, maxU = 0;
    for (const gfx::Vertex& v : mesh.vertices) {
        lowest = std::min(lowest, v.pos[1]);
        highest = std::max(highest, v.pos[1]);
        maxU = std::max(maxU, v.uv[0]);
    }
    CHECK(lowest == 10 && highest == 20 && maxU == 2);
    for (size_t i = 0; i < 3; ++i) // the roof faces up
        CHECK(mesh.vertices[i].pos[1] == 10 && mesh.vertices[i].normal[1] == 1);

    // A road strip of two sections: the road in two halves (centre line
    // at v = 0), two sidewalks; the sidewalks use the next texture.
    const std::vector<uint16_t> road = {10 << 3, 1, 0x80 | 0 << 3 | 2, 4, 0, 1, 5, 4, 3, 2, 5};
    CHECK(!readPsdl(buildPsdl(road), psdl));
    buildCityMesh(psdl, mesh);
    CHECK(mesh.batches.size() == 2 && mesh.batches[1].texture == "swalk_f");

    // Damage: another version; an attribute after the last; one running
    // past its block; an unknown type.
    CHECK(readPsdl(buildPsdl(attrs, 3), psdl));
    CHECK(readPsdl(buildPsdl({0x80 | 10 << 3, 1, 12 << 3 | 3, 0, 0, 1, 2, 3}), psdl));
    CHECK(readPsdl(buildPsdl({0x80 | 11 << 3, 0, 1, 2}), psdl));
    CHECK(readPsdl(buildPsdl({0x80 | 13 << 3}), psdl));
    CHECK(psdlAttributeLength(psdlattr::DividedRoad, 2) == 14 && psdlAttributeLength(psdlattr::Tunnel, 5) == 5);
}

void testInstances() {
    Bytes b;
    // Turned only: the X axis's x and z, then the position.
    b.u16(7).u16(0x100).u8(0x80 | 6).str("sp_box").f32(0).f32(1).f32(10).f32(20).f32(30);
    // Three axes (scaled) and a position.
    b.u16(8).u16(3).u8(4).str("abc").u8(0);
    for (int i = 0; i < 12; ++i)
        b.f32(float(i));
    std::vector<Instance> list;
    CHECK(!readInstances(b.v, list) && list.size() == 2);
    CHECK(list[0].block == 7 && list[0].flags == 0x100 && list[0].model == "sp_box");
    CHECK(list[0].matrix == (std::array<float, 12>{0, 0, 1, 0, 1, 0, -1, 0, 0, 10, 20, 30}));
    CHECK(list[1].model == "abc" && list[1].matrix[11] == 11 && list[1].flags == 3);
    b.v.pop_back();
    CHECK(readInstances(b.v, list));
}

void testTune() {
    TuneNode root;
    CHECK(!parseTune("type: a\r\nclear-noon {\r\n  KeyColor 0.9\t0.8 0.7 \r\n  Engine {\r\n    MaxRPM 8500\r\n  }\r\n"
                     "  Ambient -14145486\r\n}\r\n",
                     root));
    CHECK(root.children.size() == 1 && root.children[0].name == "clear-noon");
    const TuneNode& n = root.children[0];
    CHECK(n.number("keycolor", 2) == 0.7f && n.number("KeyColor", 3, -1) == -1 && n.number("Missing", 0, 5) == 5);
    CHECK(n.child("Engine") && n.child("Engine")->number("MaxRPM") == 8500);
    CHECK(n.number("Ambient") == -14145486.0f);
    CHECK(parseTune("a {\n b 1\n", root));
    CHECK(parseTune("}\n", root));
}

void testBound() {
    Bound b;
    CHECK(!readBound("version: 1.01\r\nverts: 4\r\nmaterials: 1\r\nedges: 0\r\npolys: 2\r\n\r\n"
                     "v\t0 0 0\r\nv 1 0 0\r\nv 1 1 0\r\nv 0 1 0\r\n\r\n"
                     "mtl default {\r\n\telasticity: 0.25\r\n\tfriction: 0.75\r\n\teffect: none\r\n\tsound: 0\r\n}\r\n"
                     "quad 0 1 2 3 0\r\ntri 0 2 3 0\r\n",
                     b));
    CHECK(b.vertices.size() == 4 && b.vertices[2] == (Vec3{1, 1, 0}));
    CHECK(b.materials.size() == 1 && b.materials[0].name == "default" && b.materials[0].elasticity == 0.25f &&
          b.materials[0].friction == 0.75f);
    CHECK(b.polygons.size() == 2 && b.polygons[0].count == 4 && b.polygons[1].count == 3 &&
          b.polygons[1].vertices[1] == 2);
    CHECK(readBound("v 0 0 0\nquad 0 1 2 3 0\n", b)); // vertices that aren't there
    CHECK(readBound("mtl a {\nfriction: 1\n", b));     // not closed
}

void testCollision() {
    CollisionWorld w;
    // A floor at y 0 over -100..100 and a wall at x 10.
    w.add({-100, 0, -100}, {-100, 0, 100}, {100, 0, 100});
    w.add({-100, 0, -100}, {100, 0, 100}, {100, 0, -100});
    w.add({10, 0, -5}, {10, 5, -5}, {10, 5, 5});
    w.add({10, 0, -5}, {10, 5, 5}, {10, 0, 5});
    w.build();
    CollisionWorld::RayHit hit;
    CHECK(w.raycast({3, 2, 4}, {0, -1, 0}, 5, hit) && std::fabs(hit.distance - 2) < 1e-4f && hit.normal[1] == 1);
    CHECK(!w.raycast({3, 2, 4}, {0, -1, 0}, 1.5f, hit));
    CHECK(w.raycast({0, 1, 0}, {1, 0, 0}, 20, hit) && std::fabs(hit.distance - 10) < 1e-4f && hit.normal[0] == -1);
    CollisionWorld::Contact c;
    CHECK(w.sphereContact({9.8f, 1, 0}, 0.5f, c) && c.normal[0] < -0.99f && std::fabs(c.depth - 0.3f) < 1e-4f);
    CHECK(w.sphereContact({0, 0.1f, 0}, 0.25f, c) && c.normal[1] > 0.99f);
    CHECK(!w.sphereContact({0, 2, 0}, 0.5f, c));
}

CarSetup testCar() {
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

void testCarSim() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    const CarSetup setup = testCar();
    CHECK(setup.gearTopSpeed(1) == setup.lowSpeed && std::fabs(setup.gearTopSpeed(setup.gears) - setup.highSpeed) < 1e-3f);
    CHECK(setup.power(setup.optRpm) > setup.power(setup.idleRpm) && setup.power(setup.maxRpm + 1) == 0);

    CarSim car;
    car.reset(setup, {0, 0, 0}, 0);
    CarControls none;
    car.step(none, flat, 0.1f);
    for (int i = 0; i < 30; ++i)
        car.step(none, flat, 0.1f);
    // Settled on its wheels.
    CHECK(length(car.velocity) < 0.05f);
    for (const auto& w : car.wheels)
        CHECK(w.contact && w.compression > 0.02f && w.compression < 0.29f);
    CHECK(car.orientation.y[1] > 0.999f);

    // Full throttle: off along -Z, up through the gears.
    CarControls go;
    go.throttle = 1;
    for (int i = 0; i < 100; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.forwardSpeed() > 15 && car.position[2] < -50 && std::fabs(car.position[0]) < 2 && car.gear > 1);
    for (int i = 0; i < 300; ++i) // 30 s more, not quite to the plane's edge
        car.step(go, flat, 0.1f);
    // Topped out near the top gear's speed.
    CHECK(car.forwardSpeed() > setup.highSpeed * 0.75f && car.forwardSpeed() < setup.highSpeed * 1.1f);

    // Steering right at speed turns it right (towards +X when facing -Z).
    car.reset(setup, {0, 0, 0}, 0);
    for (int i = 0; i < 50; ++i)
        car.step(go, flat, 0.1f);
    CarControls right = go;
    right.throttle = 0.3f;
    right.steer = 1;
    const float x0 = car.position[0];
    for (int i = 0; i < 20; ++i)
        car.step(right, flat, 0.1f);
    CHECK(car.position[0] > x0 + 5 && car.orientation.y[1] > 0.95f);

    // Braking stops it; holding the brake then backs up.
    CarControls stop;
    stop.brake = 1;
    for (int i = 0; i < 100; ++i)
        car.step(stop, flat, 0.1f);
    CHECK(car.gear == -1 && car.forwardSpeed() < -1);
}

void testEngineSound() {
    EngineSoundSetup e;
    CHECK(!readEngineSound("Horn wave name,Horn volume,flags,Num Engine Samples,clutch wave name,clutch volume,,,,,\r\n"
                           "VWHORN,0.95,0,2,REVERSE,0.93,,,,,\r\n"
                           "Engine wave name,Min Volume,Max Volume,...\r\n"
                           "VWIDLE,0.55,0.835,1,800,2500,7000,0.85,2,1,7000\r\n"
                           "VWHIGH,0.55,0.91,3000,8000,15000,15000,0.65,2.25,3000,12000\r\n",
                           e));
    CHECK(e.horn == "VWHORN" && e.samples.size() == 2 && e.samples[1].wave == "VWHIGH");
    CHECK(e.samples[0].fadeOutStart == 2500 && e.samples[1].pitchEnd == 12000);
    float volume, pitch;
    // Idle: faded in by 800 RPM, out from 2500 to 7000; pitched 0.85 to 2 over 1-7000.
    engineSampleMix(e.samples[0], 800, 0, volume, pitch);
    CHECK(std::fabs(volume - 0.55f) < 1e-4f && pitch > 0.85f && pitch < 1.0f);
    engineSampleMix(e.samples[0], 800, 1, volume, pitch);
    CHECK(std::fabs(volume - 0.835f) < 1e-4f);
    engineSampleMix(e.samples[0], 7000, 1, volume, pitch);
    CHECK(volume == 0 && std::fabs(pitch - 2) < 1e-4f);
    engineSampleMix(e.samples[1], 2000, 1, volume, pitch); // not faded in yet
    CHECK(volume == 0);
    CHECK(readEngineSound("a\nb\n", e));
}

void testSidewalkMarkers() {
    // A two-pair sidewalk strip starting (0, 0), as 188 in the cities do:
    // nothing is drawn for it, rather than a strip from vertex 0.
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl({10 << 3, 1, 0x80 | 1 << 3 | 2, 0, 0, 2, 3}), psdl));
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    CHECK(mesh.vertices.empty());
}

void testRaceFiles() {
    std::array<RaceSettings, 2> set;
    const std::string table =
        "Description, CarType, TimeofDay, Weather, Opponents, Cops, Ambient, Peds, NumLaps, TimeLimit, Difficulty, ...\r\n"
        "none,0,0,0,4,0,0.0,0.0,3,50,1,0,0,0,4,0,0.0,0.0,4,40,1\r\n"
        "none,0,2,1,7,1,0.2,0.1,3,50,1,0,3,2,6,2,0.3,0.4,4,40,1\r\n";
    CHECK(!parseRaceSettings(table, 1, set));
    CHECK(set[0].timeOfDay == 2 && set[0].weather == 1 && set[0].opponents == 7 && set[0].cops == 1 &&
          set[0].laps == 3 && set[0].timeLimit == 50);
    CHECK(set[1].timeOfDay == 3 && set[1].opponents == 6 && set[1].laps == 4 && std::fabs(set[1].peds - 0.4f) < 1e-5f);
    CHECK(parseRaceSettings(table, 2, set));

    std::vector<Waypoint> w;
    CHECK(!parseWaypoints("x,y,z,a,radius,frame rate,state changes,texture changes,msg\r\n"
                          "-1501.4,36.3,-285.7,92.3,8.0,0,0,0,\r\n-1466.5,34.8,-285.8,89.6,6.0,0,0,0,\r\n",
                          w));
    CHECK(w.size() == 2 && w[0].radius == 8 && w[1].heading > 89.5f && w[1].position[2] < -285.7f);
    CHECK(!parseWaypoints("x,y,z,a,poly count,frane rate,state changes,texture changes,msg\n1,2,3,-90,11,0,0,0,\n4,5,6,0,11,0,0,0,\n", w));
    CHECK(w[0].radius == 0); // circuits: a poly count, not a radius
    CHECK(parseWaypoints("x,y,z,a,radius\n1,2,3,4,5\n", w)); // one waypoint

    // An opponent's start, a point behind it (left out), then its path.
    RaceDefinition::Opponent o;
    CHECK(!parseOpponentPath("x,y,z,brake,forward offset,side offset,target speed,speed start,side start\n"
                             "0,0,0,91.2,0,0,0,0,0\n70,0,0,0,0,0,0,0,0\n-100,0,0,0,0,0,0,0,0\n-200,0,50,0,0,0,0,0,0\n",
                             o));
    CHECK(o.skippedFirst && o.path.size() == 2 && o.path[0][0] == -100 && o.heading == 91.2f);
    CHECK(std::fabs(o.startYaw - kPi() / 2) < 1e-4f); // facing -X
    CHECK(!parseOpponentPath("x,y,z\n0,0,0,0\n10,0,0\n20,0,0\n", o) && !o.skippedFirst && o.path.size() == 2);

    // Headings: 0 faces -Z, 90 faces +X.
    const Mat3 east = Mat3::rotationY(headingToYaw(90));
    CHECK(std::fabs((-east.z)[0] - 1) < 1e-4f);
}

void testPathDriver() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    // Two laps of a 200 m square.
    const std::vector<Vec3> square = {{0, 0, -200}, {200, 0, -200}, {200, 0, 0}, {0, 0, 0}};
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    PathDriver driver;
    driver.start(square, 2, 0.9f);
    float t = 0;
    for (; t < 300 && !driver.finished(); t += 0.05f)
        car.step(driver.drive(car, 0.05f), flat, 0.05f);
    CHECK(driver.finished() && driver.lap() == 2 && t < 200); // lap() counts laps done
    CHECK(std::fabs(car.position[0]) < 20 && std::fabs(car.position[2]) < 20);
}

void testRoadGraph() {
    // Two roads (along X, then along Z further on) meeting at an
    // intersection fan in its own block.
    Bytes b;
    const float v[][3] = {{0, 0, 0},    {0, 0, -5},   {0, 0, 5},    {0, 0, 0},    {50, 0, -5},  {50, 0, 5},
                          {50, 0, 0},   {50, 0, 0},   {60, 0, -5},  {60, 0, 5},   {55, 0, -10}, {55, 0, 10},
                          {55, 0, 10},  {60, 0, 10},  {50, 0, 10},  {60, 0, 100}, {50, 0, 100}, {0, 0, 0}};
    b.str("PSD0").u32(2).u32(18);
    for (const auto& p : v)
        b.f32(p[0]).f32(p[1]).f32(p[2]);
    b.u32(1).f32(0).u32(1).u32(4).u32(0);
    auto block = [&](const std::vector<uint16_t>& attrs) {
        b.u32(0).u32(uint32_t(attrs.size()));
        for (uint16_t w : attrs)
            b.u16(w);
    };
    block({0x80 | 0 << 3 | 2, 0, 1, 2, 0, 0, 4, 5, 0});         // road along X: curbs 1-2, then 4-5
    block({0x80 | 5 << 3 | 4, 4, 5, 9, 8, 10, 11});              // the intersection's fan
    block({0x80 | 0 << 3 | 2, 0, 14, 13, 0, 0, 16, 15, 0});     // road along Z: curbs 14-13, 16-15
    b.u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0);
    for (int i = 0; i < 10; ++i)
        b.f32(0);
    Psdl psdl;
    CHECK(!readPsdl(b.v, psdl));
    RoadGraph g;
    g.build(psdl);
    CHECK(g.nodeCount() == 5 && g.edgeCount() == 4);
    const auto route = g.route({0, 0, 0}, {55, 0, 100});
    // Along the first road, through the intersection, up the second.
    CHECK(route.size() >= 3 && route.back() == (Vec3{55, 0, 100}));
    bool viaIntersection = false;
    for (const Vec3& p : route)
        viaIntersection = viaIntersection || (std::fabs(p[0] - 55) < 6 && std::fabs(p[2]) < 6);
    CHECK(viaIntersection);
    CHECK(g.route({2000, 0, 2000}, {0, 0, 0}).size() == 1); // nowhere near a road
}


// A road map (docs/formats/age/bai.md) of straight roads, each from one
// junction to another: `lanes` lanes on side 1 (one-way, first to last)
// and `back` on side 0, `n` points along it.
struct BaiRoad {
    Vec3 from, to;
    uint32_t j0, j1; // junctions at the first and the last point
    int lanes = 1, back = 0, n = 3;
    uint16_t control = 1; // at both ends
};

void baiSide(Bytes& b, const BaiRoad& r, int lanes, int side) {
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

std::vector<uint8_t> buildBai(const std::vector<BaiRoad>& roads, const std::vector<Vec3>& junctions) {
    Bytes b;
    b.str("CAI1").u16(uint32_t(junctions.size())).u16(uint32_t(roads.size()));
    for (size_t k = 0; k < roads.size(); ++k) {
        const BaiRoad& r = roads[k];
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
            const Vec3 a = (e == 0 ? r.to : r.from) + right * 8.0f, bb = a - right;
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

// A loop of two one-way roads round a 200 m by 20 m rectangle.
std::vector<uint8_t> loopBai() {
    return buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 1, 0, 5}, {{200, 0, -20}, {0, 0, -20}, 1, 0, 1, 0, 5}},
                    {{-5, 0, -10}, {205, 0, -10}});
}

void testBai() {
    const auto data = buildBai({{{0, 0, 0}, {100, 0, 0}, 0, 1, 2, 1, 3}}, {{0, 0, 0}, {100, 0, 0}});
    AiRoadMap m;
    CHECK(!readBai(data, m));
    CHECK(m.roads.size() == 1 && m.junctions.size() == 2);
    const AiRoad& r = m.roads[0];
    CHECK(r.blocks.size() == 1 && r.blocks[0] == 100 && r.halfWidth == 10 && r.speedLimit == 15);
    CHECK(r.sides[1].lanes.size() == 2 && r.sides[0].lanes.size() == 1 && r.middle.size() == 3);
    CHECK(r.sides[1].laneOffsets.size() == 2 && r.sides[1].laneOffsets[1] == 7.5f);
    CHECK(r.sides[1].lanes[0].front()[0] == 0 && r.sides[0].lanes[0].front()[0] == 100); // side 0's run back
    CHECK(r.sides[1].sidewalk.size() == 3 && std::fabs(r.sides[1].curb[0][2] - 8) < 1e-4f);
    CHECK(r.ends[0].junction == 1 && r.ends[1].junction == 0 && r.ends[0].control == 1);
    CHECK(m.junctions[1].block == 201 && m.junctions[1].roads == std::vector<uint32_t>{0});

    CHECK(readBai(std::vector<uint8_t>(data.begin(), data.end() - 30), m)); // truncated
    auto bad = data;
    bad[0] = 'X';
    CHECK(readBai(bad, m));

    // The lanes as driven: side 1's from the first point where traffic
    // keeps right, side 0's back; the other way round where it keeps left.
    CHECK(!readBai(data, m));
    AiMapSettings right;
    TrafficNetwork net;
    net.build(m, right);
    CHECK(net.lanes().size() == 3);
    for (const TrafficLane& l : net.lanes()) {
        const bool forward = l.side == 1;
        CHECK(l.from == (forward ? 0u : 1u) && l.to == (forward ? 1u : 0u));
        CHECK(std::fabs(l.points.front()[0] - (forward ? 0 : 100)) < 1e-3f && std::fabs(l.length() - 100) < 1e-3f);
    }
    CHECK(net.leaving(0).size() == 2 && net.leaving(1).size() == 1);
    AiMapSettings left;
    left.driveOnLeft = true;
    net.build(m, left);
    for (const TrafficLane& l : net.lanes())
        CHECK(l.from == (l.side == 0 ? 0u : 1u) && std::fabs(l.points.front()[0] - (l.side == 0 ? 0 : 100)) < 1e-3f);
    // A race's exceptions: no traffic starting on the road, and its limit.
    right.exceptions.push_back({0, 0, 9});
    net.build(m, right);
    CHECK(!net.lanes()[0].spawns && net.lanes()[0].speed == 9);

    Vec3 heading;
    const std::vector<Vec3> pts = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}};
    const std::vector<float> at = {0, 10, 20};
    CHECK(pointAlong(pts, at, 15, &heading) == (Vec3{10, 0, 5}));
    CHECK(std::fabs(heading[2] - 1) < 1e-4f);
    CHECK(pointAlong(pts, at, 99) == (Vec3{10, 0, 10}));
}

void testAiMap() {
    AiMapSettings s;
    CHECK(!parseAiMap("[Speed Limit]\n15.00\n\n[Ambients Drive On The Left]\n1\n\n[Ambient Types/Density]\n2\n"
                      "va_bus_f 0.25 0\nva_cab_l 1.0 0\n\n[GoodWeatherPedName / BadWeatherPedName]\n1\n"
                      "pedmodel_man pedmodel_manw \n",
                      s));
    CHECK(s.speedLimit == 15 && s.driveOnLeft && s.ambients.size() == 2 && s.ambients[1].model == "va_cab_l");
    CHECK(s.peds.size() == 1 && s.peds[0].bad == "pedmodel_manw");
    // A race's file over the city's: its own sections replace theirs.
    CHECK(!parseAiMap("# Default Road Speed Limit\n[Speed Limit]\n12\n\n[Exceptions]\n1\n55\t0.00\t0\n\n"
                      "# Geo File, StartLink...\n[Police]\n1\nvpcop\t-978.0 89.7 62.8 50.000000 0 15 0.5 50.0\n\n"
                      "[Opponent]\n2\nvpbug race3-a-0.opp 0.96 0 50.0 0.7 1 1 1 1 0 1.0\n"
                      "vpcaddie race3-a-1.opp 0.88 0 50.0 0.7 1 1 1 1 0 1.0\n#vpbug old.opp 0.5\n",
                      s));
    CHECK(s.speedLimit == 12 && s.driveOnLeft && s.ambients.size() == 2);
    CHECK(s.exceptions.size() == 1 && s.exceptions[0].road == 55 && s.exceptions[0].density == 0);
    CHECK(s.police.size() == 1 && s.police[0].heading == 50 && s.police[0].rest.size() == 4 && s.policeGiven);
    CHECK(s.opponents.size() == 2 && s.opponents[1].model == "vpcaddie" && std::fabs(s.opponents[0].skill - 0.96f) < 1e-5f);
    CHECK(parseAiMap("nothing here", s));
}

void testTraffic(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "geometry" / "va_test.pkg", toBytes("PKG3"));
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiRoadMap m;
    CHECK(!readBai(loopBai(), m));
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);

    // One car round the loop: along the lanes, through both junctions.
    Traffic t;
    t.setup(files, m, s, 3);
    t.setDensity(1.0f / Traffic::kCarsAtFull);
    t.update(0.05f, {100, 0, -10}, {}, flat, true);
    CHECK(t.count() == 1);
    if (t.count() != 1)
        return;
    float minZ = 0, maxZ = -20, travelled = 0;
    std::vector<RoadUser> u;
    t.users(u);
    Vec3 last = u[0].position;
    for (int i = 0; i < 1200; ++i) { // a minute
        t.update(0.05f, {100, 0, -10}, {}, flat);
        u.clear();
        t.users(u);
        travelled += length(u[0].position - last);
        last = u[0].position;
        minZ = std::min(minZ, last[2]);
        maxZ = std::max(maxZ, last[2]);
        CHECK(length(u[0].velocity) <= 15.01f);
    }
    CHECK(travelled > 400 && minZ < -15 && maxZ > -5); // round both roads

    // Something stopped in its way: it stops behind.
    Traffic held;
    held.setup(files, m, s, 3);
    held.setDensity(1.0f / Traffic::kCarsAtFull);
    held.update(0.05f, {100, 0, -10}, {}, flat, true);
    u.clear();
    held.users(u);
    const Vec3 heading = normalized(u[0].velocity);
    const Vec3 block = u[0].position + heading * 25.0f;
    for (int i = 0; i < 160; ++i) // 8 s (after 10 it would go on through)
        held.update(0.05f, {100, 0, -10}, {{block, {}}}, flat);
    u.clear();
    held.users(u);
    const float gap = dot(block - u[0].position, heading);
    CHECK(length(u[0].velocity) < 0.5f && gap > 5 && gap < 12);

    // A driven car running into one from behind knocks it off its lane.
    Traffic hit;
    hit.setup(files, m, s, 3);
    hit.setDensity(1.0f / Traffic::kCarsAtFull);
    hit.update(0.05f, {100, 0, -10}, {}, flat, true);
    u.clear();
    hit.users(u);
    const Vec3 way = normalized(u[0].velocity);
    CarSim car;
    car.reset(testCar(), u[0].position - way * 4.0f + Vec3{0, 0.5f, 0}, std::atan2(-way[0], -way[2]));
    car.velocity = way * 30.0f;
    CHECK(hit.collide(car) > 0);
    std::filesystem::remove_all(tmp);
}

void testPedFiles() {
    Skeleton sk;
    CHECK(!parseSkeleton("NumBones 3\nbone root {\n\toffset 0 1 0\n\tbone spine {\n\t\toffset 0 0.5 0\n"
                         "\t\tbone head {\n\t\t\toffset 0 0.25 0\n\t\t}\n\t}\n}\n",
                         sk));
    CHECK(sk.bones.size() == 3 && sk.bones[2].parent == 1 && sk.bones[1].offset[1] == 0.5f);
    Skeleton broken;
    CHECK(parseSkeleton("bone a {\n}\n}\n", broken)); // unbalanced

    // Two frames of 3 + 3 * 3 channels: the spine turned a quarter about Z
    // in the second, and the root moved along -Z (left out of the pose).
    Bytes b;
    b.u32(0).u32(2).u32(12).f32(1.5f).u8(1);
    for (int f = 0; f < 2; ++f)
        for (int c = 0; c < 12; ++c)
            b.f32(c == 1 ? 1.0f : c == 2 ? -0.5f * float(f) : (f == 1 && c == 8) ? kPi() / 2 : 0.0f);
    PedAnimation anim;
    CHECK(!readPedAnimation(b.v, anim) && anim.frames == 2 && anim.channels == 12);
    CHECK(readPedAnimation(std::vector<uint8_t>(b.v.begin(), b.v.end() - 4), anim));
    CHECK(!readPedAnimation(b.v, anim));
    auto near = [](const Vec3& a, const Vec3& b) { return length(a - b) < 1e-5f; };
    auto pose = poseOf(sk, anim, 0);
    CHECK(near(pose[2].at, {0, 1.75f, 0}));
    pose = poseOf(sk, anim, 1);
    CHECK(near(pose[1].at, {0, 1.5f, 0}) && near(pose[2].at, {-0.25f, 1.5f, 0}));

    std::vector<PedMove> moves;
    CHECK(!parsePedMoves("# anim name,mma name,...\nSTAND,pedanim_manstand,1,30,0,0,0,0,STAND\n"
                         "WALK,pedanim_manwalk,1,20,0.281,1.409,0,0,WALK\n",
                         moves));
    CHECK(moves.size() == 2 && moves[1].animation == "pedanim_manwalk" && moves[1].next == "WALK");

    // A model in packets (one a material), the vertices in bone order.
    const std::string head = "version: 1.09\nverts: 3\n\nv\t0 0 0\nv\t1 0 0\nv\t0 1 0\nn\t0 0 1\nt1\t0 0\n";
    const std::string mtl = "mtl M:SKIN {\n\tpackets:\t1\n\tprimitives:\t1\n\tadjuncts:\t3\n\tillum: diffuse\n"
                            "\tdiffuse:\t0.5 0.25 1\n}\n";
    PedMesh mesh;
    CHECK(!parsePedMesh(head + mtl +
                            "packet 3 1 2 {\n\tadj 0 0 0 0 0 0\n\tadj 1 0 0 0 0 0\n\tadj 2 0 0 0 0 1\n"
                            "\ttri 0 1 2\n\tmtx 0 1\n}\nmtxv 2 1\n",
                        mesh));
    CHECK(mesh.materials.size() == 1 && mesh.materials[0].corners.size() == 3 && mesh.materials[0].indices.size() == 3);
    CHECK(mesh.materials[0].corners[2].bone == 1 && mesh.materials[0].corners[1].bone == 0);
    CHECK(mesh.materials[0].diffuse[1] == 0.25f);
    // The same in one list, without the packets.
    CHECK(!parsePedMesh(head + mtl + "adj 0 0 0 0 0\nadj 1 0 0 0 0\nadj 2 0 0 0 0\ntri 0 1 2\nmtxv 2 1\n", mesh));
    CHECK(mesh.materials[0].corners.size() == 3 && mesh.materials[0].corners[2].bone == 1);
    CHECK(parsePedMesh(head + mtl + "adj 0 0 0 0 0\nadj 1 0 0 0 0\nadj 2 0 0 0 0\ntri 0 1 2\nmtxv 2\n", mesh));
}

void testTrafficLights() {
    // A crossroads: four roads into junction 0 from the north, east, south
    // and west, each from a junction of its own.
    const auto data = buildBai({{{0, 0, -100}, {0, 0, 0}, 1, 0}, {{100, 0, 0}, {0, 0, 0}, 2, 0},
                                {{0, 0, 100}, {0, 0, 0}, 3, 0}, {{-100, 0, 0}, {0, 0, 0}, 4, 0}},
                               {{0, 0, 0}, {0, 0, -100}, {100, 0, 0}, {0, 0, 100}, {-100, 0, 0}});
    AiRoadMap m;
    CHECK(!readBai(data, m));
    AiMapSettings s;
    TrafficLights lights;
    lights.build(m, s);
    CHECK(lights.count() == 8 && lights.has(0, 0) && !lights.has(9, 0));
    // Opposite roads together, the crossing ones the other way; each turn
    // green, then amber, then red.
    int greenTogether = 0, conflicts = 0, ambers = 0;
    for (float t = 0; t < 64; t += 0.5f) {
        lights.setClock(t);
        const Signal n = lights.signal(0, 0), e = lights.signal(1, 0);
        const Signal so = lights.signal(2, 0), w = lights.signal(3, 0);
        CHECK(n == so && e == w);
        greenTogether += n == Signal::Green;
        conflicts += n != Signal::Red && e != Signal::Red;
        ambers += n == Signal::Yellow;
    }
    CHECK(conflicts == 0 && greenTogether > 40 && ambers > 8);
    CHECK(lights.signal(7, 0) == Signal::Green); // no light: as green

    // No lights where the ends say none (control 3) or there are no points.
    const auto plain = buildBai({{{0, 0, 0}, {100, 0, 0}, 0, 1, 1, 0, 3, 3}}, {{0, 0, 0}, {100, 0, 0}});
    CHECK(!readBai(plain, m));
    lights.build(m, s);
    CHECK(lights.count() == 0);

    AiMapSettings london;
    CHECK(!parseAiMap("[Traffic Lights]\nsp_traflitsingle_ped_l sp_traflitsingle_ped_l\n", london));
    CHECK(london.trafficLights[1] == "sp_traflitsingle_ped_l" && s.trafficLights[0] == "sp_traflitsingle_f");
}

void testTrafficRules(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "geometry" / "va_test.pkg", toBytes("PKG3"));
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);

    // A loop of two-lane one-way roads.
    AiRoadMap m;
    CHECK(!readBai(buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 2, 0, 5}, {{200, 0, -30}, {0, 0, -30}, 1, 0, 2, 0, 5}},
                            {{-5, 0, -15}, {205, 0, -15}}),
                   m));
    TrafficNetwork net;
    net.build(m, s);
    CHECK(net.lanes().size() == 4);
    // Side by side: on the first road (heading +X, right is +Z) the lane
    // at z 7.5 has the one at 2.5 on its left.
    for (size_t i = 0; i < 2; ++i) {
        const TrafficLane& l = net.lanes()[i];
        const bool outer = l.points[0][2] > 5;
        CHECK((outer ? l.left : l.right) == int(1 - i) && (outer ? l.right : l.left) == -1);
    }

    // Stopped by a red light, on at green.
    TrafficLights lights;
    lights.build(m, s);
    float red = -1;
    for (float t = 0; t < 60 && red < 0; t += 0.5f) {
        lights.setClock(t);
        if (lights.signal(0, 0) == Signal::Red && lights.signal(1, 0) == Signal::Red)
            red = t;
    }
    CHECK(red >= 0);
    Traffic t;
    t.setup(files, m, s, 3);
    t.setLights(&lights);
    t.setDensity(2.0f / Traffic::kCarsAtFull);
    t.update(0.05f, {100, 0, -15}, {}, flat, true);
    CHECK(t.count() == 2);
    for (int i = 0; i < 600; ++i) // 30 s: both reach their road's end and wait there
        t.update(0.05f, {100, 0, -15}, {}, flat);
    std::vector<RoadUser> u;
    t.users(u);
    for (const RoadUser& c : u)
        CHECK(length(c.velocity) < 0.5f && (c.position[0] > 180 || c.position[0] < 20));
    // Green for the first road: those waiting at its end go on through,
    // those at the second's go as its own light says.
    std::vector<uint32_t> roadOf;
    for (const RoadUser& c : u)
        roadOf.push_back(c.position[2] > 0 ? 0 : 1);
    float green = red;
    while (lights.signal(0, 0) != Signal::Green && green < red + 60)
        lights.setClock(green += 0.5f);
    for (int i = 0; i < 40; ++i) // 2 s, the lights held
        t.update(0.05f, {100, 0, -15}, {}, flat);
    u.clear();
    t.users(u);
    for (size_t i = 0; i < u.size(); ++i)
        CHECK((length(u[i].velocity) > 2) == (lights.signal(roadOf[i], 0) == Signal::Green));

    // Held up behind something stopped in its lane: round it in the other.
    AiMapSettings quick = s;
    bool tried = false;
    for (uint32_t seed = 1; seed < 40 && !tried; ++seed) {
        Traffic one;
        one.setup(files, m, quick, seed);
        one.setLaneChanges(true);
        one.setDensity(1.0f / Traffic::kCarsAtFull);
        one.update(0.05f, {100, 0, -15}, {}, flat, true);
        u.clear();
        one.users(u);
        // One on the first road with room ahead of it.
        if (u.size() != 1 || u[0].position[2] < -10 || u[0].position[0] < 15 || u[0].position[0] > 60)
            continue;
        const Vec3 block = u[0].position + Vec3{30, 0, 0};
        for (int i = 0; i < 160; ++i) // 8 s
            one.update(0.05f, {100, 0, -15}, {{block, {}}}, flat);
        u.clear();
        one.users(u);
        CHECK(u[0].position[0] > block[0] + 5 && std::fabs(u[0].position[2] - block[2]) > 4);
        tried = true;
    }
    CHECK(tried);
    std::filesystem::remove_all(tmp);
}

void testSirenTable() {
    // The computer-driven police's: samples, each with ways on.
    std::vector<SirenSample> s;
    CHECK(!readSirenTable("Sample name,\r\npolice1fastsirenloop,\r\nplay time,next index\r\n4.45,1\r\n"
                          "play time,next index\r\n2.65,2\r\nSample name,\r\npolice2hilowloop,\r\n"
                          "play time,next index\r\n2.31,2\r\nSample name,\r\npolice1whailerloop,\r\n"
                          "play time,next index\r\n1.11,1\r\n",
                          s));
    CHECK(s.size() == 3 && s[0].wave == "police1fastsirenloop" && s[0].volume == 1 && s[0].next.size() == 2);
    CHECK(std::fabs(s[0].next[1].first - 2.65f) < 1e-5f && s[0].next[1].second == 2 && s[2].next[0].second == 1);
    // The player's: an explosion first, volumes given.
    CHECK(!readSirenTable("Explosion sample,volume\nexplosion,0.95\nSample name,\nsiren_london,0.95\n"
                          "play time,next index\n15,1\nSample name,\nsiren_london2,0.95\nplay time,next index\n15,0\n",
                          s));
    CHECK(s.size() == 2 && s[1].wave == "siren_london2" && std::fabs(s[1].volume - 0.95f) < 1e-5f &&
          s[1].next[0] == (std::pair<float, int>{15, 0}));
    CHECK(readSirenTable("Explosion sample,volume\nexplosion,0.95\n", s));
}

void testSpeedLimit() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    PathDriver driver;
    driver.start({{0, 0, -500}, {0, 0, -1000}}, 0, 1.0f);
    driver.setSpeedLimit(15);
    float fastest = 0;
    for (float t = 0; t < 40 && !driver.finished(); t += 0.05f) {
        car.step(driver.drive(car, 0.05f), flat, 0.05f);
        fastest = std::max(fastest, car.forwardSpeed());
    }
    CHECK(fastest > 13 && fastest < 17.5f);
}

} // namespace

// A path set of the given paths: name, points and end word.
struct TestPath {
    std::string name;
    std::vector<Vec3> points;
    uint32_t end;
};
std::vector<uint8_t> buildPathset(const std::vector<TestPath>& paths) {
    Bytes b;
    b.str("PTH1").u32(uint32_t(paths.size())).u32(0);
    for (const TestPath& p : paths) {
        std::string name = p.name;
        name.resize(32, '\0');
        b.str(name).u32(uint32_t(p.points.size())).u32(0);
        for (const Vec3& v : p.points)
            b.u32(7).f32(v[0]).f32(v[1]).f32(v[2]);
        b.u32(p.end);
    }
    return b.v;
}

void testPathset() {
    const std::vector<uint8_t> data = buildPathset(
        {{"giz_ferry_f", {{0, 0, 0}, {10, 0, 0}}, 0x6402}, {"open:giz_bridge01_l", {{0, 8, 0}, {30, 8, 0}}, 0x0801},
         {"INACTIVE:giz_chinagate_f", {{5, 1, 5}, {5, 1, 9}}, 0x0801}});
    std::vector<PathsetPath> paths;
    CHECK(!readPathset(data, paths) && paths.size() == 3);
    if (paths.size() == 3) {
        CHECK(paths[0].model == "giz_ferry_f" && !paths[0].open && !paths[0].backAndForth());
        CHECK(std::fabs(paths[0].speed() - 10) < 1e-4f && paths[0].ids == (std::vector<uint32_t>{7, 7}));
        CHECK(paths[1].model == "giz_bridge01_l" && paths[1].open && paths[1].backAndForth());
        CHECK(paths[1].points[1][0] == 30 && paths[1].points[1][1] == 8);
        CHECK(paths[2].model == "giz_chinagate_f" && paths[2].inactive && !paths[2].open);
    }
    // Cut short in the last path: the whole ones are kept.
    const std::vector<uint8_t> cut(data.begin(), data.end() - 6);
    CHECK(!readPathset(cut, paths) && paths.size() == 2);
    CHECK(readPathset(toBytes("PTH0"), paths));
}

void testGizmos(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    // Models whose level-H mesh runs 0-2 m across: leaves 1 m either side.
    Bytes pkg;
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    for (const char* m : {"giz_bridge01_l", "giz_tug_l"})
        test::writeFile(tmp / "geometry" / (std::string(m) + ".pkg"), pkg.v);
    test::writeFile(tmp / "race" / "london" / "london_bridge.pathset",
                    buildPathset({{"giz_bridge01_l", {{0, 8, 0}, {30, 8, 0}, {60, 8, 0}}, 0x0801}}));
    test::writeFile(tmp / "race" / "london" / "london_ferry.pathset",
                    buildPathset({{"giz_tug_l", {{0, 0, 100}, {20, 0, 100}}, 0x0a01}, // back and forth at 1 m/s
                                  {"giz_nothere_l", {{0, 0, 0}, {9, 0, 0}}, 0x0a02}}));
    // A race's bridge, starting raised, over the city's (its first point
    // within 5 m).
    test::writeFile(tmp / "race" / "london" / "london_bridge_race0.pathset",
                    buildPathset({{"open:giz_bridge01_l", {{1, 8, 0}, {30, 8, 0}, {60, 8, 0}}, 0x0801}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);

    Gizmos g;
    CHECK(!g.load(files, "london"));
    CHECK(g.boats() == 1 && g.bridges() == 1); // no model for the second boat
    CHECK(g.raised(0) == 0);
    // Two leaves meeting in the middle: the city's road under them is
    // taken out, not that beyond the hinges, well above or to the side.
    CHECK(g.inGap({10, 8, -0.5f}, {12, 8, 0.5f}, {14, 8, 0}) && g.inGap({48, 8, 0}, {50, 8, 0.5f}, {49, 8, -0.5f}));
    CHECK(!g.inGap({-4, 8, 0}, {-3, 8, 0}, {-3, 8, 0.5f}) && !g.inGap({10, 20, 0}, {12, 20, 0}, {11, 20, 1}));
    CHECK(!g.inGap({10, 8, 3}, {12, 8, 3}, {11, 8, 4}));
    std::vector<RoadUser> u;
    g.users(u);
    CHECK(u.empty());
    CollisionWorld world;
    world.build();
    g.update(0, world);
    CollisionWorld::RayHit hit;
    const Vec3 down{0, -1, 0};
    CHECK(world.raycast({15, 20, 0}, down, 30, hit) && std::fabs(hit.point[1] - 8) < 1e-3f);
    // Down 50 s, then rising: the leaves tip up from the hinges, and the
    // traffic stops there.
    for (int i = 0; i < 62 * 4; ++i)
        g.update(0.25f, world);
    CHECK(g.raised(0) == 1);
    g.users(u);
    CHECK(!u.empty() && u[0].solid);
    for (const RoadUser& r : u)
        CHECK(std::fabs(r.position[0]) < 1e-3f || std::fabs(r.position[0] - 60) < 1e-3f);
    CHECK(!world.raycast({29, 20, 0}, down, 30, hit) || hit.point[1] > 15);
    CHECK(world.raycast({2, 20, 0}, down, 30, hit) &&
          std::fabs(hit.point[1] - (8 + 2 * std::tan(Gizmos::kRaised))) < 0.05f);

    // The race's bridge is up from the start.
    CHECK(!g.load(files, "london", "race0") && g.bridges() == 1 && g.raised(0) == 1);
}


void testParkedCars(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    Bytes pkg; // a level-H body 2 m across, 4 high and 2 long
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    test::writeFile(tmp / "geometry" / "va_test.pkg", pkg.v);
    test::writeFile(tmp / "geometry" / "sp_pcar_t.pkg", pkg.v);
    test::writeFile(tmp / "tune" / "vehicle" / "va_test.aiVehicleData",
                    toBytes("type: a\naiVehicleData {\n  Mass 1000\n  Size 2 1.5 4\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    CollisionWorld flat;
    flat.add({-500, 0, -500}, {-500, 0, 500}, {500, 0, 500});
    flat.add({-500, 0, -500}, {500, 0, 500}, {500, 0, -500});
    flat.build(64);
    AiRoadMap m; // the first road's lanes run along +X at z 2.5 and 7.5
    CHECK(!readBai(buildBai({{{0, 0, 0}, {200, 0, 0}, 0, 1, 2, 0, 5}, {{200, 0, -30}, {0, 0, -30}, 1, 0, 2, 0, 5}},
                            {{-5, 0, -15}, {205, 0, -15}}),
                   m));
    auto line = [](const std::string& name, const Vec3& a, const Vec3& b) {
        PathsetPath p;
        p.name = p.model = name;
        p.points = {a, b};
        p.end = 0x1402; // a 2 m gap
        return p;
    };
    // By the curb; in a car park far from the roads; with its own model.
    const std::vector<PathsetPath> lines = {line("PATH01", {20, 0.5f, 10}, {60, 0.5f, 10}),
                                            line("PATH02", {20, 0, 100}, {41, 0, 100}),
                                            line("sp_pcar_t", {100, 0, 10}, {130, 0, 10})};
    Traffic t;
    t.setup(files, m, s, 7);
    t.park(files, lines, {});
    CHECK(t.parked() > 3 && t.count() == 0);
    std::vector<std::pair<Vec3, Vec3>> places;
    t.parkedPlaces(places);
    auto onGrid = [](float x, float first, float step) {
        const float k = (x - first) / step;
        return std::fabs(k - std::round(k)) < 1e-3f;
    };
    for (const auto& [p, h] : places) {
        if (p[2] > 50) { // side by side, 3 m apart (2 wide, 1 between), facing across
            CHECK(std::fabs(h[2]) > 0.99f && p[0] > 20 && p[0] < 41 && onGrid(p[0], 21.5f, 3));
        } else if (p[0] < 70) { // nose to tail, 6 m apart (4 long, a 2 m gap), along
            CHECK(h[0] > 0.99f && p[0] > 20 && p[0] < 60 && onGrid(p[0], 23, 6));
        } else { // its own model, 2 m long
            CHECK(h[0] > 0.99f && onGrid(p[0], 102, 4));
        }
    }
    // Not among those to keep clear of, nor left out far from the player;
    // stood on the ground.
    std::vector<RoadUser> u;
    t.users(u);
    CHECK(u.empty());
    for (int i = 0; i < 10; ++i)
        t.update(0.05f, {0, 0, 5000}, {}, flat);
    const size_t parked = t.parked();
    CHECK(parked == places.size());
    places.clear();
    t.parkedPlaces(places);
    CHECK(std::fabs(places[0].first[1]) < 1e-3f);
    // Knocked, one slides off and is to be kept clear of.
    Vec3 position = places[0].first, velocity{0, 0, -8};
    CHECK(t.collide({places[0].first + Vec3{0, 0.5f, 1.5f}}, 1, 1e9f, position, velocity) > 0);
    for (int i = 0; i < 10; ++i)
        t.update(0.05f, {0, 0, 5000}, {}, flat);
    t.users(u);
    CHECK(u.size() == 1 && u[0].position[2] < places[0].first[2] - 0.1f && t.parked() == parked);
    t.park(files, {}, {});
    CHECK(t.parked() == 0);
    std::filesystem::remove_all(tmp);
}

void testTrains(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    Bytes pkg; // carriages 2 m wide and 2 long, standing on their origin
    pkg.str("PKG3");
    fileSection(pkg, "H", meshSection(0x102, 0, {0, 1, 2}));
    test::writeFile(tmp / "geometry" / "va_ug_l.pkg", pkg.v);
    test::writeFile(tmp / "geometry" / "va_test.pkg", pkg.v);
    test::writeFile(tmp / "race" / "london" / "london_train.pathset",
                    buildPathset({{"PATH0", {{0, 0, 0}, {100, 0, 0}}, 0x1402}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    Gizmos g;
    CHECK(!g.load(files, "london") && g.trains() == 1 && g.boats() == 0);
    CollisionWorld world;
    world.build();
    // Off from the start, gathering speed.
    const float span = 3 * 2 + 2 * 0.6f;
    for (int i = 0; i < 50; ++i)
        g.update(0.1f, world);
    CHECK(std::fabs(g.trainSpeed(0) - 5 * Gizmos::kTrainAccel) < 0.2f && g.trainPosition(0)[0] > 10);
    // Standing at the far end, then back.
    float clock = 5;
    while (g.trainSpeed(0) > 0 && clock < 60) {
        g.update(0.1f, world);
        clock += 0.1f;
    }
    CHECK(clock > 15 && clock < 30 && std::fabs(g.trainPosition(0)[0] - (100 - span + 1)) < 0.05f);
    for (int i = 0; i < int(Gizmos::kTrainStand * 10) - 5; ++i)
        g.update(0.1f, world);
    CHECK(g.trainSpeed(0) == 0);
    for (int i = 0; i < 50; ++i)
        g.update(0.1f, world);
    CHECK(g.trainSpeed(0) > 1 && g.trainPosition(0)[0] < 100 - span);
    std::vector<RoadUser> u;
    g.users(u);
    CHECK(!u.empty() && u[0].solid && u[0].velocity[0] < -1);

    // A car in its way is shoved out of it.
    CarSim car;
    car.reset(testCar(), g.trainPosition(0) + Vec3{-2, 0, 0}, 0);
    car.velocity = {};
    float knock = 0;
    for (int i = 0; i < 20; ++i) {
        g.update(0.05f, world);
        knock = std::max(knock, g.collide(car));
    }
    CHECK(knock > 1 && car.position[0] < g.trainPosition(0)[0] - 1);
    // A parked car on the line is pushed aside.
    AiMapSettings s;
    s.ambients.push_back({"va_test", 1});
    Traffic t;
    t.setup(files, AiRoadMap{}, s, 1);
    PathsetPath across;
    across.name = across.model = "PATH01";
    across.points = {{20, 0, 1}, {20, 0, -1}};
    t.park(files, {across}, {});
    std::vector<std::pair<Vec3, Vec3>> before, after;
    t.parkedPlaces(before);
    CHECK(before.size() <= 1);
    for (int i = 0; i < 400; ++i) {
        g.update(0.1f, world);
        g.push(t);
    }
    t.parkedPlaces(after);
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(length(after[i].first - before[i].first) > 1);
    std::filesystem::remove_all(tmp);
}
int main() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_age_tests";
    testArchive(tmp / "archive");
    testMount(tmp / "mount");
    testDetection(tmp / "detect");
    testFileLines();
    testTex();
    testPkg();
    testNames();
    testPsdl();
    testInstances();
    testTune();
    testBound();
    testCollision();
    testCarSim();
    testEngineSound();
    testSidewalkMarkers();
    testRaceFiles();
    testPathDriver();
    testRoadGraph();
    testBai();
    testAiMap();
    testTraffic(tmp / "traffic");
    testPedFiles();
    testTrafficLights();
    testTrafficRules(tmp / "rules");
    testSirenTable();
    testSpeedLimit();
    testPathset();
    testGizmos(tmp / "gizmos");
    testParkedCars(tmp / "parked");
    testTrains(tmp / "trains");
    std::filesystem::remove_all(tmp);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all age tests passed\n");
    return 0;
}
