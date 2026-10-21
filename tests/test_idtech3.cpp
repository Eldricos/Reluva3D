// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the id Tech 3 engine's readers, on data built here.

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/shader.h"
#include "engines/registry.h"
#include "zip_writer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void testLexer() {
    Lexer lex("first \"two words\" // comment\n/* block\ncomment */ third\n  fourth fifth\n{");
    CHECK(lex.next() == "first");
    CHECK(lex.next() == "two words");
    CHECK(!lex.next(false)); // the rest of the line is a comment
    CHECK(lex.peek() == "third" && lex.next() == "third" && lex.line() == 3);
    CHECK(lex.next() == "fourth");
    lex.skipLine();
    CHECK(lex.next() == "{" && lex.line() == 5);
    CHECK(!lex.next() && lex.atEnd());

    Lexer block("a { b { c } d } e");
    CHECK(block.next() == "a" && block.next() == "{" && block.skipBlock() && block.next() == "e");
}

static void testEntities() {
    const std::string lump = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube\"\n}\n"
                             "{\n\"classname\" \"info_player_start\"\n\"origin\" \"64 -32 24\"\n\"ORIGIN\" \"0 0 0\"\n}\n";
    std::vector<Entity> ents;
    CHECK(!parseEntities(lump + std::string(1, '\0') + "junk", ents));
    CHECK(ents.size() == 2);
    CHECK(ents[0].classname() == "worldspawn" && ents[0].get("Message") == "Borg Cube");
    float origin[3];
    CHECK(ents[1].vector("origin", origin) && origin[0] == 64 && origin[1] == -32 && origin[2] == 24);
    CHECK(!ents[1].vector("angles", origin) && ents[1].get("angles", "0") == "0");
    CHECK(parseEntities("{ \"classname\" }", ents)); // a key without a value
    CHECK(parseEntities("\"classname\" \"x\"", ents));
    CHECK(parseEntities("{ \"a\" \"b\"", ents));
}

