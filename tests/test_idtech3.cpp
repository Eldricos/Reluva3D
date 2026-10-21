// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the id Tech 3 engine's readers, on data built here.

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/sounds.h"
#include "common/audio/wav.h"
#include "engines/registry.h"
#include "data/qvm/fake_cgame_qvm.h"
#include "data/qvm/fake_game_qvm.h"
#include "data/qvm/fake_ui_qvm.h"
#include "data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
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
    // An extension is ignored, as models name their shaders "....tga".
    CHECK(lib.find("textures/a.tga") && lib.find("Textures/A.JPG") && !lib.find("textures/a.b/c"));
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

// A two-frame MD3 with one tag and one triangle; frame 1 is frame 0
// moved up 64 units.
static std::vector<uint8_t> buildMd3(int badIndex = -1) {
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

static void testMd3() {
    Md3Model m;
    Error err = parseMd3(buildMd3(), m);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK(m.name == "models/test/box.md3" && m.frames.size() == 2 && m.tagCount == 1 && m.surfaces.size() == 1);
    CHECK(m.frames[1].name == "up" && m.frames[1].maxs[2] == 80 && m.frames[0].radius == 12);
    const Md3Tag* top = m.tag(1, "tag_top");
    CHECK(top && top->origin[2] == 80 && top->axis[2][2] == 1 && !m.tag(0, "tag_head") && !m.tag(5, "tag_top"));
    const Md3Surface& s = m.surfaces[0];
    CHECK(s.name == "box" && s.shaders.size() == 1 && s.shaders[0] == "models/test/box");
    CHECK((s.indices == std::vector<uint32_t>{0, 1, 2}) && s.vertexCount == 3 && s.vertices.size() == 6);
    CHECK(s.st[1][0] == 1 && s.st[2][1] == 1);
    CHECK(s.vertices[1].pos[0] == 8 && s.vertices[2].pos[1] == 8 && s.vertices[3 + 0].pos[2] == 64);
    // Normal 0 (both angles 0) points up; lat 64 lng 64 (a quarter turn each) along +y.
    CHECK(std::fabs(s.vertices[0].normal[2] - 1) < 1e-5f);
    CHECK(std::fabs(s.vertices[1].normal[1] - 1) < 1e-5f && std::fabs(s.vertices[1].normal[2]) < 1e-5f);

    CHECK(parseMd3(buildMd3(3), m)); // a triangle using a vertex past the end
    auto cut = buildMd3();
    cut.resize(cut.size() - 4);
    CHECK(parseMd3(cut, m));
    CHECK(parseMd3({'I', 'D', 'P', '2'}, m));
}

static void testSkinAndPlacement() {
    const Skin skin = parseSkin("h_Head,models/players2/munro/head.tga\r\ntag_head,\n\nbody , models/x/body \nempty,\n");
    CHECK(skin.size() == 2 && skin.at("h_head") == "models/players2/munro/head.tga" && skin.at("body") == "models/x/body");

    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    // Yaw 90: forward is +y, left is -x.
    auto axes = anglesToAxes({0, 90, 0});
    CHECK(approx(axes[0][1], 1) && approx(axes[1][0], -1) && approx(axes[2][2], 1));
    // Pitch 90 (down positive): forward points down.
    axes = anglesToAxes({90, 0, 0});
    CHECK(approx(axes[0][2], -1));
    // A tag 10 up in a model placed at (100, 0, 0) turned 90: what's
    // attached 5 along its x lands at (100, 5, 10).
    Md3Tag tag;
    tag.origin = {0, 0, 10};
    tag.axis = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const gfx::Mat4 m = gfx::multiply(placement({100, 0, 0}, anglesToAxes({0, 90, 0})), tagPlacement(tag));
    const gfx::Vec3 p = gfx::transformPoint(m, {5, 0, 0});
    CHECK(approx(p[0], 100) && approx(p[1], 5) && approx(p[2], 10));
}

static void testLightGrid() {
    // A world 0..128 x 0..64 x 0..128 holds 3 x 2 x 2 grid points.
    Bsp bsp;
    BspModel world;
    world.mins = {0, 0, 0};
    world.maxs = {128, 64, 128};
    bsp.models.push_back(world);
    bsp.lightGrid.assign(3 * 2 * 2 * 8, 0);
    auto point = [&](int x, int y, int z) { return &bsp.lightGrid[size_t(((z * 2 + y) * 3 + x) * 8)]; };
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) {
                uint8_t* p = point(x, y, z);
                p[0] = p[1] = p[2] = uint8_t(x == 0 ? 40 : 80); // ambient, doubled on loading
                p[3] = p[4] = p[5] = 50;                        // directed
                p[6] = 0;                                      // from straight above
            }
    point(2, 0, 0)[0] = point(2, 0, 0)[1] = point(2, 0, 0)[2] = 0; // inside a wall
    LightGrid grid;
    grid.build(bsp);
    CHECK(!grid.empty());
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // On grid point (0,0,0): 40 doubled, plus the minimum ambient of 32.
    PointLight l = grid.sample({0, 0, 0});
    CHECK(approx(l.ambient[0], 112) && approx(l.directed[0], 100) && approx(l.direction[2], 1));
    // Halfway between x = 0 and 1: 80 and 160 blend to 120, + 32.
    l = grid.sample({32, 0, 0});
    CHECK(approx(l.ambient[0], 152));
    // Next to the point in a wall, it's skipped, not blended as black.
    l = grid.sample({96, 0, 0});
    CHECK(approx(l.ambient[0], 160 + 32));
    // A grid of the wrong size is ignored, and sampling still gives light.
    bsp.lightGrid.resize(8);
    grid.build(bsp);
    CHECK(grid.empty() && grid.sample({0, 0, 0}).ambient[0] > 0);
}

