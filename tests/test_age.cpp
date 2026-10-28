// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Angel Game Engine's readers against files built here,
// following docs/formats/age/, so no game data is needed.

#include "engines/age/ar_archive.h"
#include "engines/age/city_mesh.h"
#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/instances.h"
#include "engines/age/pkg.h"
#include "engines/age/psdl.h"
#include "engines/age/texture.h"
#include "engines/age/tune.h"
#include "engines/age/viewers.h"
#include "zip_writer.h"

#include <cstdio>
#include <algorithm>
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

} // namespace

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
    std::filesystem::remove_all(tmp);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all age tests passed\n");
    return 0;
}