static void testShaderStages() {
    const std::string text = R"(
textures/effects/panel
{
    cull none
    deformVertexes wave 64 sin 0 3 0 0.5
    deformVertexes move 0 0 1 sawtooth 0 8 0 1
    deformVertexes autosprite
    sort additive
    polygonOffset
    nomipmaps
    efMadeUpDirective 1 2
    {
        map textures/effects/a.tga
        rgbGen wave triangle 0.5 0.5 0.25 2
        alphaGen const 0.4
        tcGen vector ( 0.01 0 0 ) ( 0 0.01 0 )
        tcMod scroll 0.5 -1
        tcMod turb 0 0.1 0 0.3
        tcMod rotate 90
        tcMod stretch sin 1 0.2 0 1
        tcMod transform 1 0 0 1 0.5 0.25
        depthFunc equal
        depthWrite
        alphaFunc GE128
    }
    {
        map $whiteimage
        rgbGen const ( 0.25 0.5 1 )
        alphaGen oneMinusVertex
        tcGen environment
        tcMod turb sin 0 0.2 0.5 1
        efStageThing
    }
}
env/sky
{
    skyParms env/space - -
    fogParms ( 0.5 0.25 0 ) 300
    sort 7
}
)";
    std::vector<ShaderDef> defs;
    CHECK(!parseShaderFile(text, "x.shader", defs));
    CHECK(defs.size() == 2);
    if (defs.size() != 2 || defs[0].stages.size() != 2)
        return;
    const ShaderDef& d = defs[0];
    CHECK(d.cull == ShaderDef::Cull::None && d.sort == 10 && d.polygonOffset && d.noMipmaps);
    CHECK(d.deforms.size() == 3);
    CHECK(d.deforms[0].type == ShaderDeform::Type::Wave && d.deforms[0].spread == 1.0f / 64);
    CHECK(d.deforms[0].wave.amplitude == 3 && d.deforms[0].wave.frequency == 0.5f);
    CHECK(d.deforms[1].type == ShaderDeform::Type::Move && d.deforms[1].vector[2] == 1 &&
          d.deforms[1].wave.func == gfx::WaveFunc::Sawtooth && d.deforms[1].wave.amplitude == 8);
    CHECK(d.deforms[2].type == ShaderDeform::Type::Autosprite);
    CHECK((d.unknown == std::vector<std::string>{"efmadeupdirective", "efstagething"}));

    const ShaderStage& a = d.stages[0];
    CHECK(a.rgbGen == ShaderStage::RgbGen::Wave && a.rgbWave.func == gfx::WaveFunc::Triangle &&
          a.rgbWave.phase == 0.25f && a.rgbWave.frequency == 2);
    CHECK(a.alphaGen == ShaderStage::AlphaGen::Const && a.alphaConst == 0.4f);
    CHECK(a.tcGen == ShaderStage::TcGen::Vector && a.tcVectors[0][0] == 0.01f && a.tcVectors[1][1] == 0.01f);
    CHECK(a.tcMods.size() == 5);
    if (a.tcMods.size() == 5) {
        using M = ShaderStage::TcMod::Type;
        CHECK(a.tcMods[0].type == M::Scroll && a.tcMods[0].v[0] == 0.5f && a.tcMods[0].v[1] == -1);
        CHECK(a.tcMods[1].type == M::Turb && a.tcMods[1].wave.amplitude == 0.1f && a.tcMods[1].wave.frequency == 0.3f);
        CHECK(a.tcMods[2].type == M::Rotate && a.tcMods[2].v[0] == 90);
        CHECK(a.tcMods[3].type == M::Stretch && a.tcMods[3].wave.base == 1 && a.tcMods[3].wave.amplitude == 0.2f);
        CHECK(a.tcMods[4].type == M::Transform && a.tcMods[4].v[4] == 0.5f && a.tcMods[4].v[5] == 0.25f);
    }
    CHECK(a.depthEqual && a.depthWrite && a.alphaFunc == gfx::AlphaTest::Ge128);

    const ShaderStage& b = d.stages[1];
    CHECK(b.whiteImage && b.rgbGen == ShaderStage::RgbGen::Const && b.rgbConst[1] == 0.5f && b.rgbConst[2] == 1);
    CHECK(b.alphaGen == ShaderStage::AlphaGen::OneMinusVertex && b.tcGen == ShaderStage::TcGen::Environment);
    // turb with the (ignored) function name written
    CHECK(b.tcMods.size() == 1 && b.tcMods[0].wave.amplitude == 0.2f && b.tcMods[0].wave.phase == 0.5f);

    const ShaderDef& sky = defs[1];
    CHECK(sky.sky && sky.skyFarBox == "env/space" && sky.cloudHeight == 512 && sky.sort == 7);
    CHECK(sky.fog && sky.fogColor[0] == 0.5f && sky.fogColor[1] == 0.25f && sky.fogDistance == 300);
}

static void testWaves() {
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    gfx::Wave w{gfx::WaveFunc::Sin, 1, 2, 0, 1};
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::Triangle;
    CHECK(approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.5f), 1) && approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::Square;
    CHECK(approx(gfx::evaluate(w, 0.1f), 3) && approx(gfx::evaluate(w, 0.6f), -1));
    w.func = gfx::WaveFunc::Sawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 1.5f) && approx(gfx::evaluate(w, 1.25f), 1.5f));
    w.func = gfx::WaveFunc::InverseSawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 2.5f));
    w = {gfx::WaveFunc::Sin, 0, 1, 0.25f, 0}; // no frequency: constant, set by the phase
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 100), 1));
    w.func = gfx::WaveFunc::Noise;
    w.frequency = 1;
    for (float t = 0; t < 5; t += 0.37f)
        CHECK(gfx::evaluate(w, t) >= -1 && gfx::evaluate(w, t) <= 1);
}