// Builds a QVM from instructions, initialised data and literals.
struct QvmAsm {
    std::vector<std::pair<uint8_t, int32_t>> code;
    std::vector<uint8_t> data, lit;
    int32_t bss = 0;
    int add(uint8_t op, int32_t operand = 0) {
        code.emplace_back(op, operand);
        return int(code.size()) - 1;
    }
    std::vector<uint8_t> build(int32_t magic = 0x12721444) const {
        std::vector<uint8_t> bytes;
        for (const auto& [op, v] : code) {
            bytes.push_back(op);
            const int n = qvmop::operandSize(op);
            for (int i = 0; i < n; ++i)
                bytes.push_back(uint8_t(uint32_t(v) >> (8 * i)));
        }
        while (bytes.size() % 4)
            bytes.push_back(0);
        std::vector<uint8_t> out;
        auto i32 = [&](int32_t x) {
            for (int i = 0; i < 4; ++i)
                out.push_back(uint8_t(uint32_t(x) >> (8 * i)));
        };
        const int32_t header = 32;
        i32(magic);
        i32(int32_t(code.size()));
        i32(header);
        i32(int32_t(bytes.size()));
        i32(header + int32_t(bytes.size()));
        i32(int32_t(data.size()));
        i32(int32_t(lit.size()));
        i32(bss);
        out.insert(out.end(), bytes.begin(), bytes.end());
        out.insert(out.end(), data.begin(), data.end());
        out.insert(out.end(), lit.begin(), lit.end());
        return out;
    }
};

