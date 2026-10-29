// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Angel Game Engine's readers against files built here,
// following docs/formats/age/, so no game data is needed.

#include "engines/age/aimap.h"
#include "engines/age/ambient.h"
#include "engines/age/music.h"
#include "engines/age/ar_archive.h"
#include "engines/age/bai.h"
#include "engines/age/bound.h"
#include "engines/age/car.h"
#include "engines/age/city_mesh.h"
#include "engines/age/damage.h"
#include "engines/age/dashboard.h"
#include "engines/age/engine_sound.h"
#include "engines/age/filesystem.h"
#include "engines/age/front_end.h"
#include "engines/age/games.h"
#include "engines/age/gizmos.h"
#include "engines/age/hud_map.h"
#include "engines/age/instances.h"
#include "engines/age/lesson.h"
#include "engines/age/materials.h"
#include "engines/age/opponent.h"
#include "engines/age/pathset.h"
#include "engines/age/ped_model.h"
#include "engines/age/pkg.h"
#include "engines/age/props.h"
#include "engines/age/psdl.h"
#include "engines/age/pvs.h"
#include "engines/age/race.h"
#include "engines/age/record.h"
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
// A city of one block (by default with two perimeter edges), then `tail`
// (its roads) after the bounds.
std::vector<uint8_t> buildPsdl(const std::vector<uint16_t>& attributes, uint32_t version = 2,
                               const std::vector<std::pair<uint16_t, uint16_t>>& perimeter = {{0, 0}, {1, 5}},
                               const std::vector<std::string>& textures = {"r2_f", "swalk_f"},
                               const std::vector<uint8_t>& tail = {}) {
    Bytes b;
    b.str("PSD0").u32(version).u32(6);
    const float v[6][3] = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}, {0, 0.15f, -2}, {10, 0.15f, -2}};
    for (const auto& p : v)
        b.f32(p[0]).f32(p[1]).f32(p[2]);
    b.u32(3).f32(10).f32(20).f32(1);
    b.u32(uint32_t(textures.size() + 1)); // counts are one more than stored
    for (const std::string& t : textures)
        b.name(t);
    b.u32(2).u32(1); // blocks (plus one), the first road block
    b.u32(uint32_t(perimeter.size())).u32(uint32_t(attributes.size()));
    for (const auto& [vertex, beyond] : perimeter)
        b.u16(vertex).u16(beyond);
    for (uint16_t w : attributes)
        b.u16(w);
    b.u8(0).u8(4);   // flags of blocks 0 and 1
    b.u8(205).u8(0); // the second bytes
    for (float f : {-1.f, -2.f, -3.f, 4.f, 5.f, 6.f, 1.f, 2.f, 3.f, 9.f})
        b.f32(f);
    return b.bytes(tail).v;
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

    // The roads after the bounds: flags, a word, lanes each side and where
    // they are across, a byte each end, eight vertices, then the blocks
    // (negative in some of San Francisco's).
    Bytes roads;
    roads.u32(1).u16(0x440).u16(1).u8(1).u8(1).f32(0.25f).f32(0.75f).u8(1).u8(0x80);
    for (uint16_t v = 0; v < 8; ++v)
        roads.u16(v);
    roads.u8(2).u16(1).u16(0xffff);
    CHECK(!readPsdl(buildPsdl(attrs, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f"}, roads.v), psdl));
    CHECK(psdl.firstRoadBlock == 1 && psdl.roads.size() == 1);
    if (psdl.roads.size() == 1) {
        const PsdlRoad& r = psdl.roads[0];
        CHECK(r.flags == 0x440 && r.extra == 1 && r.lanes[0] == 1 && r.lanes[1] == 1);
        CHECK(r.lanePositions == (std::vector<float>{0.25f, 0.75f}) && r.ends[0] == 1 && r.ends[1] == 0x80);
        CHECK(r.vertices[7] == 7 && r.blocks == (std::vector<int32_t>{1, -1}));
    }
    roads.v.pop_back();
    CHECK(readPsdl(buildPsdl(attrs, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f"}, roads.v), psdl)); // cut short
    roads.u8(0xff).u8(0);
    CHECK(readPsdl(buildPsdl(attrs, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f"}, roads.v), psdl)); // a byte left over
}

void testTunnels() {
    // A tunnel over a rectangle strip from x 0 to 10 along z 0-10: walls
    // 5.5 m high both sides, a curved ceiling reaching 7 at its crown, its
    // start closed above the walls.
    const std::vector<uint16_t> tube = {10 << 3, 1, 9 << 3 | 3, 0x153, 0x580, 0x700, 0x80 | 2 << 3 | 2, 0, 1, 3, 2};
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl(tube), psdl));
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    float crown = -1, wallTop = -1;
    for (const gfx::Vertex& v : mesh.vertices) {
        crown = std::max(crown, v.pos[1]);
        if (v.pos[0] == 0)
            wallTop = std::max(wallTop, v.pos[1]);
    }
    CHECK(std::fabs(crown - 7) < 1e-4f && std::fabs(wallTop - 5.5f) < 1e-4f);
    // Two walls, eight pieces of arch, the strip, half the start's end each
    // side (four pieces each).
    CHECK(mesh.batches.size() == 1 && mesh.batches[0].indices.size() == (4 + 16 + 2 + 8) * 3);
    // The lower height is the walls' either way round (San Francisco's
    // tunnels: 9 then 7).
    CHECK(!readPsdl(buildPsdl({10 << 3, 1, 9 << 3 | 3, 0x103, 0x900, 0x700, 0x80 | 2 << 3 | 2, 0, 1, 3, 2}), psdl));
    buildCityMesh(psdl, mesh);
    crown = wallTop = -1;
    for (const gfx::Vertex& v : mesh.vertices) {
        crown = std::max(crown, v.pos[1]);
        if (v.pos[0] == 0)
            wallTop = std::max(wallTop, v.pos[1]);
    }
    CHECK(std::fabs(crown - 9) < 1e-4f && std::fabs(wallTop - 7) < 1e-4f);

    // A junction: walls along the edges the mask names (bit i: the edge
    // ending at outline vertex i), a flat ceiling at 5.5 over the block.
    const std::vector<uint16_t> junction = {10 << 3, 1, 9 << 3, 10, 0xb, 0x580, 0x700, 0, 0b1010, 0, 0, 0, 0, 0,
                                            0x80 | 6 << 3 | 2, 0, 1, 2, 3};
    CHECK(!readPsdl(buildPsdl(junction, 2, {{0, 0}, {1, 7}, {2, 0}, {3, 7}}), psdl));
    buildCityMesh(psdl, mesh);
    size_t ceiling = 0, walls = 0;
    for (size_t i = 0; i + 2 < mesh.batches[0].indices.size(); i += 3) {
        const gfx::Vertex& a = mesh.vertices[mesh.batches[0].indices[i]];
        if (a.normal[1] != 0 && a.pos[1] > 5)
            ++ceiling;
        else if (a.normal[1] == 0)
            ++walls;
    }
    CHECK(ceiling == 2 && walls == 4); // the edges 0-1 and 2-3
}

void testCityExtras() {
    // Water over block 1 at its height, a batch of its own.
    CityWater water;
    CHECK(!parseCityWater("-1.9\r\n1\r\n", water) && water.height == -1.9f &&
          water.blocks == (std::vector<uint16_t>{1}));
    CHECK(parseCityWater("-1.9\nboat\n", water) && parseCityWater("", water));
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl({0x80 | 10 << 3, 1}, 2, {{0, 0}, {1, 0}, {2, 0}, {3, 0}}), psdl));
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    CHECK(!parseCityWater("-1.9 1", water));
    addCityWater(psdl, water, 1, mesh);
    CHECK(mesh.batches.size() == 1 && mesh.batches[0].water && mesh.batches[0].indices.size() == 6);
    for (uint32_t i : mesh.batches[0].indices)
        CHECK(mesh.vertices[i].pos[1] == -1.9f);
    CHECK(mesh.batches[0].blocks == (std::vector<uint16_t>{1, 1}));

    // A road whose texture has a low-detail version: made twice.
    const std::vector<uint16_t> road = {10 << 3, 1, 0x80 | 0 << 3 | 2, 4, 0, 1, 5, 4, 3, 2, 5};
    CHECK(!readPsdl(buildPsdl(road, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f", "r2_lo_f"}), psdl));
    buildCityMesh(psdl, mesh);
    int near = -1, far = -1;
    for (size_t i = 0; i < mesh.batches.size(); ++i) {
        if (mesh.batches[i].detail == 1)
            near = int(i);
        if (mesh.batches[i].detail == 2)
            far = int(i);
    }
    CHECK(near >= 0 && far >= 0 && mesh.batches[size_t(near)].texture == "r2_f");
    if (near >= 0 && far >= 0)
        CHECK(mesh.batches[size_t(far)].texture == "r2_lo_f" &&
              mesh.batches[size_t(far)].indices.size() == mesh.batches[size_t(near)].indices.size());

    // Visibility: blocks 1 and 2; 1 sees both, 2 only itself. Rows are
    // runs: under 0x80, that many of the next byte; from 0x80, 0x7f fewer
    // bytes as they are.
    Bytes pvs;
    pvs.str("PVS0").u32(4).u32(0).u32(2).u32(4).u8(0x80).u8(0x3c).u8(1).u8(0x30);
    Pvs v;
    CHECK(!readCpvs(pvs.v, v) && v.blocks == 3 && v.rows.size() == 2);
    CHECK(v.sees(1, 1) && v.sees(1, 2) && !v.sees(2, 1) && v.sees(2, 2) && !v.sees(1, 0));
    CHECK(v.sees(0, 1) && v.sees(7, 1)); // not covered
    pvs.v[8 + 8] = 9; // the last row ends past the file
    CHECK(readCpvs(pvs.v, v));
    CHECK(readCpvs(toBytes("PVS1"), v));

    // A divided road whose divider (texture 2, a barrier 1 m high) runs
    // from vertices 0-1 to 3-2: raised to its height, sides and ends; a
    // flat one stays on the road.
    for (const uint16_t kind : {uint16_t(3), uint16_t(1)}) {
        const std::vector<uint16_t> divided = {
            10 << 3, 1, 0x80 | 8 << 3 | 2, uint16_t(2 << 8 | kind), 0x100, 0, 0, 0, 1, 1, 1, 3, 3, 3, 2, 2, 2};
        CHECK(!readPsdl(buildPsdl(divided), psdl));
        buildCityMesh(psdl, mesh);
        float top = -1;
        for (const CityMesh::Batch& b : mesh.batches)
            if (b.texture == "swalk_f")
                for (uint32_t i : b.indices)
                    top = std::max(top, mesh.vertices[i].pos[1]);
        CHECK(kind == 3 ? std::fabs(top - 1) < 1e-4f : std::fabs(top) < 1e-4f);
    }

    // Decals: a strip over a square at height 2 in block 5, taking its
    // height and block (the points' own words are no help); an unnamed
    // path places nothing.
    CityMesh ground;
    for (const auto& [x, z] : std::vector<std::pair<float, float>>{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}) {
        gfx::Vertex gv{};
        gv.pos[0] = x;
        gv.pos[1] = 2;
        gv.pos[2] = z;
        ground.vertices.push_back(gv);
    }
    ground.batches.push_back({"r2_f", {0, 1, 2, 0, 2, 3}, {5, 5}, false, false, 0});
    std::vector<PathsetPath> decals(2);
    decals[0].model = "decal_x_inter_l";
    decals[0].points = {{-2, 0, -4}, {2, 0, -4}, {-2, 0, 4}, {2, 0, 4}};
    decals[0].ids = {20978, 20978, 20978, 20978};
    decals[1].model = "PATH44";
    decals[1].points = decals[0].points;
    addCityDecals(decals, ground);
    CHECK(ground.batches.size() == 2 && ground.vertices.size() == 8);
    if (ground.batches.size() == 2) {
        const CityMesh::Batch& d = ground.batches[1];
        CHECK(d.decal && d.texture == "decal_x_inter_l" && d.indices.size() == 12 &&
              d.blocks == (std::vector<uint16_t>{5, 5, 5, 5}));
        for (uint32_t i : d.indices)
            CHECK(std::fabs(ground.vertices[i].pos[1] - 2.03f) < 1e-4f);
        // Across once, along twice (8 m long, 4 wide).
        CHECK(ground.vertices[7].uv[0] == 1 && std::fabs(ground.vertices[7].uv[1] - 2) < 1e-4f);
    }
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

    // The same square in binary (.bbnd): a quad and a triangle (its fourth
    // vertex 0).
    Bytes bin;
    bin.u8(1).u32(4).u32(1).u32(2);
    for (const Vec3& v : {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}})
        bin.f32(v[0]).f32(v[1]).f32(v[2]);
    auto padded = [&](const std::string& t) {
        std::string out = t;
        out.resize(32, '\0');
        bin.str(out);
    };
    padded("grass");
    bin.f32(0.25f).f32(0.75f);
    padded("none");
    padded("0");
    bin.u16(0).u16(1).u16(2).u16(3).u16(0);
    bin.u16(1).u16(2).u16(3).u16(0).u16(0);
    Bound binary;
    CHECK(!readBinaryBound(bin.v, binary));
    CHECK(binary.vertices.size() == 4 && binary.vertices[2] == (Vec3{1, 1, 0}));
    CHECK(binary.materials.size() == 1 && binary.materials[0].name == "grass" &&
          binary.materials[0].elasticity == 0.25f && binary.materials[0].friction == 0.75f);
    CHECK(binary.polygons.size() == 2 && binary.polygons[0].count == 4 && binary.polygons[1].count == 3 &&
          binary.polygons[1].vertices[2] == 3);
    std::vector<uint8_t> cut(bin.v.begin(), bin.v.end() - 4);
    CHECK(readBinaryBound(cut, binary)); // cut short
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
    // Topped out at the top gear's speed (at the redline).
    CHECK(car.gear == setup.gears && car.forwardSpeed() > setup.highSpeed * 0.95f &&
          car.forwardSpeed() < setup.highSpeed * 1.05f);

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

    // Manual gears: no change by itself; up and down as asked; reverse from
    // first only when stopped, the throttle then backing up.
    car.reset(setup, {0, 0, 0}, 0);
    car.manual = true;
    for (int i = 0; i < 60; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.gear == 1 && car.rpm > setup.maxRpm * 0.9f);
    CarControls up = go;
    up.shift = 1;
    car.step(up, flat, 0.1f);
    car.step(go, flat, 0.1f);
    CHECK(car.gear == 2);
    CarControls down;
    down.shift = -1;
    car.step(down, flat, 0.1f);
    car.step(down, flat, 0.1f); // first, but moving: not reverse
    CHECK(car.gear == 1);
    for (int i = 0; i < 100; ++i)
        car.step(stop, flat, 0.1f);
    car.step(down, flat, 0.1f);
    CHECK(car.gear == -1);
    for (int i = 0; i < 20; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.forwardSpeed() < -1);

    // On its roof and stuck: set upright, the way it faced, after
    // stuckTime.
    car.reset(setup, {0, 2, 0}, 0.5f);
    car.manual = false;
    car.orientation = Mat3::rotation({0, 0, 1}, 3.0f) * car.orientation;
    for (int i = 0; i < 10; ++i)
        car.step(none, flat, 0.1f);
    CHECK(car.orientation.y[1] < 0);
    for (int i = 0; i < 40; ++i)
        car.step(none, flat, 0.1f);
    CHECK(car.orientation.y[1] > 0.95f);
}

void testCarFiles() {
    // city/materials.mtl and .csv.
    PhysicsMaterials m;
    CHECK(!m.parse("mtl deepwater {\n\telasticity: 0.5\n\tfriction: 0.65\n\tdrag: 0.5\n\tdepth: 100.0\n}\n"
                   "mtl _default {\n\tfriction: 0.9\n\tsound: 0\n}\n"
                   "mtl grass {\n\tfriction: 0.45\n\tsound: 2\n\tptxindex: 4 -1\n}\n",
                   "texture,physics\r\nS_THAMES,deepwater\r\nlawn,grass\r\nroad,none\r\n"));
    CHECK(m.forTexture("s_thames-0009").name == "deepwater" && m.forTexture("s_thames").depth == 100);
    CHECK(m.forTexture("LAWN").name == "grass" && m.forTexture("road").name == "_default");
    CHECK(m.byName("nothing").name == "_default");
    const CollisionSurface grass = m.surface(m.byName("grass"));
    CHECK(std::fabs(grass.friction - CollisionSurface{}.friction * 0.5f) < 1e-4f && grass.sound == 2);
    CHECK(m.surface(m.byName("deepwater")).drag == 0.5f);
    CHECK(m.parse("mtl open {\nfriction: 1\n", ""));

    // The tyres' and knocks' sound tables.
    SurfaceSounds surfaces;
    CHECK(!readSurfaceSounds("Tunnel sound index,,,\r\n3,,,\r\n"
                             "surface wave,max speed,min surface volume,max surface volume,min surface pitch,max "
                             "surface pitch,min skid volume,max skid volume,num skid samples\r\n"
                             "NOSOUND,125,0,0,0,0,0.5,0.88,2\r\n"
                             "skid wave,min slippage,max slippage\r\n"
                             "tireskid1,0.55,0.65\r\ntireskid2,0.65,1\r\n"
                             "surface wave,max speed\r\nGRAVEL,40,0.2,0.9,0.8,1.2,0.4,0.8,0\r\n",
                             surfaces));
    CHECK(surfaces.tunnel == 3 && surfaces.surfaces.size() == 2);
    if (surfaces.surfaces.size() == 2) {
        CHECK(surfaces.surfaces[0].wave == "NOSOUND" && surfaces.surfaces[0].skids.size() == 2 &&
              surfaces.surfaces[0].skids[1].wave == "tireskid2" && surfaces.surfaces[0].skids[1].maxSlip == 1);
        CHECK(surfaces.surfaces[1].wave == "GRAVEL" && surfaces.surfaces[1].maxSpeed == 40 &&
              surfaces.surfaces[1].skids.empty());
    }
    std::vector<ImpactSounds> impacts;
    CHECK(!readImpactSounds("***\r\nBanger name,Num samples,ID\r\nWALL,2,0\r\n"
                            "sample name,min volume,max volume,min force,max force,frequency\r\n"
                            "soft,0.9,0.93,1000,8000,1\r\nhard,0.94,1,8000,999999,1\r\n"
                            "***\r\nBanger name,Num samples,ID\r\nENDOFDATA\r\n",
                            impacts));
    CHECK(impacts.size() == 1 && impacts[0].name == "WALL" && impacts[0].samples.size() == 2 &&
          impacts[0].samples[1].minForce == 8000 && impacts[0].samples[0].maxVolume == 0.93f);

    // .vehCarDamage: knocks over the threshold count what they're over it.
    DamageSetup d;
    CHECK(!parseDamageSetup("type: a\nvehCarDamage {\n  MaxDamage 10000\n  MedDamage 4000\n  ImpactThreshold 1500\n"
                            "  RegenerateRate 100\n  SmokeOffset -0.13 0.6 -1.65\n  Gravity 15\n"
                            "  TexFrameStart 2\n  Color -167772161\n}\n",
                            d));
    CHECK(d.maxDamage == 10000 && d.smokeOffset[2] == -1.65f && d.smokeOffset2[2] == -1.65f && d.smoke.frame == 2);
    CHECK(std::fabs(d.smoke.color[3] - 0xf5 / 255.0f) < 1e-4f && d.smoke.color[0] == 1);
    CarDamage damage;
    CHECK(damage.knock(d, 1400) == 0 && damage.knock(d, 3500) == 2000 && !damage.smoking(d));
    damage.knock(d, 4000);
    CHECK(damage.smoking(d) && !damage.wrecked(d));
    damage.update(d, 1);
    CHECK(damage.damage() == 4400);
    damage.knock(d, 50000);
    CHECK(damage.wrecked(d) && damage.fraction(d) == 1);

    // tune/<city>.mmHudMap: two-word names come with their second word.
    HudMapSetup h;
    CHECK(!parseHudMapSetup("type: a\nmmHudMap {\n  Size 0.21 0.25\n  Pos 0.78 0.75\n  ZoomIn 0\n"
                            "  Approach Rate 1.2\n  ZoomInDist 577\n  ZoomOutDistFS 1581\n"
                            "  Ocean Color 0.084  0.7 0.94\n}\n",
                            h));
    CHECK(h.width == 0.21f && h.top == 0.75f && !h.zoomIn && h.approachRate == 1.2f && h.zoomInDist == 577 &&
          h.zoomOutDistFS == 1581 && h.ocean[0] == 0.084f && h.ocean[2] == 0.94f);
}

void testDashboardFiles(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    // The Beetle's is .asNode, others' .asnode.
    test::writeFile(tmp / "tune" / "vpbug_dash.asNode",
                    toBytes("type: a\nasNode {\n  DashPos 0 -0.3 -0.9\n  WheelFact 0.75\n  SpeedRotMax 3.614\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    DashSetup dash;
    CHECK(!loadDashSetup(files, "vpbug", dash));
    CHECK(dash.dashPos[1] == -0.3f && dash.wheelFact == 0.75f && dash.speedRotMax == 3.614f && dash.rpmRotMax == 3);
    CHECK(loadDashSetup(files, "vpnone", dash));
}

void testLessons() {
    LessonSettings l;
    CHECK(!parseLessonSettings("Filename,Event,Checkpoints,TimeLimit,AmbDensity,extra,extra,extra,extra,etra,\r\n"
                               "corner0waypoints,4,1,0,0,50,0,0,0,0,0\r\n",
                               l));
    CHECK(l.file == "corner0waypoints" && l.event == LessonEvent::MinimumSpeed && l.timeLimit == 0 && l.extra[0] == 50);
    LessonSettings none;
    CHECK(parseLessonSettings("Filename,Event\r\n", none));
    // A lesson's checkpoints' fifth column is a radius whatever it's called.
    std::vector<Waypoint> w;
    CHECK(!parseWaypoints("x,y,z,a,poly count,frane rate\n0,0,0,0,7,0\n10,0,0,0,9,0\n", w, true) && w[1].radius == 9);
    CHECK(!parseWaypoints("x,y,z,a,poly count,frane rate\n0,0,0,0,7,0\n10,0,0,0,9,0\n", w) && w[1].radius == 0);

    using R = LessonJudge::Result;
    LessonJudge j;
    LessonJudge::Frame f;
    f.dt = 0.1f;
    // Minimum speed: too slow at a checkpoint fails.
    j.start(l);
    CHECK(j.message() == 232 && j.minimumSpeed() == 50);
    f.clock = 1;
    f.speedMph = 55;
    CHECK(j.step(f) == R::Going && j.message() == 237); // up to speed
    f.checkpoint = true;
    f.speedMph = 40;
    CHECK(j.step(f) == R::Failed && j.message() == 238);

    // Clean: each knock 5 s; out of time with the penalties.
    LessonSettings clean;
    clean.event = LessonEvent::Clean;
    clean.timeLimit = 20;
    j.start(clean);
    f = {};
    f.dt = 0.1f;
    f.clock = 2;
    f.impact = 3000;
    CHECK(j.step(f) == R::Going && j.penalty() == 5 && j.message() == 216);
    f.clock = 3;
    CHECK(j.step(f) == R::Going && j.penalty() == 5); // a moment between knocks
    f.impact = 0;
    f.clock = 9;
    CHECK(std::fabs(j.timeLeft(9) - 6) < 1e-4f);
    f.clock = 15.5f;
    CHECK(j.step(f) == R::Failed && j.message() == 214);
    j.start(clean);
    f.clock = 4;
    f.finished = true;
    CHECK(j.step(f) == R::Passed && j.message() == 215);

    // No touching: a car touched after the start fails.
    LessonSettings touch;
    touch.event = LessonEvent::NoTouching;
    j.start(touch);
    f = {};
    f.carTouches = 2; // before the start
    f.clock = 1;
    CHECK(j.step(f) == R::Going);
    f.carTouches = 3;
    CHECK(j.step(f) == R::Failed && j.message() == 230);

    // Cop chase: the end counts once the police are lost; busted fails.
    LessonSettings chase;
    chase.event = LessonEvent::CopChase;
    chase.timeLimit = 100;
    j.start(chase);
    f = {};
    f.dt = 4; // the opening message gone
    f.clock = 10;
    f.finished = true;
    f.chasing = 2;
    CHECK(j.step(f) == R::Going && j.message() == 197);
    f.chasing = 0;
    CHECK(j.step(f) == R::Passed && j.message() == 196);
    j.start(chase);
    f.busted = true;
    CHECK(j.step(f) == R::Failed);

    // Follow: escaped fails; kept up with for the time passes.
    LessonSettings follow;
    follow.event = LessonEvent::Follow;
    follow.timeLimit = 36;
    j.start(follow);
    f = {};
    f.clock = 5;
    f.targetDistance = 50;
    CHECK(j.step(f) == R::Going);
    f.targetDistance = LessonJudge::kEscape + 1;
    CHECK(j.step(f) == R::Failed && j.message() == 222);
    j.start(follow);
    f.targetDistance = 30;
    f.clock = 36;
    CHECK(j.step(f) == R::Passed && j.message() == 221);

    // Gates: in time, or not.
    LessonSettings gates;
    gates.event = LessonEvent::Gates;
    gates.timeLimit = 22;
    j.start(gates);
    f = {};
    f.clock = 22.5f;
    CHECK(j.step(f) == R::Failed && j.message() == 244);
}

void testWidgets() {
    std::vector<Widget> w;
    CHECK(!parseWidgets("MENU NAME,ID,WIDGET NAME,ID,X,Y,W,H,DESC\r\n"
                        "Main Menu,1,dvrcc,0,439,242,230,90,4 State crash\r\n"
                        "Main Menu,1,DRIVER NAME,4,177,128,205,24,\r\n"
                        "short,line\r\n",
                        w));
    CHECK(w.size() == 2 && w[0].menu == "Main Menu" && w[0].name == "dvrcc" && w[0].x == 439 && w[0].h == 90 &&
          w[0].desc == "4 State crash" && w[1].desc.empty());
    CHECK(parseWidgets("MENU NAME,ID\r\n", w));
}

void testRecord() {
    PlayerRecord r;
    CHECK(!r.parse("# a comment\nrace sf checkpoint 2 a 3\nrace sf checkpoint 2 a 1\nlesson london 3\nnonsense\n"));
    CHECK(r.bestPlace("sf", RaceKind::Checkpoint, 2, false) == 1 &&
          r.bestPlace("sf", RaceKind::Checkpoint, 2, true) == 0);
    r.raced("sf", RaceKind::Checkpoint, 2, false, 4); // not better
    CHECK(r.bestPlace("sf", RaceKind::Checkpoint, 2, false) == 1);
    CHECK(r.hasPassed("london", 3) && !r.hasPassed("sf", 3));
    PlayerRecord again;
    CHECK(!again.parse(r.text()) && again.bestPlace("sf", RaceKind::Checkpoint, 2, false) == 1 &&
          again.hasPassed("london", 3));

    // Races open three at a time, each group once every race before is
    // placed 3rd or better; lessons one after another.
    PlayerRecord p;
    CHECK(raceOpen(p, "sf", RaceKind::Blitz, 2, false, 3, 3) && !raceOpen(p, "sf", RaceKind::Blitz, 3, false, 3, 3));
    for (int i = 0; i < 3; ++i)
        p.raced("sf", RaceKind::Blitz, i, false, i == 2 ? 4 : 2);
    CHECK(!raceOpen(p, "sf", RaceKind::Blitz, 3, false, 3, 3));
    p.raced("sf", RaceKind::Blitz, 2, true, 3); // a professional's result counts for amateurs
    CHECK(raceOpen(p, "sf", RaceKind::Blitz, 5, false, 3, 3) && !raceOpen(p, "sf", RaceKind::Blitz, 6, false, 3, 3));
    CHECK(!raceOpen(p, "sf", RaceKind::Blitz, 3, true, 3, 3));
    CHECK(raceOpen(p, "sf", RaceKind::CrashCourse, 0, false, 3, 3) &&
          !raceOpen(p, "sf", RaceKind::CrashCourse, 1, false, 3, 3));
    p.passed("sf", 0);
    CHECK(raceOpen(p, "sf", RaceKind::CrashCourse, 1, false, 3, 3));

    // Unlocking, as the pictures say.
    const RaceCount twelve = [](const std::string&, RaceKind) { return 12; };
    PlayerRecord u;
    CHECK(unlocked(u, "vpbug", -1, false, twelve)); // no rule: open
    CHECK(!unlocked(u, "vpauditt", -1, false, twelve));
    for (int i = 0; i < 6; ++i)
        u.raced("sf", RaceKind::Checkpoint, i, false, 3);
    CHECK(unlocked(u, "vpauditt", -1, false, twelve) && !unlocked(u, "vpauditt", -1, true, twelve));
    CHECK(!unlocked(u, "vpauditt", 4, false, twelve)); // all twelve for the paint job
    CHECK(!unlocked(u, "vp4x4", -1, false, twelve));
    u.passed("sf", 12);
    CHECK(unlocked(u, "vp4x4", -1, false, twelve));
    for (const UnlockRule& rule : unlockRules())
        if (rule.car == "vpauditt" && rule.paintJob < 0) {
            CHECK(unlockText(rule, false) ==
                  "To unlock this vehicle, place 1st, 2nd, or 3rd in six San Francisco Checkpoint Races");
            CHECK(unlockText(rule, true) ==
                  "To unlock this vehicle, finish first in six San Francisco Checkpoint Races");
        } else if (rule.car == "vpcab") {
            CHECK(unlockText(rule, false) ==
                  "To unlock this custom paint job, pass London Crash Course Midterm Exam Two");
        }
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
    CHECK(e.hornVolume == 0.95f && e.clutch == "REVERSE" && e.clutchVolume == 0.93f);
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
    // A road flagged 0x2 isn't for traffic: none starts on it, and it's
    // taken only where there's no other way on (here there isn't).
    right.exceptions.clear();
    m.roads[0].flags = 2;
    net.build(m, right);
    CHECK(!net.lanes()[0].open && !net.lanes()[0].spawns);
    for (size_t i = 0; i < net.lanes().size(); ++i)
        CHECK(!net.waysOn(int(i)).empty());

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

void testAmbient(const std::filesystem::path& tmp) {
    const std::string text = "Min distance,Max distance,3D priority,Audible area,,,,,\r\n"
                             "5,75,12,2,,,,,\r\n"
                             "sample name,sample volume,sample type,oneshot time limit low,oneshot time limit high,"
                             "active,min speed,max speed,Doppler\r\n"
                             "buoybell,0.95,2,5,10,1,0,999999,0\r\n"
                             "streetcable,0.82,0,0,0,1,0,999999,0\r\n"
                             "VECTORPOINTS,,,,,,,,\r\nx,y,z,,,,,,\r\n"
                             "-146.180206,-0.000192,-662.411499,,,,,,\r\n"
                             "-6.795126,-0.000042,-603.490356\r\n";
    Ambience a;
    CHECK(!parseAmbience(text, a));
    CHECK(a.minDistance == 5 && a.maxDistance == 75 && a.priority == 12 && a.area == 2 && a.samples.size() == 2 &&
          a.points.size() == 2);
    if (a.samples.size() == 2 && a.points.size() == 2) {
        CHECK(a.samples[0].name == "buoybell" && std::fabs(a.samples[0].volume - 0.95f) < 1e-5f &&
              a.samples[0].type == 2 && a.samples[0].low == 5 && a.samples[0].high == 10 && a.samples[0].active);
        CHECK(a.samples[1].type == 0 && std::fabs(a.points[1][2] + 603.490356f) < 1e-3f);
    }
    CHECK(ambientGain(a, 3) == 1 && std::fabs(ambientGain(a, 40) - 0.5f) < 1e-5f && ambientGain(a, 80) == 0);
    CHECK(parseAmbience("sample name\n", a));
    CHECK(parseAmbientContainer("file names\r\nbirdies\r\nbuoyseals\r\n") ==
          (std::vector<std::string>{"birdies", "buoyseals"}));

    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "aud" / "ambient" / "testambientcontainer.csv", toBytes("file names\nbuoys\nmissing\n"));
    test::writeFile(tmp / "aud" / "ambient" / "buoys.csv", toBytes(text));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    AmbientSounds sounds;
    CHECK(!sounds.load(files, "test", nullptr) && sounds.ambiences().size() == 1);
    sounds.update(1, {0, 0, 0});
    CHECK(sounds.playing() == 0); // nothing to play them on
    CHECK(sounds.load(files, "elsewhere", nullptr));
}

void testMusic(const std::filesystem::path& tmp) {
    // The tunes' segments by state (aud/dmusic/csv_files): the race file's
    // and the cruising file's columns differ in order.
    const auto race =
        parseMusicSets("Start Music,Return Music,Idle Race Music,Idle Cop Music,Cop chase music,"
                       "Pause Music,Race results Music,Big air Motif style\r\n"
                       "EnemyStart,EnemyReturn,EnemyIdle,EnemyIdleCops,EnemyCops,Pause,Results1,Groover\r\n",
                       true);
    CHECK(race.size() == 1 && race[0].idleCops == "EnemyIdleCops" && race[0].cops == "EnemyCops" &&
          race[0].results == "Results1");
    const auto roam = parseMusicSets("Start Music,Return Music,Idle Music,Cop Chase Music,idle cop music,Pause Music\n"
                                     "SunroofStart,SunRoofReturn,SunRoofIdle,SunRoofCops,SunIdleCops,Pause\n",
                                     false);
    CHECK(roam.size() == 1 && roam[0].cops == "SunRoofCops" && roam[0].idleCops == "SunIdleCops");

    // Which segment it asks for as things happen (the segments themselves
    // aren't there: only the choice is checked).
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "aud" / "dmusic" / "csv_files" / "singleroam.csv",
                    toBytes("Start,Return,Idle,Cops,IdleCops,Pause\nS,R,I,C,IC,P\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    audio::Mixer mixer(22050);
    GameMusic music;
    CHECK(!music.start(files, mixer, false, 0) && music.segment() == "S");
    GameMusic::State st;
    st.speed = 20;
    music.update(1, st);
    CHECK(music.segment() == "S"); // on the move from the start: the start plays on
    st.speed = 0;
    for (int i = 0; i < 5; ++i)
        music.update(1, st);
    CHECK(music.segment() == "I"); // stopped a while
    st.chased = true;
    music.update(1, st);
    CHECK(music.segment() == "IC");
    st.speed = 20;
    music.update(1, st);
    CHECK(music.segment() == "C"); // chased on the move
    st.chased = false;
    music.update(1, st);
    CHECK(music.segment() == "R"); // the chase over
    st.paused = true;
    music.update(1, st);
    CHECK(music.segment() == "P");
    music.stop();
    CHECK(music.segment().empty());
    CHECK(music.start(files, mixer, true, 0)); // no race list
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
void testProps(const std::filesystem::path& tmp) {
    // Placing: points; pairs (where, then a point it faces); a line with a
    // prop every 2 m (end word 20 << 8 | 2).
    std::vector<PathsetPath> paths(3);
    paths[0].model = "sp_cone_f";
    paths[0].points = {{0, 0, 0}, {5, 0, 0}};
    paths[1].model = "sp_dumpstr_f";
    paths[1].end = 1;
    paths[1].points = {{10, 0, 0}, {10, 0, 2}};
    paths[2].model = "sp_barricadewood_f";
    paths[2].end = 20 << 8 | 2;
    paths[2].points = {{0, 0, 10}, {5, 0, 10}};
    auto placed = placeProps(paths);
    CHECK(placed.size() == 2 + 1 + 3);
    // An unnamed path (a race's ramp line) places nothing.
    paths.push_back({});
    paths.back().model = "PATH01";
    paths.back().points = {{0, 0, 0}};
    CHECK(placeProps(paths).size() == placed.size());
    if (placed.size() == 6) {
        CHECK(placed[2].model == "sp_dumpstr_f" && std::fabs(placed[2].along[2] - 1) < 1e-4f);
        CHECK(placed[3].at == (Vec3{0, 0, 10}) && placed[5].at == (Vec3{4, 0, 10}) && placed[4].along[0] == 1);
    }
    // Street furniture: a road in rule 3's block, 100 m along +X, its
    // left side's curb at z -5 and outer edge at -7.
    const auto kinds = parsePropDefs("name,start,distance,maxUse,minLerp,maxLerp,file1,file2,file3,file4,,,\r\n"
                                     "streetlight,9,29,9999,0.1,0.1,sp_lightstreet_f\r\n"
                                     "mailbox,1,1,1,0.2,0.2,sp_mailbox_f\r\n"
                                     "newsboxs,7,10,1,0.1,0.1,sp_newsblue_f,sp_newsred_f,sp_newsyelw_f\r\n");
    CHECK(kinds.size() == 3 && kinds.at("streetlight").start == 9 && kinds.at("streetlight").spacing == 29 &&
          kinds.at("streetlight").most == 9999 && kinds.at("newsboxs").models.size() == 3 &&
          std::fabs(kinds.at("mailbox").minLerp - 0.2f) < 1e-6f);
    const auto rules =
        parsePropRules("rulename,prop1,prop2,,,\r\nn03left,streetlight,,mailbox\r\nn03right,newsboxs\r\n");
    CHECK(rules.size() == 2 && rules.at("n03left") == (std::vector<std::string>{"streetlight", "mailbox"}));
    AiRoadMap street;
    street.roads.resize(2);
    street.roads[0].blocks = {7};
    street.roads[1].blocks = {8}; // rule 0: none
    for (AiRoad& r : street.roads) {
        r.sides[0].curb = {{0, 0, -5}, {100, 0, -5}};
        r.sides[0].outerEdge = {{0, 0, -7}, {100, 0, -7}};
        r.sides[1].curb = {{0, 0, 5}, {100, 0, 5}};
        r.sides[1].outerEdge = {{0, 0, 7}, {100, 0, 7}};
    }
    std::vector<uint8_t> blockRules(9, 0);
    blockRules[7] = 3;
    const auto furniture = placeStreetProps(street, blockRules, kinds, rules);
    // Lights at 9, 38, 67 and 96 m and a mailbox on the left; a news box
    // on the right.
    CHECK(furniture.size() == 4 + 1 + 1);
    if (furniture.size() == 6) {
        CHECK(furniture[0].model == "sp_lightstreet_f" && std::fabs(furniture[0].at[0] - 9) < 1e-3f &&
              std::fabs(furniture[0].at[2] + 5.2f) < 1e-3f && std::fabs(furniture[3].at[0] - 96) < 1e-3f);
        CHECK(std::fabs(furniture[0].along[2] + 1) < 1e-4f); // X away from the road
        CHECK(furniture[4].model == "sp_mailbox_f" && std::fabs(furniture[4].at[2] + 5.4f) < 1e-3f);
        CHECK(furniture[5].model.rfind("sp_news", 0) == 0 && std::fabs(furniture[5].at[2] - 5.2f) < 1e-3f &&
              std::fabs(furniture[5].along[2] - 1) < 1e-4f);
    }

    BangerData b;
    CHECK(!parseBangerData("type: a\ndgBangerData {\n  Size 0.2 10.3 0.2\n  CG 0.18 5.15 0\n  Mass 9.3\n"
                           "  ImpulseLimit2 4657.7\n  NumParts 2\n}\n",
                           b));
    CHECK(std::fabs(b.mass - 9.3f) < 1e-4f && std::fabs(b.impulseLimit2 - 4657.7f) < 0.01f && b.parts == 2 &&
          b.cg[1] == 5.15f && b.size[1] == 10.3f);
    CHECK(parseBangerData("type: a\nother {\n}\n", b));

    // A cone (breaks at sqrt(2500) = 50 kg m/s) and a tree (never) in the
    // way of a car on a flat plane.
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "city" / "test" / "props.pathset",
                    buildPathset({{"sp_cone_f", {{0, 0, -30}}, 0}, {"sp_tree_s", {{6, 0, -30}}, 0}}));
    // The cone has two pieces: a base (its middle 0.1 up) and a top.
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Mass 10\n  ImpulseLimit2 2500\n  NumParts 2\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f_BREAK01.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Size 0.6 0.2 0.6\n  CG 0 0.1 0\n  Mass 4\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_cone_f_BREAK02.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Size 0.3 0.6 0.3\n  CG 0 0.5 0\n  Mass 6\n}\n"));
    test::writeFile(tmp / "tune" / "banger" / "sp_tree_s.dgBangerData",
                    toBytes("type: a\ndgBangerData {\n  Mass 50000\n  ImpulseLimit2 1e+30\n}\n"));
    // A race's own (race/<map>/<race>.pathset) besides the city's.
    test::writeFile(tmp / "race" / "test" / "circuit0.pathset",
                    buildPathset({{"sp_barricadeconcl_f", {{0, 0, 50}, {4, 0, 50}}, 20 << 8 | 2}}));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    Props props;
    props.load(files, "test", "circuit0");
    CHECK(props.count() == 2 + 3);
    props.load(files, "test");
    CHECK(props.count() == 2);
    props.prepare(files, [](const std::string& model, Vec3& lo, Vec3& hi) {
        lo = model == "sp_tree_s" ? Vec3{-0.5f, -3, -0.5f} : Vec3{-0.3f, -0.4f, -0.3f};
        hi = {-lo[0], -lo[1], -lo[2]};
        return true;
    });
    CollisionWorld flat;
    flat.add({-300, 0, -300}, {-300, 0, 300}, {300, 0, 300});
    flat.add({-300, 0, -300}, {300, 0, 300}, {300, 0, -300});
    flat.build();
    props.collide(flat);
    CollisionWorld::RayHit hit;
    CHECK(flat.raycast({0, 5, -30}, {0, -1, 0}, 10, hit) && hit.surface.prop == 0 &&
          std::fabs(hit.distance - 4.2f) < 0.01f);
    CHECK(hit.surface.breakImpulse == 50 && hit.surface.propMass == 10);
    CHECK(flat.raycast({6, 10, -30}, {0, -1, 0}, 20, hit) && hit.surface.prop == 1 && hit.surface.breakImpulse == 0);

    // Driven through: the cone comes loose and falls out of the way; the
    // car goes on, a little slower.
    TrafficLights lights;
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0);
    CarControls go;
    go.throttle = 1;
    float speedBefore = 0;
    for (int i = 0; i < 160 && car.position[2] > -40; ++i) {
        if (car.position[2] > -26)
            speedBefore = car.forwardSpeed();
        car.step(go, flat, 0.05f);
        props.update(0.05f, {&car}, flat, lights);
    }
    CHECK(props.loose() == 1 && car.position[2] < -35 && car.forwardSpeed() > speedBefore * 0.8f);
    CHECK(!flat.raycast({0, 5, -30}, {0, -1, 0}, 10, hit) || hit.surface.prop < 0);
    // It came apart: both pieces fly off and come to rest on the ground.
    for (int i = 0; i < 100; ++i)
        props.update(0.05f, {}, flat, lights);
    size_t resting = 0;
    float lowest = 0;
    CHECK(props.pieces(&resting, &lowest) == 2 && resting == 2 && lowest > 0.05f && lowest < 0.5f);
    // The tree stops a car.
    car.reset(testCar(), {6, 0, 0}, 0);
    for (int i = 0; i < 80; ++i) {
        car.step(go, flat, 0.05f);
        props.update(0.05f, {&car}, flat, lights);
    }
    CHECK(props.loose() == 1 && car.position[2] > -30);
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
    testCarFiles();
    testLessons();
    testRecord();
    testWidgets();
    testDashboardFiles(tmp / "dash");
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
    testTunnels();
    testCityExtras();
    testTrains(tmp / "trains");
    testProps(tmp / "props");
    testAmbient(tmp / "ambient");
    testMusic(tmp / "music");
    std::filesystem::remove_all(tmp);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all age tests passed\n");
    return 0;
}