static void testShaders() {
    const std::string text = R"(
// A comment before the first shader.
textures/borg/floor
{
    surfaceparm metalsteps
    qer_editorimage textures/borg/floor.tga
    {
        map $lightmap
    }
    {
        map textures/borg/floor.tga
        blendFunc filter
    }
    {
        animMap 4 textures/borg/a.tga textures/borg/b.tga
        blendfunc GL_ONE GL_ONE
    }
}
textures/sky/stars
{
    skyParms env/stars - -
    surfaceparm NoDraw
}
)";
    std::vector<ShaderDef> defs;
    CHECK(!parseShaderFile(text, "scripts/borg.shader", defs));
    CHECK(defs.size() == 2);
    if (defs.size() == 2) {
        const ShaderDef& f = defs[0];
        CHECK(f.name == "textures/borg/floor" && f.file == "scripts/borg.shader");
        CHECK(f.hasSurfaceParm("MetalSteps") && f.directives.size() == 2 && !f.sky);
        CHECK(f.stages.size() == 3);
        CHECK(f.stages[0].lightmap && f.stages[0].maps.empty() && f.stages[0].blendSrc.empty());
        CHECK(f.stages[1].maps.size() == 1 && f.stages[1].maps[0] == "textures/borg/floor.tga");
        CHECK(f.stages[1].blendSrc == "gl_dst_color" && f.stages[1].blendDst == "gl_zero");
        CHECK(f.stages[2].animFps == 4 && f.stages[2].maps.size() == 2 && f.stages[2].blendDst == "gl_one");
        CHECK(defs[1].sky && defs[1].hasSurfaceParm("nodraw"));
    }
    defs.clear();
    CHECK(parseShaderFile("broken\n{\n  { map x.tga\n", "x.shader", defs));
    CHECK(parseShaderFile("nobrace map", "x.shader", defs));

    ShaderLibrary lib;
    ShaderDef first, second;
    first.name = "Textures/A";
    first.file = "first.shader";
    second.name = "textures/a";
    second.file = "second.shader";
    lib.add(first);
    lib.add(second);
    CHECK(lib.size() == 1 && lib.find("TEXTURES\\a")->file == "first.shader" && !lib.find("textures/b"));
}