static void testQvm() {
    using namespace qvmop;
    int32_t r = 0;
    // vmMain(command, a, b) = command + a * b; arguments sit at
    // sp + frame + 8 after ENTER frame.
    {
        QvmAsm a;
        a.add(Enter, 8);
        a.add(Local, 16), a.add(Load4);
        a.add(Local, 20), a.add(Load4), a.add(Local, 24), a.add(Load4), a.add(Muli), a.add(Add);
        a.add(Leave, 8);
        Qvm vm;
        CHECK(!vm.load(a.build()));
        CHECK(!vm.call(3, {4, 5}, r) && r == 23);
        CHECK(!vm.call(-7, {-2, 3}, r) && r == -13);
    }
    // Recursion: factorial through calls, frames and ARG.
    QvmAsm f;
    f.add(Enter, 16);
    f.add(Local, 24), f.add(Load4), f.add(Arg, 8), f.add(Const, 0), f.add(Call), f.add(Leave, 16);
    const int fact = f.add(Enter, 16);
    f.code[4].second = fact;
    f.add(Local, 24), f.add(Load4), f.add(Const, 1);
    const int gt = f.add(Gti, 0);
    f.add(Const, 1), f.add(Leave, 16);
    f.code[size_t(gt)].second = int32_t(f.code.size());
    f.add(Local, 24), f.add(Load4);
    f.add(Local, 24), f.add(Load4), f.add(Const, 1), f.add(Sub), f.add(Arg, 8), f.add(Const, fact), f.add(Call);
    f.add(Muli), f.add(Leave, 16);
    Qvm vm;
    CHECK(!vm.load(f.build()));
    CHECK(!vm.call(5, {}, r) && r == 120);
    CHECK(!vm.call(10, {}, r) && r == 3628800);

    // A system call with a string and a number, and one that calls back
    // into the VM while it's running.
    {
        QvmAsm a;
        a.lit = {'h', 'i', 0, 0};
        a.data = {0, 0, 0, 0};
        a.add(Enter, 16);
        a.add(Const, 4), a.add(Arg, 8), a.add(Const, 42), a.add(Arg, 12), a.add(Const, -2), a.add(Call);
        a.add(Const, 1), a.add(Add);
        a.add(Local, 24), a.add(Load4), a.add(Arg, 8), a.add(Const, -6), a.add(Call), a.add(Add);
        a.add(Leave, 16);
        Qvm s;
        CHECK(!s.load(a.build()));
        Qvm inner;
        CHECK(!inner.load(f.build()));
        std::string seen;
        int calls = 0;
        s.setSystemCall([&](Qvm& v, int number, const int32_t* args) -> int32_t {
            ++calls;
            if (number == 1) {
                seen = v.string(args[0]) + "/" + std::to_string(args[1]);
                return 100;
            }
            if (number == 5) {
                // Back into a VM from a system call: here another one, and
                // the running one keeps its state.
                int32_t out = 0;
                return inner.call(args[0], {}, out) ? -1 : out;
            }
            return 0;
        });
        CHECK(!s.call(4, {}, r) && r == 101 + 24 && seen == "hi/42" && calls == 2);
        CHECK(s.executed() > 10);
    }

    // Memory: bytes and halfwords sign-extended, block copy, initialised data.
    {
        QvmAsm a;
        a.data = {0x80, 0xff, 0x34, 0x12, 1, 2, 3, 4};
        a.bss = 64;
        a.add(Enter, 8);
        a.add(Const, 0), a.add(Load1), a.add(Sex8);       // -128
        a.add(Const, 0), a.add(Load2), a.add(Sex16), a.add(Add); // -128 + -128
        a.add(Const, 16), a.add(Const, 4), a.add(BlockCopy, 4);
        a.add(Const, 16), a.add(Load4), a.add(Add);       // + 0x04030201
        a.add(Const, 20), a.add(Const, 0x55), a.add(Store1);
        a.add(Const, 20), a.add(Load1), a.add(Add);       // + 0x55
        a.add(Leave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -256 + 0x04030201 + 0x55);
        CHECK(m.readInt(16) == 0x04030201 && m.memory()[20] == 0x55 && m.readInt(0) == int32_t(0x1234ff80));
    }

    // Floats, unsigned comparison, division edge cases.
    {
        QvmAsm a;
        a.add(Enter, 8);
        a.add(Const, Qvm::fromFloat(1.5f)), a.add(Const, Qvm::fromFloat(2.25f)), a.add(Mulf);
        a.add(Const, 3), a.add(Cvif), a.add(Addf), a.add(Negf), a.add(Cvfi); // -(3.375 + 3) -> -6
        a.add(Const, -1), a.add(Const, 1);
        const int b = a.add(Ltu, 0); // 0xffffffff < 1 unsigned: false
        a.add(Const, 1000), a.add(Add);
        a.code[size_t(b)].second = int32_t(a.code.size());
        a.add(Const, INT32_MIN), a.add(Const, -1), a.add(Divi), a.add(Const, INT32_MIN), a.add(Sub), a.add(Add);
        a.add(Const, -7), a.add(Const, 2), a.add(Modi), a.add(Add);    // -1
        a.add(Const, -8), a.add(Const, 1), a.add(Rshi), a.add(Add);    // -4
        a.add(Const, -8), a.add(Const, 28), a.add(Rshu), a.add(Add);   // 15
        a.add(Leave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -6 + 1000 + 0 - 1 - 4 + 15);
    }

    // Faults are errors that name the instruction, not crashes.
    auto runs = [&](std::vector<std::pair<uint8_t, int32_t>> code, uint64_t limit = 0) {
        QvmAsm a;
        a.code = std::move(code);
        Qvm m;
        if (m.load(a.build()))
            return std::string("load");
        m.setInstructionLimit(limit);
        Error err = m.call(0, {}, r);
        return err ? err.message : std::string();
    };
    CHECK(runs({{Enter, 8}, {Const, -100}, {Load4, 0}, {Leave, 8}}).find("reads outside memory") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1 << 29}, {Const, 1}, {Store4, 0}, {Leave, 8}}).find("writes outside") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1}, {Const, 0}, {Divi, 0}, {Leave, 8}}).find("division by zero") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 99}, {Jump, 0}}).find("jumps to 99") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1}, {Jump, 0}}, 1000).find("instruction limit") != std::string::npos);
    CHECK(runs({{Enter, 0x20000}, {Leave, 0x20000}}).find("stack overflow") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Add, 0}, {Leave, 8}}).find("operand stack") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 5}, {Call, 0}, {Leave, 8}}).find("calls 5") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Eq, 50}, {Leave, 8}}) == "load"); // branch target checked on load
    // Damaged files.
    Qvm bad;
    CHECK(bad.load({1, 2, 3}));
    auto file = f.build();
    file[0] = 0;
    CHECK(bad.load(file));
    file = f.build();
    file[32] = 200; // an unknown opcode
    CHECK(bad.load(file));
    file = f.build();
    file[4] = 100; // more instructions than the code holds
    CHECK(bad.load(file));
    CHECK(!bad.load(f.build(0x12721445))); // the second version's magic
}

