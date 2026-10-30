// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The games' file formats: text and entities, shaders, maps (BSP, patches, the light grid), models (MD3, MDR, animation).

#include "common/fs.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/server_browser.h"
#include "backends/sockets/socket_net.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/pages/page.h"
#include "common/audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

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

// A small .mdr: two bones, two frames, one triangle whose first vertex
// hangs half off each bone, one tag on bone 1. Frame 0: bone 0 at the
// origin, bone 1 moved 10 along x; frame 1: bone 1 turned a quarter turn
// about z as well. `packed`: the bones as 16-bit numbers.
static std::vector<uint8_t> buildMdr(bool packed) {
    std::vector<uint8_t> d;
    auto i32 = [&](int32_t v) {
        for (int k = 0; k < 4; ++k)
            d.push_back(uint8_t(uint32_t(v) >> (8 * k)));
    };
    auto f32 = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        i32(int32_t(bits));
    };
    auto u16 = [&](int v) {
        d.push_back(uint8_t(v));
        d.push_back(uint8_t(v >> 8));
    };
    auto text = [&](const char* t, size_t size) {
        for (size_t k = 0; k < size; ++k)
            d.push_back(k < std::strlen(t) ? uint8_t(t[k]) : 0);
    };
    using Bone = std::array<float, 12>; // rows of 3 x 4
    const Bone still{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone moved{1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone turned{0, -1, 0, 10, 1, 0, 0, 0, 0, 0, 1, 0};
    const std::array<std::array<Bone, 2>, 2> frames{{{still, moved}, {still, turned}}};
    const size_t frameSize = packed ? 40 + 2 * 24 : 56 + 2 * 48;
    const int32_t framesAt = 104, lodAt = framesAt + 2 * int32_t(frameSize);
    // One vertex: normal, st, one or two weights.
    const size_t surfaceSize = 168 + (24 + 2 * 20) + 2 * (24 + 20) + 12;
    const int32_t tagsAt = lodAt + 12 + int32_t(surfaceSize);
    text("RDM5", 4);
    i32(2);
    text("test", 64);
    i32(2);                              // frames
    i32(2);                              // bones
    i32(packed ? -framesAt : framesAt); // negative: packed
    i32(1);                              // levels of detail
    i32(lodAt);
    i32(1); // tags
    i32(tagsAt);
    i32(tagsAt + 36);
    for (const auto& frame : frames) {
        for (int k = 0; k < 10; ++k)
            f32(0); // bounds, origin, radius
        if (!packed)
            text("frame", 16);
        for (const Bone& b : frame) {
            if (packed) {
                for (int r = 0; r < 3; ++r)
                    u16(int(std::lround(b[size_t(r * 4 + 3)] * 64)) + 32768);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        u16(int(std::lround(b[size_t(r * 4 + c)] * 32766)) + 32768);
            } else {
                for (float v : b)
                    f32(v);
            }
        }
    }
    i32(1);  // surfaces
    i32(12); // the first right after the level of detail
    i32(12 + int32_t(surfaceSize));
    i32(0);
    text("Body", 64);
    text("models/test/body", 64);
    i32(0);
    i32(0);
    i32(3);   // vertices
    i32(168); // right after the surface's header
    i32(1);   // triangles
    i32(int32_t(surfaceSize) - 12);
    i32(0);
    i32(0);
    i32(int32_t(surfaceSize));
    // Vertex 0: half on each bone, 1 along y from each; vertices 1 and 2 on bone 1.
    f32(0), f32(0), f32(1), f32(0.25f), f32(0.5f), i32(2);
    i32(0), f32(0.5f), f32(0), f32(1), f32(0);
    i32(1), f32(0.5f), f32(0), f32(1), f32(0);
    for (int v = 1; v < 3; ++v) {
        f32(1), f32(0), f32(0), f32(0), f32(0), i32(1);
        i32(1), f32(1), f32(float(v)), f32(0), f32(0);
    }
    i32(0), i32(1), i32(2);
    i32(1);
    text("tag_weapon", 32);
    return d;
}

static void testPatchGrids() {
    // A flat 3x3 patch: its middle column and row are straight, so left
    // out: four corners.
    BspVertex control[9];
    for (int i = 0; i < 9; ++i) {
        control[i].pos = {float(i % 3) * 64, float(i / 3) * 64, 0};
        control[i].normal = {0, 0, 1};
    }
    PatchGrid flat = subdividePatch(control, 3, 3, 4);
    CHECK(flat.width == 2 && flat.height == 2 && flat.points.size() == 4);
    CHECK(flat.points[3].pos[0] == 128 && flat.points[3].pos[1] == 128);

    // Bulging up in the middle. Across first: the middle row strays 32
    // units, halved twice (8, then 2 off); that flattens the control
    // points, so down the columns it's halved once (16, then about 4):
    // 9 x 5, as the original's. Every point on the curve, normals from
    // the shape.
    control[4].pos[2] = 64;
    PatchGrid bulge = subdividePatch(control, 3, 3, 4);
    CHECK(bulge.width == 9 && bulge.height == 5);
    const BspVertex& mid = bulge.points[2 * 9 + 4];
    CHECK(std::fabs(mid.pos[0] - 64) < 1e-4f && std::fabs(mid.pos[2] - 16) < 1e-4f);
    CHECK(std::fabs(mid.normal[2] - 1) < 1e-4f);
    // A quarter of the way across the middle row: 64 * (2 * 0.25 * 0.75) *
    // (2 * 0.5 * 0.5) = 12.
    CHECK(std::fabs(bulge.points[2 * 9 + 2].pos[2] - 12) < 1e-4f);
    std::vector<BspVertex> verts;
    std::vector<uint32_t> idx;
    triangulateGrid(bulge, verts, idx);
    CHECK(verts.size() == 45 && idx.size() == 8 * 4 * 6);
    // Finer for a smaller error.
    CHECK(subdividePatch(control, 3, 3, 0.5f).width > 9);

    // Two patches side by side sharing the edge x = 128 (straight). The
    // left one bends along y, so it's cut into rows; the right one is flat,
    // two rows. Stitched, the right one gets the left one's points on the
    // shared edge.
    BspVertex left[9], right[9];
    for (int i = 0; i < 9; ++i) {
        left[i].pos = {float(i % 3) * 64, float(i / 3) * 64, 0};
        right[i].pos = {128 + float(i % 3) * 64, float(i / 3) * 64, 0};
        left[i].normal = right[i].normal = {0, 0, 1};
    }
    left[3].pos[2] = 64; // the left edge's middle raised: rows needed
    std::vector<PatchGrid> grids{subdividePatch(left, 3, 3, 4), subdividePatch(right, 3, 3, 4)};
    CHECK(grids[0].height > 2 && grids[1].height == 2);
    CHECK(stitchPatches(grids) > 0);
    CHECK(grids[1].height == grids[0].height);
    bool matched = true;
    for (int y = 0; y < grids[0].height; ++y) {
        const auto& a = grids[0].points[size_t(y * grids[0].width + grids[0].width - 1)].pos;
        const auto& b = grids[1].points[size_t(y * grids[1].width)].pos;
        matched = matched && std::fabs(a[0] - b[0]) < 0.1f && std::fabs(a[1] - b[1]) < 0.1f &&
                  std::fabs(a[2] - b[2]) < 0.1f;
    }
    CHECK(matched);
    // Not in the same group: left alone.
    std::vector<PatchGrid> apart{subdividePatch(left, 3, 3, 4), subdividePatch(right, 3, 3, 4)};
    apart[1].groupRadius = 1;
    CHECK(stitchPatches(apart) == 0 && apart[1].height == 2);
}

static void testPlayerAnimation() {
    const auto anims = parseAnimationConfig("sex f\nsoundpath alexa\n\n"
                                            "0\t47\t0\t10\t\t// BOTH_DEATH1\n"
                                            "95 18 0 15 // TORSO_GESTURE\n"
                                            "134 2 0 1 // TORSO_STAND\n"
                                            "138 14 14 10 // LEGS_WALKCR\n"
                                            "208 10 10 15 // LEGS_IDLE\n"
                                            "186 6 0 10 // LEGS_JUMP\n");
    CHECK(anims.size() == 6);
    CHECK(anims[0].name == "BOTH_DEATH1" && anims[0].part == PlayerAnimation::Part::Both && anims[0].first == 0);
    CHECK(anims[2].part == PlayerAnimation::Part::Torso && anims[2].first == 134);
    // Legs moved back by 138 - 95 into the legs model.
    CHECK(anims[3].part == PlayerAnimation::Part::Legs && anims[3].first == 95);
    CHECK(anims[4].name == "LEGS_IDLE" && anims[4].first == 165 && anims[4].loop == 10 && anims[4].fps == 15);
    int frame = 0, next = 0;
    float f = 0;
    anims[4].frameAt(0.5f / 15, frame, next, f); // halfway through the first frame
    CHECK(frame == 165 && next == 166 && std::fabs(f - 0.5f) < 1e-4f);
    anims[4].frameAt(9.0f / 15, frame, next, f); // the last frame, on its way round to the first
    CHECK(frame == 174 && next == 165);
    anims[4].frameAt(23.0f / 15, frame, next, f); // looping
    CHECK(frame == 168);
    anims[5].frameAt(100, frame, next, f); // not looping: held on the last
    CHECK(frame == 186 - 43 + 5 && next == frame && f == 0);
}

static void testMdr() {
    for (bool packed : {false, true}) {
        Md3Model m;
        CHECK(!parseModel(buildMdr(packed), m));
        CHECK(m.frames.size() == 2 && m.surfaces.size() == 1 && m.tagCount == 1);
        const Md3Surface& s = m.surfaces[0];
        CHECK(s.name == "body" && s.shaders[0] == "models/test/body" && s.vertexCount == 3);
        CHECK(s.indices == std::vector<uint32_t>({0, 1, 2}) && s.st[0][0] == 0.25f && s.st[0][1] == 0.5f);
        auto near = [](const Vec3& a, Vec3 b) {
            return std::fabs(a[0] - b[0]) < 0.02f && std::fabs(a[1] - b[1]) < 0.02f && std::fabs(a[2] - b[2]) < 0.02f;
        };
        // Frame 0: vertex 0 between (0,1,0) and (10,1,0); vertex 1 at 10 + 1.
        CHECK(near(s.vertices[0].pos, {5, 1, 0}) && near(s.vertices[1].pos, {11, 0, 0}));
        CHECK(near(s.vertices[0].normal, {0, 0, 1}) && near(s.vertices[1].normal, {1, 0, 0}));
        // Frame 1: bone 1 turned, so its offsets are too: (1,0,0) -> (0,1,0).
        CHECK(near(s.vertices[3 + 1].pos, {10, 1, 0}) && near(s.vertices[3 + 2].pos, {10, 2, 0}));
        CHECK(near(s.vertices[3 + 0].pos, {4.5f, 0.5f, 0}) && near(s.vertices[3 + 1].normal, {0, 1, 0}));
        // The tag follows bone 1: its x axis is the matrix's first column.
        const Md3Tag* tag = m.tag(1, "tag_weapon");
        CHECK(tag && near(tag->origin, {10, 0, 0}) && near(tag->axis[0], {0, 1, 0}) && near(tag->axis[1], {-1, 0, 0}));
        // A level of detail past its only one: that one.
        Md3Model low;
        CHECK(!parseModel(buildMdr(packed), low, 2) && low.surfaces.size() == 1 &&
              low.surfaces[0].vertices.size() == s.vertices.size());
    }
    // A broken one: the triangles past the end.
    std::vector<uint8_t> broken = buildMdr(false);
    broken.resize(broken.size() - 60);
    Md3Model m;
    CHECK(parseModel(broken, m));
}

static const reluva::test::Suite suite("idtech3.unit.formats", {
    {"lexer", [](const std::filesystem::path&) { testLexer(); }},
    {"entities", [](const std::filesystem::path&) { testEntities(); }},
    {"shaders", [](const std::filesystem::path&) { testShaders(); }},
    {"shader stages", [](const std::filesystem::path&) { testShaderStages(); }},
    {"waves", [](const std::filesystem::path&) { testWaves(); }},
    {"bsp", [](const std::filesystem::path&) { testBsp(); }},
    {"patches", [](const std::filesystem::path&) { testPatch(); }},
    {"patch grids", [](const std::filesystem::path&) { testPatchGrids(); }},
    {"md3", [](const std::filesystem::path&) { testMd3(); }},
    {"skins and placement", [](const std::filesystem::path&) { testSkinAndPlacement(); }},
    {"light grid", [](const std::filesystem::path&) { testLightGrid(); }},
    {"mdr", [](const std::filesystem::path&) { testMdr(); }},
    {"player animation", [](const std::filesystem::path&) { testPlayerAnimation(); }},
});