// A tiny map: one triangle in one leaf of a two-leaf tree, one brush,
// one lightmap and vis data for two clusters.
static std::vector<uint8_t> buildBsp(int version = 46, int badIndex = -1, int32_t lightmap = 0, int32_t fog = -1,
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

static void testBsp() {
    Bsp bsp;
    Error err = parseBsp(buildBsp(), bsp);
    CHECK(!err);
    if (err)
        std::fprintf(stderr, "%s\n", err.message.c_str());
    CHECK(bsp.entities == "{ \"classname\" \"worldspawn\" }");
    CHECK(bsp.shaders.size() == 1 && bsp.shaders[0].name == "textures/borg/floor" && bsp.shaders[0].contentFlags == 1);
    CHECK(bsp.planes.size() == 1 && bsp.planes[0].normal[2] == 1 && bsp.planes[0].dist == 16);
    CHECK(bsp.nodes.size() == 1 && bsp.nodes[0].children[1] == -2 && bsp.leafs.size() == 2);
    CHECK(bsp.models.size() == 1 && bsp.models[0].maxs[0] == 64 && bsp.models[0].numSurfaces == 1);
    CHECK(bsp.vertices.size() == 3 && bsp.vertices[1].pos[0] == 32 && bsp.vertices[0].color[1] == 128);
    CHECK(bsp.indices.size() == 3 && bsp.surfaces.size() == 1);
    CHECK(bsp.surfaces[0].type == SurfaceType::Planar && bsp.surfaces[0].fog == -1);
    CHECK(bsp.surfaces[0].lightmapSize[0] == 16 && bsp.surfaces[0].lightmapVecs[2][2] == 1);
    CHECK(bsp.lightmapCount() == 1 && bsp.visClusters == 2);
    CHECK(bsp.clusterVisible(0, 0) && !bsp.clusterVisible(0, 1) && bsp.clusterVisible(1, 0));
    CHECK(bsp.clusterVisible(-1, 1));

    CHECK(bsp.fixes.total() == 0);

    CHECK(parseBsp(buildBsp(47), bsp)); // wrong version
    // An index past the surface's vertices; the message says which.
    err = parseBsp(buildBsp(46, 3), bsp);
    CHECK(err && err.message == "BSP surface 0: index 2 is vertex 3 (of 3)");

    // What the original engine tolerates: a lightmap past the end (vertex
    // lighting), a missing fog, a flare's vertices, an unknown type.
    CHECK(!parseBsp(buildBsp(46, -1, 5, 2), bsp));
    CHECK(bsp.surfaces[0].lightmap == -1 && bsp.surfaces[0].fog == -1);
    CHECK(bsp.fixes.lightmaps == 1 && bsp.fixes.fogs == 1 && bsp.fixes.total() == 2);
    CHECK(!parseBsp(buildBsp(46, 7, 0, -1, 4), bsp)); // a flare's bad index doesn't matter
    CHECK(bsp.surfaces[0].type == SurfaceType::Flare && bsp.surfaces[0].numVertices == 0 && bsp.fixes.flares == 1);
    CHECK(!parseBsp(buildBsp(46, -1, 0, -1, 9), bsp));
    CHECK(bsp.surfaces[0].type == SurfaceType::Bad && bsp.fixes.types == 1);
    auto truncated = buildBsp();
    truncated.resize(truncated.size() - 1);
    CHECK(parseBsp(truncated, bsp));
    CHECK(parseBsp({'I', 'B', 'S', 'P'}, bsp));
}

static void testPatch() {
    // A 3x3 patch bulging up in the middle: every control point at height
    // 0 but the centre one at 4, normals pointing up.
    BspVertex control[9];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            BspVertex& v = control[y * 3 + x];
            v.pos = {float(x) * 8, float(y) * 8, x == 1 && y == 1 ? 4.0f : 0.0f};
            v.st = {float(x) / 2, float(y) / 2};
            v.normal = {0, 0, 1};
            v.color = {200, 100, 0, 255};
        }
    std::vector<BspVertex> verts(1); // something already there: indices must be offset
    std::vector<uint32_t> idx;
    tessellatePatch(control, 3, 3, 4, verts, idx);
    CHECK(verts.size() == 1 + 25 && idx.size() == 16 * 6);
    // Corners stay put; the middle is the Bezier point: 4 * (2 * 0.5 * 0.5)^2 = 1.
    CHECK(verts[1].pos[0] == 0 && verts[1].pos[2] == 0 && verts[25].pos[0] == 16 && verts[25].pos[1] == 16);
    const BspVertex& mid = verts[1 + 12];
    CHECK(std::fabs(mid.pos[0] - 8) < 1e-5f && std::fabs(mid.pos[2] - 1) < 1e-5f);
    CHECK(std::fabs(mid.st[0] - 0.5f) < 1e-5f && mid.color[0] == 200 && std::fabs(mid.normal[2] - 1) < 1e-5f);
    bool inRange = true, clockwise = true;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        for (int k = 0; k < 3; ++k)
            inRange = inRange && idx[i + k] >= 1 && idx[i + k] < verts.size();
        // Clockwise seen from above (where the normals point): the
        // geometric normal points down.
        const auto& a = verts[idx[i]].pos;
        const auto& b = verts[idx[i + 1]].pos;
        const auto& c = verts[idx[i + 2]].pos;
        const float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        clockwise = clockwise && nz < 0;
    }
    CHECK(inRange && clockwise);
    // Normals pointing down flip the winding.
    for (BspVertex& v : control)
        v.normal = {0, 0, -1};
    std::vector<BspVertex> down;
    std::vector<uint32_t> downIdx;
    tessellatePatch(control, 3, 3, 4, down, downIdx);
    const auto& a = down[downIdx[0]].pos;
    const auto& b = down[downIdx[1]].pos;
    const auto& c = down[downIdx[2]].pos;
    CHECK((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0);
    // Two blocks side by side share their edge: 5x3 control points.
    std::vector<BspVertex> wide(15, control[0]), out;
    std::vector<uint32_t> outIdx;
    tessellatePatch(wide.data(), 5, 3, 2, out, outIdx);
    CHECK(out.size() == 5 * 3 && outIdx.size() == 4 * 2 * 6);
}