// tests/data/qvm/test_vm.c compiled by the Quake III compiler: every case
// must give what the same program gives compiled natively.
static void testCompiledQvm() {
    Qvm vm;
    Error err = vm.load(std::vector<uint8_t>(std::begin(kTestVmQvm), std::end(kTestVmQvm)));
    CHECK(!err);
    if (err)
        return;
    std::string printed;
    vm.setSystemCall([&](Qvm& v, int number, const int32_t* args) -> int32_t {
        switch (number) {
        case 0: printed = v.string(args[0]); return int32_t(printed.size()) * 3;
        case 1: return args[0] * 2 + args[1];
        case 2: {
            int32_t out = 0;
            return v.call(args[0], {args[1]}, out) ? -999 : out;
        }
        case 3: return Qvm::fromFloat(Qvm::asFloat(args[0]) / 2.0f);
        default: return 0;
        }
    });
    for (const QvmCase& c : kQvmCases) {
        int32_t r = 0;
        err = vm.call(c.command, {c.arg0, c.arg1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 100}, r);
        if (err || r != c.expected)
            std::fprintf(stderr, "case %d(%d, %d): got %d, expected %d %s\n", c.command, c.arg0, c.arg1, r,
                         c.expected, err ? err.message.c_str() : "");
        CHECK(!err && r == c.expected);
    }
    // Case 7 builds a string and prints it.
    int32_t r = 0;
    CHECK(!vm.call(7, {42, -17}, r) && printed == "value 42 and -17");
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

static void testCvars() {
    CvarStore cvars;
    const CvarStore::Cvar& a = cvars.create("Sensitivity", "5.5", CvarStore::Archive);
    CHECK(a.handle == 1 && a.modificationCount == 1);
    CHECK(cvars.value("sensitivity") == 5.5f && cvars.integer("SENSITIVITY") == 5);
    cvars.create("sensitivity", "1", CvarStore::Cheat); // exists: keeps its value, gains the flag
    CHECK(cvars.string("sensitivity") == "5.5" && cvars.find("sensitivity")->flags == (CvarStore::Archive | CvarStore::Cheat));
    cvars.set("sensitivity", "3");
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.set("sensitivity", "3"); // unchanged: not counted
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.create("version", "1.0", CvarStore::ReadOnly);
    cvars.set("version", "2.0");
    CHECK(cvars.string("version") == "1.0");
    cvars.set("version", "2.0", true);
    CHECK(cvars.string("version") == "2.0");
    cvars.set("made_up", "x"); // created by the user, then claimed by the code with its default
    CHECK(cvars.find("made_up")->flags == CvarStore::UserCreated);
    cvars.create("made_up", "y", 0);
    CHECK(cvars.find("made_up")->flags == 0 && cvars.find("made_up")->defaultValue == "y" &&
          cvars.string("made_up") == "x");
    CHECK(cvars.byHandle(2)->name == "version" && !cvars.byHandle(0) && !cvars.byHandle(4));
}

static void testVmFileList() {
    using test::toBytes;
    auto dir = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_list.pk3";
    test::writeFile(path, test::buildZip({{"scripts/a.arena", toBytes("a"), {}},
                                          {"Scripts/B.ARENA", toBytes("b"), {}},
                                          {"scripts/c.txt", toBytes("c"), {}},
                                          {"scripts/deep/d.arena", toBytes("d"), {}},
                                          {"scripts/deep/e.arena", toBytes("e"), {}},
                                          {"scriptsx/f.arena", toBytes("f"), {}}}));
    CHECK(!dir->open(path));
    SearchSet set;
    set.add(std::move(dir), 0);
    const auto arenas = VmFiles::listNames(set, "scripts/", ".arena");
    CHECK(arenas.size() == 2 && arenas[0] == "a.arena" && arenas[1] == "B.ARENA");
    const auto folders = VmFiles::listNames(set, "scripts", "/");
    CHECK(folders.size() == 1 && folders[0] == "deep");
    CHECK(VmFiles::listNames(set, "scripts", "").size() == 3);
    std::filesystem::remove(path);

    HandleTable table;
    CHECK(table.add("gfx/a") == 1 && table.add("GFX\\A") == 1 && table.add("gfx/b") == 2 && table.add("") == 0);
    CHECK(table.name(2) == "gfx/b" && table.name(0).empty() && table.name(3).empty());
}

// Sounds by name: decoded once, missing and broken ones remembered.
static void testSoundLibrary(const std::filesystem::path& tmp) {
    using test::toBytes;
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.format = 1;
    for (int i = 0; i < 2205; ++i)
        tone.samples.push_back(int16_t(i % 50 < 25 ? 8000 : -8000));
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"sound/interface/Button1.wav", audio::encodePcmWav(tone), {}},
                                    {"sound/broken.wav", toBytes("not a sound"), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    SoundLibrary sounds(set);
    const auto a = sounds.get("sound/interface/button1.wav");
    CHECK(a && a->rate == 22050 && a->channels == 1 && a->frames() == 2205 && a->samples[30] == -8000);
    CHECK(sounds.get("SOUND\\interface\\BUTTON1.WAV") == a); // the same, decoded once
    CHECK(!sounds.get("sound/missing.wav") && !sounds.get("sound/broken.wav") && !sounds.get(""));
    CHECK(sounds.problems().size() == 2);
    CHECK(!sounds.get("sound/missing.wav") && sounds.problems().size() == 2); // not tried again
    std::filesystem::remove_all(tmp);
}

// A small map for collision: a floor in the world (x and y -512..512, z
// -64..0) and one brush model, *1, a door 16 x 128 x 128 standing on its
// own origin.
static std::shared_ptr<Bsp> collisionMap() {
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

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void testCollision() {
    const auto bsp = collisionMap();
    const CollisionWorld world(*bsp);
    const Vec3f zero{};
    // A point falling onto the floor stops an eighth above it.
    TraceResult t = world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Solid);
    CHECK(!t.startSolid && near(t.endPos[2], 0.125f) && near(t.fraction, 99.875f / 200));
    CHECK(t.normal[2] == 1 && t.contents == contents::Solid && near(t.dist, 0));
    // A player's box (feet 24 below its origin) stops with its feet there.
    t = world.trace({0, 0, 100}, {0, 0, -100}, {-15, -15, -24}, {15, 15, 32}, 0, contents::Solid);
    CHECK(near(t.endPos[2], 24.125f));
    // Nothing beyond the floor's edge; nothing for a mask without solid.
    CHECK(world.trace({1000, 0, 100}, {1000, 0, -100}, zero, zero, 0, contents::Solid).fraction == 1);
    CHECK(world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Water).fraction == 1);
    // Starting inside: startSolid; staying inside: allSolid.
    t = world.trace({0, 0, -10}, {0, 0, -1000}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && !t.allSolid);
    t = world.trace({0, 0, -10}, {0, 0, -20}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && t.allSolid && t.fraction == 0);
    // Sideways into the door (model 1, in its own coordinates).
    t = world.trace({-100, 0, 64}, {100, 0, 64}, zero, zero, 1, contents::Solid);
    CHECK(near(t.endPos[0], -8.125f) && t.normal[0] == -1);
    CHECK(world.pointContents({0, 0, -32}, 0) == contents::Solid && world.pointContents({0, 0, 32}, 0) == 0);
    CHECK(world.pointContents({0, 0, 64}, 1) == contents::Solid);
    Vec3f mins, maxs;
    CHECK(world.modelBounds(1, mins, maxs) && maxs[2] == 128 && !world.modelBounds(2, mins, maxs));
    // A box entity.
    t = CollisionWorld::traceBox({0, 0, 100}, {0, 0, -100}, zero, zero, {-16, -16, 0}, {16, 16, 32}, contents::Body);
    CHECK(near(t.endPos[2], 32.125f) && t.contents == contents::Body);
}

