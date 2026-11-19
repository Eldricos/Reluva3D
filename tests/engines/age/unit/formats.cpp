// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: the game's files read: archives, textures, models, cities, races, road maps, path sets.
// Every file is built in the test.

#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testArchive(const std::filesystem::path &tmp) {
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

void testMount(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    std::vector<std::string> warnings;
    SearchSet set;
    std::filesystem::create_directories(tmp);
    CHECK(mountGame(set, tmp, warnings)); // no archives

    test::writeFile(tmp / "mm2core.ar", buildAr({{"tune/a.txt", toBytes("core"), {}},
                                                 {"tune/b.txt", toBytes("core"), {}},
                                                 {"tune/c.txt", toBytes("core"), {}}}));
    test::writeFile(tmp / "VPMOD.AR",
                    buildAr({{"tune/b.txt", toBytes("mod"), {}}, {"tune/c.txt", toBytes("mod"), {}}}));
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

void testTex() {
    // 2x2 palette texture, two levels (2x2 and 1x1): index 1 is
    // B=10 G=20 R=30 with alpha 40, which only format 14 uses.
    Bytes pal;
    pal.bytes(texHeader(2, 2, texformat::kPalette, 2, 0x10001));
    for (int i = 0; i < 256; ++i)
        pal.u8(i == 1 ? 10 : 0).u8(i == 1 ? 20 : 0).u8(i == 1 ? 30 : 0).u8(i == 1 ? 40 : 255);
    pal.u8(0).u8(1).u8(1).u8(0).u8(1);
    Tex tex;
    CHECK(!decodeTex(pal.v, tex));
    CHECK(tex.image.w == 2 && tex.image.h == 2 && tex.levels == 2 && tex.flags == 0x10001);
    CHECK(tex.image.rgba == (std::vector<uint8_t>{0, 0, 0, 255, 30, 20, 10, 255, 30, 20, 10, 255, 0, 0, 0, 255}));
    pal.v[4] = texformat::kPaletteAlpha;
    CHECK(!decodeTex(pal.v, tex) && tex.image.pixel(1, 0)[3] == 40);
    pal.v.pop_back(); // the 1x1 level cut short
    CHECK(decodeTex(pal.v, tex));

    // 24-bit and 32-bit: R, G, B(, A) in the file, unlike the palettes.
    Bytes rgb;
    rgb.bytes(texHeader(1, 1, texformat::kRgb, 1, 0)).u8(1).u8(2).u8(3);
    CHECK(!decodeTex(rgb.v, tex) && tex.image.rgba == (std::vector<uint8_t>{1, 2, 3, 255}));
    Bytes rgba;
    rgba.bytes(texHeader(1, 1, texformat::kRgba, 1, 0)).u8(1).u8(2).u8(3).u8(4);
    CHECK(!decodeTex(rgba.v, tex) && tex.image.rgba == (std::vector<uint8_t>{1, 2, 3, 4}));

    CHECK(decodeTex(texHeader(1, 1, 99, 1, 0), tex)); // unknown format
    CHECK(decodeTex(texHeader(0, 1, texformat::kRgb, 1, 0), tex));
    CHECK(decodeTex(toBytes("short"), tex));
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
    const PkgMesh *body = model.mesh("BODY_H");
    CHECK(body && body->part() == "BODY" && body->level() == "H" && body->fvf == 0x112);
    CHECK(body && body->groups.size() == 1 && body->groups[0].indices == (std::vector<uint16_t>{0, 1, 2}));
    if (body && !body->groups.empty()) {
        const PkgVertex &v = body->groups[0].vertices[2];
        CHECK(v.pos == (Vec3{2, 4, -2}) && v.normal == (Vec3{0, 1, 0}) && v.uv[0] == 0.5f && v.uv[1] == 0.5f);
    }
    const PkgMesh *h = model.mesh("H");
    CHECK(h && h->part().empty() && h->level() == "H" && h->groups[0].vertices[1].normal == (Vec3{0, 0, 0}));
    CHECK(model.paintJobs == 2 && model.shadersPerJob == 1 && model.shaders.size() == 2);
    const PkgShader *red = model.shader(0, 0);
    CHECK(red && red->texture == "red_body" && red->diffuse == (std::array<float, 4>{1, 0, 0, 1}));
    CHECK(red && red->ambient[0] > 0.5f && red->ambient[0] < 0.51f && red->shininess == 0.5f);
    const PkgShader *glow = model.shader(1, 0);
    CHECK(glow && glow->texture.empty() && glow->diffuse[3] == 0 && glow->emissive[2] > 0.11f &&
          glow->emissive[2] < 0.12f);
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
    CHECK(!readMtx(mtx.v, m) && m.min == (Vec3{0, 1, 2}) && m.pivot == (Vec3{6, 7, 8}) &&
          m.origin == (Vec3{9, 10, 11}));
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

void testPsdl() {
    // Texture 1 (r2_f); a roof at height 0 (10) over 0, 1, 2, 3; a facade
    // from height 0 to 1 (10 to 20) along 0-1, repeated 2 x 3, marked last;
    // then a stray texture attribute, as the retail cities have.
    const std::vector<uint16_t> attrs = {10 << 3, 1, 12 << 3 | 3, 0, 0, 1, 2,       3, 0x80 | 11 << 3 | 6,
                                         0,       1, 2,           3, 0, 1, 10 << 3, 2};
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl(attrs), psdl));
    CHECK(psdl.vertices.size() == 6 && psdl.heights.size() == 3 && psdl.textures.size() == 2);
    CHECK(psdl.texture(1) == "r2_f" && psdl.texture(2) == "swalk_f" && psdl.texture(0).empty() &&
          psdl.texture(3).empty());
    CHECK(psdl.blocks.size() == 2 && psdl.blocks[1].perimeter.size() == 2 && psdl.blocks[1].perimeter[1].second == 5);
    CHECK(psdl.blocks[1].attributes.size() == 4 && psdl.blocks[1].flags == 4 && psdl.blocks[0].extra == 205);
    if (psdl.blocks.size() == 2 && psdl.blocks[1].attributes.size() > 1) {
        const PsdlAttribute &roof = psdl.blocks[1].attributes[1];
        CHECK(roof.type == psdlattr::kRoof && roof.count == 3 && roof.words == (std::vector<uint16_t>{0, 0, 1, 2, 3}));
    }
    CHECK(psdl.min == (Vec3{-1, -2, -3}) && psdl.center == (Vec3{1, 2, 3}) && psdl.radius == 9);

    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    CHECK(mesh.batches.size() == 1 && mesh.batches[0].texture == "r2_f");
    CHECK(mesh.batches[0].indices.size() == 4 * 3); // two roof triangles, two facade triangles
    float lowest = 1e9f, highest = -1e9f, maxU = 0;
    for (const gfx::Vertex &v : mesh.vertices) {
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
    CHECK(psdlAttributeLength(psdlattr::kDividedRoad, 2) == 14 && psdlAttributeLength(psdlattr::kTunnel, 5) == 5);

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
        const PsdlRoad &r = psdl.roads[0];
        CHECK(r.flags == 0x440 && r.extra == 1 && r.lanes[0] == 1 && r.lanes[1] == 1);
        CHECK(r.lanePositions == (std::vector<float>{0.25f, 0.75f}) && r.ends[0] == 1 && r.ends[1] == 0x80);
        CHECK(r.vertices[7] == 7 && r.blocks == (std::vector<int32_t>{1, -1}));
    }
    roads.v.pop_back();
    CHECK(readPsdl(buildPsdl(attrs, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f"}, roads.v), psdl)); // cut short
    roads.u8(0xff).u8(0);
    CHECK(readPsdl(buildPsdl(attrs, 2, {{0, 0}, {1, 5}}, {"r2_f", "swalk_f"}, roads.v), psdl)); // a byte left over
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
    const TuneNode &n = root.children[0];
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
    CHECK(readBound("mtl a {\nfriction: 1\n", b));    // not closed

    // The same square in binary (.bbnd): a quad and a triangle (its fourth
    // vertex 0).
    Bytes bin;
    bin.u8(1).u32(4).u32(1).u32(2);
    for (const Vec3 &v : {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}})
        bin.f32(v[0]).f32(v[1]).f32(v[2]);
    auto padded = [&](const std::string &t) {
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

void testSidewalkMarkers() {
    // A two-pair sidewalk strip starting (0, 0), as 188 in the cities do:
    // not a strip from vertex 0, but the sidewalk's end face over its one
    // section; nothing where the section is level.
    Psdl psdl;
    CHECK(!readPsdl(buildPsdl({10 << 3, 1, 0x80 | 1 << 3 | 2, 0, 0, 2, 3}), psdl));
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    CHECK(mesh.vertices.empty());
    // From the curb (vertex 0, at 0) to the outer edge (vertex 4, 0.15 m
    // up, 2 m along -z): a face 0.15 m high, facing +x for (1, 1), -x for
    // (0, 0), away from the sidewalk.
    for (const uint16_t marker : {uint16_t(1), uint16_t(0)}) {
        CHECK(!readPsdl(buildPsdl({10 << 3, 1, 0x80 | 1 << 3 | 2, marker, marker, 0, 4}), psdl));
        buildCityMesh(psdl, mesh);
        CHECK(mesh.vertices.size() == 6);
        float top = 0;
        for (const gfx::Vertex &v : mesh.vertices) {
            CHECK(marker == 1 ? v.normal[0] > 0.99f : v.normal[0] < -0.99f);
            top = std::max(top, v.pos[1]);
        }
        CHECK(std::fabs(top - 0.15f) < 1e-5f);
    }
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
    CHECK(!parseWaypoints(
        "x,y,z,a,poly count,frane rate,state changes,texture changes,msg\n1,2,3,-90,11,0,0,0,\n4,5,6,0,11,0,0,0,\n",
        w));
    CHECK(w[0].radius == 0);                                 // circuits: a poly count, not a radius
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

void testBai() {
    const auto data = buildBai({{{0, 0, 0}, {100, 0, 0}, 0, 1, 2, 1, 3}}, {{0, 0, 0}, {100, 0, 0}});
    AiRoadMap m;
    CHECK(!readBai(data, m));
    CHECK(m.roads.size() == 1 && m.junctions.size() == 2);
    const AiRoad &r = m.roads[0];
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
    for (const TrafficLane &l : net.lanes()) {
        const bool forward = l.side == 1;
        CHECK(l.from == (forward ? 0u : 1u) && l.to == (forward ? 1u : 0u));
        CHECK(std::fabs(l.points.front()[0] - (forward ? 0 : 100)) < 1e-3f && std::fabs(l.length() - 100) < 1e-3f);
    }
    CHECK(net.leaving(0).size() == 2 && net.leaving(1).size() == 1);
    AiMapSettings left;
    left.driveOnLeft = true;
    net.build(m, left);
    for (const TrafficLane &l : net.lanes())
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
    CHECK(s.opponents.size() == 2 && s.opponents[1].model == "vpcaddie" &&
          std::fabs(s.opponents[0].skill - 0.96f) < 1e-5f);
    CHECK(parseAiMap("nothing here", s));
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
    auto near = [](const Vec3 &a, const Vec3 &b) { return length(a - b) < 1e-5f; };
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

void testPathset() {
    const std::vector<uint8_t> data = buildPathset({{"giz_ferry_f", {{0, 0, 0}, {10, 0, 0}}, 0x6402},
                                                    {"open:giz_bridge01_l", {{0, 8, 0}, {30, 8, 0}}, 0x0801},
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
    for (const gfx::Vertex &v : mesh.vertices) {
        crown = std::max(crown, v.pos[1]);
        if (v.pos[0] == 0)
            wallTop = std::max(wallTop, v.pos[1]);
    }
    CHECK(std::fabs(crown - 7) < 1e-4f && std::fabs(wallTop - 5.5f) < 1e-4f);
    // The walls' texture runs up them from its first row at the foot.
    int foot = 0, head = 0;
    for (const gfx::Vertex &v : mesh.vertices)
        if (std::fabs(v.normal[1]) < 1e-3f && std::fabs(v.normal[0]) > 0.999f) {
            if (v.pos[1] == 0)
                foot += v.uv[1] == 0;
            else if (std::fabs(v.pos[1] - 5.5f) < 1e-4f)
                head += v.uv[1] == 1;
        }
    CHECK(foot > 0 && head > 0);
    // Two walls, eight pieces of arch, the strip, half the start's end each
    // side (four pieces each).
    CHECK(mesh.batches.size() == 1 && mesh.batches[0].indices.size() == (4 + 16 + 2 + 8) * 3);
    // Flag 0x4: a band 2 m deep (the second height) hanging under the
    // deck's edges, besides the 5 m walls (the freeway's 0x7).
    CHECK(!readPsdl(buildPsdl({10 << 3, 1, 9 << 3 | 3, 0x7, 0x500, 0x200, 0x80 | 2 << 3 | 2, 0, 1, 3, 2}), psdl));
    buildCityMesh(psdl, mesh);
    float lowest = 1;
    for (const gfx::Vertex &v : mesh.vertices)
        lowest = std::min(lowest, v.pos[1]);
    CHECK(std::fabs(lowest + 2) < 1e-4f);
    // The lower height is the walls' either way round (San Francisco's
    // tunnels: 9 then 7).
    CHECK(!readPsdl(buildPsdl({10 << 3, 1, 9 << 3 | 3, 0x103, 0x900, 0x700, 0x80 | 2 << 3 | 2, 0, 1, 3, 2}), psdl));
    buildCityMesh(psdl, mesh);
    crown = wallTop = -1;
    for (const gfx::Vertex &v : mesh.vertices) {
        crown = std::max(crown, v.pos[1]);
        if (v.pos[0] == 0)
            wallTop = std::max(wallTop, v.pos[1]);
    }
    CHECK(std::fabs(crown - 9) < 1e-4f && std::fabs(wallTop - 7) < 1e-4f);

    // A junction: walls along the edges the mask names (bit i: the edge
    // ending at outline vertex i), a flat ceiling at 5.5 over the block.
    const std::vector<uint16_t> junction = {
        10 << 3, 1, 9 << 3, 10, 0xb, 0x580, 0x700, 0, 0b1010, 0, 0, 0, 0, 0, 0x80 | 6 << 3 | 2, 0, 1, 2, 3};
    CHECK(!readPsdl(buildPsdl(junction, 2, {{0, 0}, {1, 7}, {2, 0}, {3, 7}}), psdl));
    buildCityMesh(psdl, mesh);
    size_t ceiling = 0, walls = 0;
    for (size_t i = 0; i + 2 < mesh.batches[0].indices.size(); i += 3) {
        const gfx::Vertex &a = mesh.vertices[mesh.batches[0].indices[i]];
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
    pvs.v[8 + 8] = 9;                    // the last row ends past the file
    CHECK(readCpvs(pvs.v, v));
    CHECK(readCpvs(toBytes("PVS1"), v));

    // A divided road whose divider (texture 2, a barrier 1 m high) runs
    // from vertices 0-1 to 3-2: raised to its height with sides, its ends
    // closed only where 0x40 and 0x80 say; a flat one stays on the road.
    size_t open = 0, closed = 0;
    for (const uint16_t kind : {uint16_t(3), uint16_t(0xc3), uint16_t(1)}) {
        const std::vector<uint16_t> divided = {
            10 << 3, 1, 0x80 | 8 << 3 | 2, uint16_t(2 << 8 | kind), 0x100, 0, 0, 0, 1, 1, 1, 3, 3, 3, 2, 2, 2};
        CHECK(!readPsdl(buildPsdl(divided), psdl));
        buildCityMesh(psdl, mesh);
        float top = -1;
        for (const CityMesh::Batch &b : mesh.batches)
            if (b.texture == "swalk_f")
                for (uint32_t i : b.indices)
                    top = std::max(top, mesh.vertices[i].pos[1]);
        CHECK((kind & 3) == 3 ? std::fabs(top - 1) < 1e-4f : std::fabs(top) < 1e-4f);
        for (const CityMesh::Batch &b : mesh.batches)
            if (b.texture == "swalk_f")
                (kind == 3 ? open : closed) += kind == 1 ? 0 : b.indices.size();
    }
    CHECK(closed == open + 12); // two end quads

    // Decals: a strip over a square at height 2 in block 5, taking its
    // height and block (the points' own words are no help); an unnamed
    // path places nothing.
    CityMesh ground;
    for (const auto &[x, z] : std::vector<std::pair<float, float>>{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}) {
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
        const CityMesh::Batch &d = ground.batches[1];
        CHECK(d.decal && d.texture == "decal_x_inter_l" && d.indices.size() == 12 &&
              d.blocks == (std::vector<uint16_t>{5, 5, 5, 5}));
        for (uint32_t i : d.indices)
            CHECK(std::fabs(ground.vertices[i].pos[1] - 2.03f) < 1e-4f);
        // Across once, along twice (8 m long, 4 wide).
        CHECK(ground.vertices[7].uv[0] == 1 && std::fabs(ground.vertices[7].uv[1] - 2) < 1e-4f);
    }
}

void testDashboardFiles(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    // The Beetle's is .asNode, others' .asnode.
    test::writeFile(tmp / "tune" / "vpbug_dash.asNode",
                    toBytes("type: a\nasNode {\n  DashPos 0 -0.3 -0.9\n  WheelFact 0.75\n  SpeedRotMax 3.614\n}\n"));
    // A following camera: it catches up slowly when the car is slow, and
    // backing for RevDelay seconds it swings round to the car's front.
    test::writeFile(tmp / "tune" / "camera" / "vpbug_near.camTrackCS",
                    toBytes("type: a\ncamTrackCS {\n  Offset 0 1 4\n  MinAppXZPos 1.5\n  MaxAppXZPos 8\n"
                            "  MaxSpeed 15\n  AppInc 2.5\n  AppDec 10\n  ApproachOn 1\n  ReverseOn 1\n"
                            "  RevDelay 2\n  RevOnApp 2\n  RevOffApp 4\n  HillMin -0.5\n  HillMax 0.5\n"
                            "  HillLerp 0.1\n  TrackTo 0 1.7 0\n  MaxDist 5.3\n  MinDist 3.9\n}\n"));
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    DashSetup dash;
    CHECK(!loadDashSetup(files, "vpbug", dash));
    CHECK(dash.dashPos[1] == -0.3f && dash.wheelFact == 0.75f && dash.speedRotMax == 3.614f && dash.rpmRotMax == 3);
    CHECK(loadDashSetup(files, "vpnone", dash));

    const std::vector<CarCamera> cams = loadCarCameras(files, "vpbug");
    CHECK(cams.size() == 1 && cams[0].approachSlow == 1.5f && cams[0].approachFast == 8 && cams[0].reverse &&
          cams[0].reverseDelay == 2 && cams[0].hillMax == 0.5f);
    CarSim car;
    car.reset(testCar(), {0, 0, 0}, 0); // facing -z
    CameraFollow follow;
    updateCarCamera(cams[0], car, 0.05f, follow, true);
    CHECK(follow.position[2] > 3.8f && follow.rate == 1.5f);
    car.velocity = {0, 0, 4}; // backing
    for (int i = 0; i < 30; ++i)
        updateCarCamera(cams[0], car, 0.05f, follow, false);
    CHECK(!follow.backwards && follow.position[2] > 3.8f);
    for (int i = 0; i < 100; ++i)
        updateCarCamera(cams[0], car, 0.05f, follow, false);
    CHECK(follow.backwards && follow.position[2] < -3.8f);

    // The game's view controls: the Thrill Cam stands ahead of the car,
    // looking back at it; Look Back in the car looks behind; Wide Angle
    // widens and hides the dashboard; Camera Pan goes round while held.
    {
        ViewControls vc;
        CarSim still;
        still.reset(testCar(), {0, 0, 0}, 0); // facing -z
        CarCamera inside;
        inside.inside = true;
        const CameraView ahead{{0, 1, 0}, {0, 1, -1}, {0, 1, 0}};
        CHECK(vc.press(Control::kThrillCam, true) && vc.thrill);
        CameraView t = vc.apply(ahead, cams[0], still, 0.05f);
        CHECK(t.eye[2] < -30 && t.target[2] > t.eye[2]);
        vc.press(Control::kThrillCam, true);
        CHECK(vc.press(Control::kLookBack, true));
        t = vc.apply(ahead, inside, still, 0.05f);
        CHECK(t.target[2] - t.eye[2] > 0.9f);
        CHECK(vc.press(Control::kWideAngle, true) && vc.fov(70) > 90 && !vc.showDashboard());
        CHECK(!vc.press(Control::kThrottle, true));
        vc.press(Control::kLookForward, true);
        CameraView behind{{0, 2, 4}, {0, 1, 0}, {0, 1, 0}};
        vc.press(Control::kCameraPan, true);
        for (int i = 0; i < 10; ++i)
            t = vc.apply(behind, cams[0], still, 0.1f);
        CHECK(std::fabs(t.eye[0]) > 2 &&
              std::fabs(length(t.eye - t.target) - length(behind.eye - behind.target)) < 1e-3f);
    }
}

} // namespace

static const reluva::test::Suite kSuite("age.unit.formats",
    {
        {"archive", [](const std::filesystem::path &tmp) { testArchive(tmp); }},
        {"mount", [](const std::filesystem::path &tmp) { testMount(tmp); }},
        {"file lines", [](const std::filesystem::path &) { testFileLines(); }},
        {"tex", [](const std::filesystem::path &) { testTex(); }},
        {"pkg", [](const std::filesystem::path &) { testPkg(); }},
        {"names", [](const std::filesystem::path &) { testNames(); }},
        {"psdl", [](const std::filesystem::path &) { testPsdl(); }},
        {"instances", [](const std::filesystem::path &) { testInstances(); }},
        {"tune", [](const std::filesystem::path &) { testTune(); }},
        {"bound", [](const std::filesystem::path &) { testBound(); }},
        {"sidewalk markers", [](const std::filesystem::path &) { testSidewalkMarkers(); }},
        {"race files", [](const std::filesystem::path &) { testRaceFiles(); }},
        {"bai", [](const std::filesystem::path &) { testBai(); }},
        {"ai map", [](const std::filesystem::path &) { testAiMap(); }},
        {"ped files", [](const std::filesystem::path &) { testPedFiles(); }},
        {"pathset", [](const std::filesystem::path &) { testPathset(); }},
        {"tunnels", [](const std::filesystem::path &) { testTunnels(); }},
        {"city extras", [](const std::filesystem::path &) { testCityExtras(); }},
        {"dashboard files", [](const std::filesystem::path &tmp) { testDashboardFiles(tmp); }},
    });