static void testFilesystemAndDetection(const std::filesystem::path& tmp) {
    using test::toBytes;
    std::filesystem::remove_all(tmp);
    const auto base = tmp / "BaseEF";
    test::writeFile(base / "pak0.pk3", test::buildZip({{"a.txt", toBytes("pak0"), {}},
                                                       {"b.txt", toBytes("pak0"), {}},
                                                       {"scripts/x.shader", toBytes("textures/x\n{\n}\n"), {}}}));
    test::writeFile(base / "PAK1.pk3", test::buildZip({{"a.txt", toBytes("pak1"), {}},
                                                       {"b.txt", toBytes("pak1"), {}}}));
    test::writeFile(base / "zz_broken.pk3", toBytes("not a zip"));
    // In upper case '_' sorts after letters, so pak_c.pk3 beats pakb.pk3.
    test::writeFile(base / "pak_c.pk3", test::buildZip({{"c.txt", toBytes("underscore"), {}}}));
    test::writeFile(base / "pakb.pk3", test::buildZip({{"c.txt", toBytes("letter"), {}}}));
    test::writeFile(base / "b.txt", toBytes("loose"));

    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "baseef", 0, warnings));
    CHECK(warnings.size() == 1);
    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && data == toBytes("pak1"));
    CHECK(!set.read("b.txt", data) && data == toBytes("loose"));
    CHECK(!set.read("c.txt", data) && data == toBytes("underscore"));
    CHECK(mountGameDir(set, tmp, "missing", 1, warnings));

    ShaderLibrary lib;
    lib.load(set);
    CHECK(lib.size() == 1 && lib.find("textures/x"));

    // Images are found whichever extension is written, .tga first.
    test::writeFile(base / "textures" / "a.jpg", toBytes("jpg"));
    test::writeFile(base / "textures" / "b.tga", toBytes("tga"));
    test::writeFile(base / "textures" / "b.jpg", toBytes("jpg"));
    test::writeFile(base / "v1.5" / "c.tga", toBytes("tga"));
    SearchSet images;
    CHECK(!mountGameDir(images, tmp, "baseef", 0, warnings));
    CHECK(findImage(images, "textures/a.tga") == "textures/a.jpg");
    CHECK(findImage(images, "textures/a") == "textures/a.jpg");
    CHECK(findImage(images, "textures/b.jpg") == "textures/b.tga");
    CHECK(findImage(images, "v1.5/c") == "v1.5/c.tga"); // a dot in a folder isn't an extension
    CHECK(findImage(images, "textures/none.tga").empty());

    // Not Elite Force until the executable is there too.
    const MetaEngine* meta = findEngine("idtech3");
    CHECK(meta && meta->detect(tmp).empty());
    test::writeFile(tmp / "STVOY.EXE", toBytes("MZ"));
    // The test's pak0.pk3 isn't a known release: found by name, and only once.
    auto found = meta ? meta->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1 && found[0].gameId == "stvef" && findGame(found[0].gameId));
    CHECK(found.size() == 1 && found[0].description.find("unknown release") != std::string::npos);
    // Every known release names pak0.pk3's size and hash.
    for (const GameVariant& v : detectionTable())
        CHECK(!v.files.empty() && !v.files[0].md5.empty() && v.files[0].size > 0);
    CHECK(!findGame("nope"));
    std::filesystem::remove_all(tmp);
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_idtech3_tests";
    testLexer();
    testEntities();
    testShaders();
    testShaderStages();
    testWaves();
    testBsp();
    testPatch();
    testFilesystemAndDetection(tmp / "fs");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