// The game logic starting a level: its entities handed over token by
// token, as the original server does, by a QVM the Quake III compiler
// made from tests/data/qvm/fake_game.c.
static void testGameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube, deck 4\"\n}\n"
                                 "// a comment\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                                 "\"origin\" \"200 0 0\"\n}\n/* another */ {\n\"classname\" \"item_health\"\n"
                                 "\"origin\" \"-200 0 16\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n"
                                 "\"origin\" \"0 0 300\"\n}\n";
    auto map = collisionMap();
    map->entities = entities;
    const Error err = game.startLevel("borg2", map);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK(game.log() == "------- Game Initialization -------\n");
    CHECK(game.gameData().count == 4 && game.gameData().entitySize == 352 && game.gameData().clientSize == 4);
    CHECK((game.linked() == std::set<int32_t>{1, 2, 3})); // the world unlinked
    CHECK(game.configstrings().count(12) && game.configstrings().at(12) == "borg2");
    // The server info, as the client game finds the map's name.
    CHECK(game.configstrings().count(0) && game.configstrings().at(0).find("\\mapname\\borg2") != std::string::npos);
    CHECK(game.levelTime() == 300);
    int32_t r = 0;
    CHECK(!game.call(23, {}, r) && r == 3); // the three settling frames
    CHECK(!game.call(21, {2}, r) && r == 'i' * 1000 + 't');
    CHECK(!game.call(21, {0}, r) && r == 'w' * 1000 + 'o');
    CHECK(!game.call(22, {}, r) && r == 803); // sv_maxclients 8, ceil(2.25)

    // SetBrushModel: the door's model, box and contents, linked.
    CHECK(!game.call(30, {1}, r) && r == 111);
    CHECK(game.entityVec(1, 232)[2] == 128 && game.entityInt(1, 244) == -1);
    // Linking fills in the box in the world, a unit larger: the item's z -16..16 around 16.
    CHECK(!game.call(27, {2}, r) && r == -1 * 1000 + 33);
    // Traces down onto the floor, the door (entity 1) and the item (2), in eighths.
    CHECK(!game.call(24, {0, 0, 100, 0, -1}, r) && r == 1022);
    CHECK(!game.call(25, {}, r) && r == 1);
    CHECK(!game.call(26, {}, r) && r == 1000); // the floor's normal points up
    CHECK(!game.call(24, {200, 0, 500, 0, -1}, r) && r == 1);
    CHECK(!game.call(25, {}, r) && r == 128 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, -1}, r) && r == 2);
    CHECK(!game.call(25, {}, r) && r == 32 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, 2}, r) && r == 1022); // ignoring the item
    CHECK(!game.call(24, {0, 0, 100, 16, -1}, r) && !game.call(25, {}, r) && r == 16 * 8 + 1); // a box
    CHECK(!game.call(24, {0, 0, -10, 0, -1}, r) && !game.call(26, {}, r) && r == 101); // starting in the floor
    CHECK(!game.call(24, {1000, 0, 100, 0, -1}, r) && r == 1023); // nothing there
    CHECK(!game.call(28, {0, 0, -32}, r) && r == contents::Solid);
    CHECK(!game.call(28, {200, 0, 64}, r) && r == -1); // inside the door
    CHECK(!game.call(28, {0, 0, 64}, r) && r == 0);
    CHECK(!game.call(29, {-200, 0, 16}, r) && r == 102);
    CHECK(!game.call(29, {0, 0, 300}, r) && r == 103); // the spawn point

    // A player joins: refused without a name, then let in and placed.
    std::string refused;
    CHECK(!game.connectClient(0, "", refused) && refused == "No name given");
    CHECK(!game.connectClient(0, "\\name\\Reluva\\model\\munro", refused) && refused.empty());
    CHECK(!game.beginClient(0));
    CHECK(!game.call(31, {}, r) && r == 4 && game.linked().count(4));
    CHECK(game.entityVec(4, 272)[0] == 0 && game.entityVec(4, 272)[2] == 309); // the spawn point, 9 up
    // Its commands, field by field in Quake III's usercmd_t.
    ef::GameHost::UserCommand cmd;
    cmd.serverTime = 400;
    cmd.angles = {1000, 16384, 0};
    cmd.buttons = 5;
    cmd.weapon = 3;
    cmd.forward = 127;
    cmd.right = -127;
    cmd.up = 10;
    CHECK(!game.clientThink(0, cmd));
    CHECK(!game.call(32, {}, r) && r == 4005);
    CHECK(!game.call(33, {}, r) && r == 1000 + 16384 * 100000);
    CHECK(!game.call(34, {}, r) && r == 3000000 + 255 * 10000 + 1 * 100 + 60);
    CHECK(game.entityVec(4, 272)[0] == 127); // moved by it
    // Its snapshot: every linked entity (the door, the item, the spawn
    // point, the player's box at 4: the stand-in's player isn't entity 0),
    // each the first 204 bytes of the game's entity.
    const auto snap = game.snapshot(0);
    CHECK(snap.serverTime == 300 && snap.entities.size() == 4 && snap.entities[0].size() == 204);
    CHECK(snap.entities[0][0] == 1 && snap.entities[3][0] == 4); // s.number
    CHECK(snap.serverCommandSequence == 1);
    CHECK(game.serverCommands().size() == 1 && game.serverCommands()[0].first == -1 &&
          game.serverCommands()[0].second == "print \"Reluva entered the game\n\"");
    CHECK(game.unhandled().empty());

    // Broken entity text: the game's own error stops it.
    ef::GameHost broken(set, cvars);
    CHECK(!broken.load());
    auto brokenMap = std::make_shared<Bsp>();
    brokenMap->entities = "{ \"classname\" \"worldspawn\" ";
    const Error e2 = broken.startLevel("x", brokenMap);
    CHECK(e2 && e2.message.find("broken entity text") != std::string::npos);
    std::filesystem::remove_all(tmp);
}

// The client game starting: the server's configstrings handed over as
// its game state (Quake III's gamestate_t), by a QVM the Quake III
// compiler made from tests/data/qvm/fake_cgame.c.
static void testCgameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/cgame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeCgameQvm), std::end(kFakeCgameQvm)), {}},
                                    {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::CgameHost cgame(set, cvars);
    CHECK(!cgame.load());
    cgame.setConfigstrings({{12, "baseef-1"}, {289, "sound/player/fry.wav"}, {544, "n\\Reluva\\t\\0"}, {5, ""}});
    int32_t r = 0;
    CHECK(!cgame.call(ef::CgameHost::Init, {0, 0, 3}, r) && r == 3);
    CHECK(cgame.log() == "fake cgame\n");
    CHECK(!cgame.call(20, {12}, r) && r == 'b' * 1000 + 'a');
    CHECK(!cgame.call(21, {289}, r) && r == 20);
    CHECK(!cgame.call(20, {544}, r) && r == 'n' * 1000 + '\\');
    CHECK(!cgame.call(21, {5}, r) && r == 0);   // empty
    CHECK(!cgame.call(21, {100}, r) && r == 0); // never set
    CHECK(!cgame.call(22, {}, r) && r == 1 + 9 + 21 + 13);
    CHECK(!cgame.call(23, {}, r) && r == 640480);
    CHECK(!cgame.call(24, {}, r) && r == 101);
    CHECK((cgame.commands() == std::vector<std::string>{"+zoom", "weapnext"}));
    CHECK(cgame.music().first == "music/intro" && cgame.music().second == "music/loop");

    // A snapshot, in Elite Force's snapshot_t (52740 bytes).
    CHECK(!cgame.call(25, {}, r) && r == -1); // none yet
    ef::GameHost::Snapshot snap;
    snap.serverTime = 1300;
    snap.playerState.assign(460, 0);
    snap.playerState[0] = 7;   // the first word: 7
    snap.playerState[456] = 2; // the last: 2
    for (int e = 0; e < 3; ++e) {
        std::vector<uint8_t> state(204, 0);
        state[0] = uint8_t(10 + e); // s.number
        state[200] = uint8_t(e);    // the last word
        snap.entities.push_back(state);
    }
    snap.serverCommandSequence = 2;
    cgame.newSnapshot(snap);
    CHECK(!cgame.call(25, {}, r) && r == 1300);
    CHECK(!cgame.call(26, {}, r) && r == 3002);
    CHECK(!cgame.call(27, {}, r) && r == 9);
    CHECK(!cgame.call(28, {2}, r) && r == 12 * 1000 + 2);
    CHECK(cgame.snapshotRequests().size() == 2 && cgame.snapshotRequests()[1].first == 1);
    // Server commands, split into words.
    cgame.setServerCommands({"print \"Reluva entered the game\n\"", "cs 544 \"n\\Reluva\""});
    CHECK(!cgame.call(29, {1, 1}, r) && r == 2000 + 'R');
    CHECK(!cgame.call(29, {2, 0}, r) && r == 3000 + 'c');
    CHECK(!cgame.call(29, {3, 0}, r) && r == -1);

    // The player's commands, for prediction.
    CHECK(!cgame.call(30, {}, r) && r == -1); // none yet
    ef::GameHost::UserCommand cmd;
    cmd.forward = 100;
    cgame.addUserCommand(cmd);
    cgame.addUserCommand(cmd);
    CHECK(!cgame.call(30, {}, r) && r == 2100);
    // A scene: its entities kept with the size that cleared them, the view.
    CHECK(!cgame.call(31, {}, r));
    CHECK(cgame.scene().entities.size() == 3 && cgame.scene().entities[0].clearedSize == 160);
    CHECK(cgame.scene().renders == 2 && cgame.scene().view.size() == 112 && cgame.scene().view[8] == 0x80);
    // Each render with the entities added since the last.
    CHECK(cgame.scene().views.size() == 2 && cgame.scene().views[0].entityCount == 2 &&
          cgame.scene().views[1].firstEntity == 2 && cgame.scene().views[1].entityCount == 1 &&
          cgame.scene().views[0].refdef[76] == 0 && cgame.scene().views[1].refdef[76] == 1);
    bool handled = false;
    CHECK(!cgame.consoleCommand("weapnext", handled) && handled);
    CHECK(!cgame.consoleCommand("weapon 3", handled) && !handled);
    // A tag: tag_top half way between the box's two frames.
    int32_t box = 0;
    CHECK(!cgame.call(33, {}, box) && box > 0);
    Md3Model boxModel;
    CHECK(!parseMd3(buildMd3(), boxModel));
    const float halfway = (boxModel.tag(0, "tag_top")->origin[2] + boxModel.tag(1, "tag_top")->origin[2]) / 2;
    CHECK(!cgame.call(32, {box}, r) && r == int(halfway) * 10 + 1);
    CHECK(cgame.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

// The tables switch statements jump through are found and kept from
// writes: a system call writing over one stops the run, named.
static void testJumpTableGuard() {
    Qvm vm;
    CHECK(!vm.load(std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm))));
    CHECK(!vm.protectedRanges().empty());
    if (vm.protectedRanges().empty())
        return;
    // vmMain's (commands 0..33) and test command 32's (cases 100..107):
    // exactly their entries, nothing before them.
    // (They may sit side by side, as one range.)
    size_t bytes = 0;
    for (const auto& [first, end] : vm.protectedRanges())
        bytes += end - first;
    CHECK(bytes == 4 * (34 + 8));
    const auto table = vm.protectedRanges().front();
    vm.setSystemCall([&](Qvm& v, int number, const int32_t*) -> int32_t {
        if (number == 1) // Print, during Init
            v.writeString(int32_t(table.first), "-75", 4);
        return 0;
    });
    int32_t r = 0;
    const Error err = vm.call(1, {}, r);
    CHECK(err && err.message.find("system call 1") != std::string::npos &&
          err.message.find("jump table") != std::string::npos);
    CHECK(vm.readInt(int32_t(table.first)) >= 0 && vm.readInt(int32_t(table.first)) < int32_t(vm.code().size()));
    vm.setSystemCall(nullptr);
    CHECK(!vm.call(0, {}, r) && r == 4); // the table intact, it still works
    const int expected[8] = {1, 7, 3, 9, 2, 8, 4, 6};
    for (int i = 0; i < 8; ++i)
        CHECK(!vm.call(32, {100 + i}, r) && r == expected[i]);
    CHECK(!vm.call(32, {99}, r) && r == 0);
}

// The menus' system calls, called by a QVM the Quake III compiler made
// from tests/data/qvm/fake_ui.c.
static void testUiHost(const std::filesystem::path& tmp) {
    using test::toBytes;
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/ui.qvm", std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm)), {}},
                                    {"scripts/test.txt", toBytes("hello world\n"), {}},
                                    {"scripts/other.txt", toBytes("x"), {}},
                                    {"scripts/sub/deep.txt", toBytes("x"), {}},
                                    {"scripts/a.shader", toBytes("x"), {}},
                                    {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::UiHost ui(set, cvars);
    Error err = ui.load();
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    int32_t r = 0;
    CHECK(!ui.call(ef::UiHost::Init, {}, r) && r == 1202); // 12 bytes, 2 .txt files directly in scripts/
    CHECK(ui.log() == "fake ui\n");
    CHECK(cvars.string("ui_test") == "7" && cvars.find("ui_test")->flags == CvarStore::Archive);
    CHECK(!ui.call(20, {}, r) && r == 7);
    cvars.set("ui_test", "9");
    CHECK(!ui.call(20, {}, r) && r == 7); // the program's copy changes on Cvar_Update
    CHECK(!ui.call(21, {}, r) && r == 9);
    CHECK(!ui.call(25, {}, r) && r == 2);
    CHECK(!ui.call(22, {}, r) && r == 640480);

    CHECK(!ui.call(ef::UiHost::SetActiveMenu, {1}));
    CHECK(!ui.call(ef::UiHost::Refresh, {1234}, r) && r == 1234);
    CHECK(ui.pictures().size() == 3); // two, then the pointer
    if (ui.pictures().size() == 3) {
        const auto& p = ui.pictures()[0];
        CHECK(p.x == 10 && p.y == 20 && p.w == 320 && p.h == 30 && p.s1 == 0 && p.t2 == 0.5f);
        CHECK(p.color[0] == 1 && p.color[1] == 0.5f && p.color[2] == 0 && p.color[3] == 1);
        CHECK(ui.shaders().name(p.shader) == "menu/test");
        CHECK(ui.pictures()[1].color == (std::array<float, 4>{1, 1, 1, 1}));
    }
    CHECK(!ui.call(23, {}, r) && r == 'h' * 1000000 + 'e' * 1000); // "hel", then NUL padding
    CHECK(cvars.string("ui_text") == "hello world\n");
    cvars.set("ui_number", "16");
    CHECK(!ui.call(24, {}, r) && r == 40);
    err = ui.call(26, {}, r);
    CHECK(err && err.message.find("boom") != std::string::npos);
    CHECK(!ui.call(20, {}, r) && r == 9); // usable again after the program's error
    // Typed characters come as keys with 1024 added; plain keys don't type.
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'q', 1}));
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'Q' | 1024, 1}));
    CHECK(!ui.call(27, {}, r) && r == 'Q' * 1000 + 1);

    // The CD key: taken if it has 18 letters and digits, kept in the key
    // file, read back by a new run, which then counts it as checked.
    ui.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!ui.call(28, {1}, r) && r == 0 && !std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    CHECK(!ui.call(28, {0}, r) && r == 1 && std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    // Call 58, the model bounds the main menu asks for: frame 0's box.
    Md3Model box;
    CHECK(!parseMd3(buildMd3(), box));
    const auto& f0 = box.frames[0];
    const int boxCode = int(f0.mins[0] * 10) + int(f0.mins[1] * 100) + int(f0.mins[2] * 1000) +
                        int(f0.maxs[0] * 10000) + int(f0.maxs[1] * 100000) + int(f0.maxs[2] * 1000000);
    CHECK(boxCode != 0);
    CHECK(!ui.call(29, {}, r) && r == boxCode);
    CHECK(!ui.call(29, {99}, r) && r == 0); // no such model: zeros
    CvarStore fresh;
    ef::UiHost again(set, fresh);
    again.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!again.load() && !again.call(ef::UiHost::Init) && fresh.string("ui_cdkeychecked") == "1");

    // A frame with no menu open opens the main one, as the original
    // engine does while not connected; with one open, it's left alone.
    CHECK(!ui.call(31, {}, r) && r == 1);
    ui.clearPictures();
    CHECK(!ui.refresh(2000) && !ui.call(31, {}, r) && r == 1 && ui.pictures().size() == 4);
    CHECK(!ui.call(30));
    ui.clearPictures();
    CHECK(!ui.refresh(2016) && !ui.call(31, {}, r) && r == 2 && ui.pictures().size() == 4);

    // A menu sound: passed on by name with its channel.
    std::vector<std::pair<std::string, int>> played;
    ui.startLocalSound = [&](const std::string& name, int channel) { played.emplace_back(name, channel); };
    CHECK(!ui.call(33));
    CHECK(played.size() == 1 && played[0].first == "sound/interface/button1.wav" && played[0].second == 6);

    CHECK(ui.unhandled().empty());
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
    testMd3();
    testSkinAndPlacement();
    testLightGrid();
    testQvm();
    testCompiledQvm();
    testCvars();
    testVmFileList();
    testSoundLibrary(tmp / "sounds");
    testCollision();
    testGameHost(tmp / "game");
    testCgameHost(tmp / "cgame");
    testJumpTableGuard();
    testUiHost(tmp / "ui");
    testFilesystemAndDetection(tmp / "fs");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
