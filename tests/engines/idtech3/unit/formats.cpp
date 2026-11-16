// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The games' file formats: text and entities, shaders, maps (BSP, patches, the light grid), models (MD3, MDR, animation).

#include "common/fs.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/effects.h"
#include "engines/idtech3/raven/roff.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/formats/ghoul2.h"
#include "engines/idtech3/jk2/ghoul2_instances.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/formats/lexer.h"
#include "engines/idtech3/formats/marks.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/render/patch.h"
#include "engines/idtech3/render/world_renderer.h"
#include "engines/idtech3/render/surface_sprites.h"
#include "engines/idtech3/formats/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/client/server_browser.h"
#include "backends/networking/sockets/socket_net.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/raven/fonts.h"
#include "engines/idtech3/raven/strings.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/audio/sounds.h"
#include "engines/idtech3/client/page.h"
#include "audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm/vm_services.h"
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
    const std::string lump =
        "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube\"\n}\n"
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
    const ShaderDef &d = defs[0];
    CHECK(d.cull == ShaderDef::Cull::kNone && d.sort == 10 && d.polygonOffset && d.noMipmaps);
    CHECK(d.deforms.size() == 3);
    CHECK(d.deforms[0].type == ShaderDeform::Type::kWave && d.deforms[0].spread == 1.0f / 64);
    CHECK(d.deforms[0].wave.amplitude == 3 && d.deforms[0].wave.frequency == 0.5f);
    CHECK(d.deforms[1].type == ShaderDeform::Type::kMove && d.deforms[1].vector[2] == 1 &&
          d.deforms[1].wave.func == gfx::WaveFunc::kSawtooth && d.deforms[1].wave.amplitude == 8);
    CHECK(d.deforms[2].type == ShaderDeform::Type::kAutosprite);
    CHECK((d.unknown == std::vector<std::string>{"efmadeupdirective", "efstagething"}));

    const ShaderStage &a = d.stages[0];
    CHECK(a.rgbGen == ShaderStage::RgbGen::kWave && a.rgbWave.func == gfx::WaveFunc::kTriangle &&
          a.rgbWave.phase == 0.25f && a.rgbWave.frequency == 2);
    CHECK(a.alphaGen == ShaderStage::AlphaGen::kConst && a.alphaConst == 0.4f);
    CHECK(a.tcGen == ShaderStage::TcGen::kVector && a.tcVectors[0][0] == 0.01f && a.tcVectors[1][1] == 0.01f);
    CHECK(a.tcMods.size() == 5);
    if (a.tcMods.size() == 5) {
        using M = ShaderStage::TcMod::Type;
        CHECK(a.tcMods[0].type == M::kScroll && a.tcMods[0].v[0] == 0.5f && a.tcMods[0].v[1] == -1);
        CHECK(a.tcMods[1].type == M::kTurb && a.tcMods[1].wave.amplitude == 0.1f && a.tcMods[1].wave.frequency == 0.3f);
        CHECK(a.tcMods[2].type == M::kRotate && a.tcMods[2].v[0] == 90);
        CHECK(a.tcMods[3].type == M::kStretch && a.tcMods[3].wave.base == 1 && a.tcMods[3].wave.amplitude == 0.2f);
        CHECK(a.tcMods[4].type == M::kTransform && a.tcMods[4].v[4] == 0.5f && a.tcMods[4].v[5] == 0.25f);
    }
    CHECK(a.depthEqual && a.depthWrite && a.alphaFunc == gfx::AlphaTest::kGe128);

    const ShaderStage &b = d.stages[1];
    CHECK(b.whiteImage && b.rgbGen == ShaderStage::RgbGen::kConst && b.rgbConst[1] == 0.5f && b.rgbConst[2] == 1);
    CHECK(b.alphaGen == ShaderStage::AlphaGen::kOneMinusVertex && b.tcGen == ShaderStage::TcGen::kEnvironment);
    // turb with the (ignored) function name written
    CHECK(b.tcMods.size() == 1 && b.tcMods[0].wave.amplitude == 0.2f && b.tcMods[0].wave.phase == 0.5f);

    const ShaderDef &sky = defs[1];
    CHECK(sky.sky && sky.skyFarBox == "env/space" && sky.cloudHeight == 512 && sky.sort == 7);
    CHECK(sky.fog && sky.fogColor[0] == 0.5f && sky.fogColor[1] == 0.25f && sky.fogDistance == 300);

    // rgbGen vertex brings the vertices' alpha, unless an alphaGen came first.
    defs.clear();
    CHECK(!parseShaderFile("a\n{\n{\nmap $whiteimage\nrgbGen vertex\n}\n{\nmap b\nalphaGen const 0.5\nrgbGen "
                           "vertex\n}\n}\n",
                           "y.shader", defs));
    CHECK(defs.size() == 1 && defs[0].stages.size() == 2);
    if (defs.size() == 1 && defs[0].stages.size() == 2) {
        CHECK(defs[0].stages[0].alphaGen == ShaderStage::AlphaGen::kVertex);
        CHECK(defs[0].stages[1].alphaGen == ShaderStage::AlphaGen::kConst);
    }
}

static void testWaves() {
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    gfx::Wave w{gfx::WaveFunc::kSin, 1, 2, 0, 1};
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::kTriangle;
    CHECK(approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.5f), 1) &&
          approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::kSquare;
    CHECK(approx(gfx::evaluate(w, 0.1f), 3) && approx(gfx::evaluate(w, 0.6f), -1));
    w.func = gfx::WaveFunc::kSawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 1.5f) && approx(gfx::evaluate(w, 1.25f), 1.5f));
    w.func = gfx::WaveFunc::kInverseSawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 2.5f));
    w = {gfx::WaveFunc::kSin, 0, 1, 0.25f, 0}; // no frequency: constant, set by the phase
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 100), 1));
    w.func = gfx::WaveFunc::kNoise;
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
        const ShaderDef &f = defs[0];
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
    ShaderDef first;
    ShaderDef second;
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

static void testSurfaceSprites() {
    // Raven's sprite stage and its options; values out of range leave what
    // was there; only the first facing counts; the sky's directives with
    // nothing to draw are known.
    const char *text = "textures/grass\n{\n notc\n sun 1 1 1 100 45 45\n lightColor 1 1 1\n"
                       " {\n map textures/mud\n }\n"
                       " {\n map gfx/grass\n surfaceSprites vertical 10 16 30 300\n ssVariance 1 2\n"
                       "   ssWind 1\n ssFademax 200\n ssHangdown\n ssFaceup\n blendFunc GL_ONE GL_ONE\n }\n"
                       " {\n map gfx/bubble\n surfaceSprites effect 2 1 36 400\n ssFXWeather\n"
                       "   ssFXAlphaRange 0.5 2\n ssMadeUp\n }\n"
                       "}\n";
    std::vector<ShaderDef> defs;
    CHECK(!parseShaderFile(text, "t.shader", defs) && defs.size() == 1);
    if (defs.size() != 1 || defs[0].stages.size() != 3)
        return;
    const ShaderDef &d = defs[0];
    CHECK((d.unknown == std::vector<std::string>{"ssmadeup"}));
    const SurfaceSprites &grass = d.stages[1].sprites;
    CHECK(grass.type == SurfaceSprites::Type::kVertical && grass.width == 10 && grass.density == 30);
    CHECK(grass.variance[1] == 2 && grass.wind == 1 && grass.windIdle == 1);
    CHECK(std::fabs(grass.fadeMax - 300 * 1.33f) < 1e-3f); // 200 isn't past the fade's start
    CHECK(grass.facing == SurfaceSprites::Facing::kDown);
    const SurfaceSprites &bubbles = d.stages[2].sprites;
    CHECK(bubbles.type == SurfaceSprites::Type::kWeather && bubbles.fxAlphaStart == 0.5f && bubbles.fxAlphaEnd == 0);
    CHECK(d.stages[0].sprites.type == SurfaceSprites::Type::kNone);

    // Scattered over a flat floor 256 units square, facing up; none on a
    // wall. Near the eye they're drawn as quads standing their height;
    // far off, past fadeMax, none are.
    std::vector<gfx::Vertex> verts(7);
    const float floor[4][2] = {{0, 0}, {256, 0}, {256, 256}, {0, 256}};
    for (size_t i = 0; i < 4; ++i) {
        verts[i].pos[0] = floor[i][0];
        verts[i].pos[1] = floor[i][1];
        verts[i].normal[2] = 1;
        verts[i].color[2] = 200;
    }
    for (size_t i = 4; i < 7; ++i) {
        verts[i].pos[2] = float(i) * 10;
        verts[i].normal[0] = 1;
    }
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3, 4, 5, 6};
    SurfaceSprites upright = grass;
    upright.facing = SurfaceSprites::Facing::kNormal;
    upright.windIdle = 0;
    SurfaceSpriteSet set;
    set.add(7, 3, -1, 6, false, upright, verts, idx, 9);
    CHECK(!set.empty() && set.spriteCount() > 20 && set.spriteCount() < 200);
    std::vector<gfx::Vertex> out;
    std::vector<uint32_t> outIdx;
    std::vector<SurfaceSpriteSet::Part> parts;
    auto all = [](int) { return true; };
    set.build({128, 128, 40}, {0, 1, 0}, {0, 0, 1}, 0, 0, 10, all, out, outIdx, parts);
    CHECK(parts.size() == 1 && parts[0].material == 3 && outIdx.size() == out.size() / 4 * 6 && !out.empty());
    float top = 0;
    for (const gfx::Vertex &v : out)
        top = std::max(top, v.pos[2]);
    // The corners' light weighted by where the sprite stands, cut to a
    // byte as the original cuts it: 200, or just under.
    CHECK(top >= 16 && top <= 16 * 3 && out[0].color[0] >= 199 && out[0].color[0] <= 200);
    out.clear();
    parts.clear();
    set.build({128, 2000, 40}, {0, 1, 0}, {0, 0, 1}, 0, 0, 10, all, out, outIdx, parts);
    CHECK(parts.empty() && out.empty());
    set.build({128, 128, 40}, {0, 1, 0}, {0, 0, 1}, 0, 0, 10, [](int s) { return s != 7; }, out, outIdx, parts);
    CHECK(parts.empty());
}

static void testGhoul2() {
    using namespace fixtures;
    GlaSkeleton gla;
    Error err = parseGla(buildGla(), gla);
    CHECK(!err && gla.bones.size() == 2 && gla.frameCount == 2 && gla.pool.size() == 3);
    CHECK(gla.bones.size() == 2 && gla.bones[0].name == "child" && gla.bones[0].parent == 1 &&
          gla.bones[1].children == std::vector<int>{0});
    GlmModel glm;
    err = parseGlm(buildGlm(), glm);
    CHECK(!err && glm.animName == "test/skel" && glm.boneCount == 2 && glm.surfaces.size() == 1);
    if (err || gla.bones.size() != 2 || glm.lods.size() != 1)
        return;
    CHECK(glm.surfaces[0].name == "body" && glm.surfaces[0].shader == "textures/x" && glm.surfaces[0].flags == 0);
    const GlmSurface &s = glm.lods[0][0];
    CHECK(s.boneRefs == (std::vector<int32_t>{0, 1}) && s.indices.size() == 3 && s.st[2][0] == 1);
    CHECK(s.vertices[2].weightCount == 2 && std::fabs(s.vertices[2].weight[0] - 341.0f / 1023) < 1e-6f &&
          std::fabs(s.vertices[2].weight[1] - (1 - 341.0f / 1023)) < 1e-6f);

    // The child placed on its parent, though it comes first; frame 1 lifts
    // both; halfway between, half as much.
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    std::vector<Bone34> bones;
    std::vector<Md3Vertex> posed;
    poseBones(&gla, 0, 0, 0, bones);
    skinSurface(s, bones, posed);
    CHECK(approx(posed[0].pos[0], 0) && approx(posed[1].pos[0], 10) && approx(posed[1].pos[2], 0));
    CHECK(approx(posed[2].pos[0], 10.0f / 3) && approx(posed[2].normal[2], 1));
    poseBones(&gla, 1, 1, 0, bones);
    skinSurface(s, bones, posed);
    CHECK(approx(posed[0].pos[2], 5) && approx(posed[1].pos[0], 10) && approx(posed[1].pos[2], 5));
    poseBones(&gla, 1, 0, 0.5f, bones);
    skinSurface(s, bones, posed);
    CHECK(approx(posed[1].pos[2], 2.5f));
    poseBones(nullptr, 0, 0, 0, bones); // "*default": one still bone
    CHECK(bones.size() == 1 && bones[0][0] == 1 && bones[0][3] == 0);

    // A vertex past the surface's, a bone past the model's, a truncated
    // file: refused.
    CHECK(parseGlm(buildGlm(3), glm));
    std::vector<uint8_t> bad = buildGla();
    bad.erase(bad.end() - 14, bad.end());
    CHECK(parseGla(bad, gla)); // frame 1's root bone is past the pool now
    CHECK(parseGlm({'2', 'L', 'G', 'M'}, glm));

    // Raven's animation.cfg: the name first; -1 doesn't loop; a negative
    // speed plays it backwards.
    const auto anims = parseGhoul2AnimationConfig("// comment\nBOTH_A 10 4 -1 20\nBOTH_B\t20\t3\t0\t-10\n");
    CHECK(anims.size() == 2 && anims[0].name == "BOTH_A" && anims[0].loop == 0 && anims[1].loop == 3);
    if (anims.size() == 2) {
        int frame = 0;
        int next = 0;
        float fraction = 0;
        anims[0].frameAt(1, frame, next, fraction); // long past its end: held
        CHECK(frame == 13 && next == 13);
        anims[1].frameAt(0, frame, next, fraction);
        CHECK(anims[1].backwards && frame == 22 && next == 21);
        anims[1].frameAt(0.35f, frame, next, fraction); // round again
        CHECK(frame == 22);
    }
}

static void testRavenText() {
    using namespace raven;
    // A string package: its header, entries by INDEX, text from the first
    // quote to the last (quotes inside kept), \n a line break.
    const char *text = "VERSION 1\r\nCONFIG W:\\bin\\striped.cfg\r\nID 7\r\nREFERENCE INGAME\r\n"
                       "DESCRIPTION \"in game text\"\r\nCOUNT 2\r\nINDEX 0\r\n{\r\n   REFERENCE OBJNONE\r\n"
                       "   TEXT_LANGUAGE1 \"NONE\"\r\n   TEXT_LANGUAGE3 \"KEINE\"\r\n   FLAGS SP_FLAG9\r\n}\r\n"
                       "INDEX 1\r\n{\r\n   REFERENCE SAY\r\n   NOTES \"a note\"\r\n"
                       "   TEXT_LANGUAGE1 \"When you say \"troubling.\"\\nAgain\"\r\n}\r\n";
    StringPackage p;
    CHECK(!parseStringPackage(text, p) && p.reference == "INGAME" && p.id == 7 && p.description == "in game text");
    CHECK(p.entries.size() == 2 && p.entries[0].reference == "OBJNONE" && p.entries[0].text[2] == "KEINE");
    CHECK(p.entries.size() == 2 && p.entries[0].flags == "SP_FLAG9" && p.entries[1].notes == "a note");
    CHECK(p.entries.size() == 2 && p.entries[1].index == 1 &&
          p.entries[1].text[0] == "When you say \"troubling.\"\nAgain");
    CHECK(parseStringPackage("INDEX 0\n{\nREFERENCE A\n", p));   // left open
    CHECK(parseStringPackage("INDEX 0\n{\nWHAT \"x\"\n}\n", p)); // unknown key

    // Found by PACKAGE_ENTRY whatever its case; a language without text
    // gives the English.
    SearchSet set;
    auto zip = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_strip.pk3";
    test::writeFile(path, test::buildZip({{"strip/ingame.sp", test::toBytes(text), {}}}));
    CHECK(!zip->open(path));
    set.add(std::move(zip), 0);
    StringTable table;
    table.load(set);
    const std::string *none = table.find("ingame_objnone", StringTable::Language::kGerman);
    const std::string *say = table.find("INGAME_SAY", StringTable::Language::kFrench);
    CHECK(table.size() == 2 && none && *none == "KEINE" && say && say->rfind("When", 0) == 0 && !table.find("X_Y"));
    std::filesystem::remove(path);

    // A font: 'A' 10 wide, advancing 12, 2 to the right, standing 8 up;
    // '.' 4 wide, advancing 5; ' ' advancing 6; others none.
    std::vector<uint8_t> dat(7180, 0);
    auto glyph = [&](int c, int16_t w, int16_t h, int16_t advance, int16_t offset, int32_t baseline) {
        uint8_t *g = &dat[size_t(c) * 28];
        for (auto [at, v] : {std::pair<int, int>{0, w}, {2, h}, {4, advance}, {6, offset}}) {
            g[at] = uint8_t(v);
            g[at + 1] = uint8_t(v >> 8);
        }
        std::memcpy(g + 8, &baseline, 4);
        const float st[4] = {0.25f, 0.5f, 0.75f, 1};
        std::memcpy(g + 12, st, 16);
    };
    glyph('A', 10, 12, 12, 2, 8);
    glyph('.', 4, 4, 5, 0, 2);
    glyph(' ', 0, 0, 6, 0, 0);
    for (auto [at, v] : {std::pair<size_t, int>{7168, 16}, {7170, 14}, {7172, 12}, {7174, 4}})
        dat[at] = uint8_t(v);
    Font font;
    CHECK(!parseFontdat(dat, "anewhope", font) && font.pointSize == 16 && font.height == 14 && !font.roundSizes);
    CHECK(font.glyphs['A'].baseline == 8 && font.glyphs['A'].s2 == 0.75f && font.glyph('B').width == 4);
    CHECK(font.textWidth("A A\nA", 1) == 12 + 6 + 12 && font.textWidth("^1A", 1) == 5 + 5 + 12);
    // From (100, 50): the baseline 14 - 4/2 = 12 down; 'A' 2 right of the
    // pen, its top 8 above the baseline; then a space; ^1 colours what
    // follows; a missing letter is a full stop; a new line 16 down.
    const auto pieces = font.layout("A ^1A\nB", 100, 50, 1);
    CHECK(pieces.size() == 3);
    if (pieces.size() == 3) {
        CHECK(pieces[0].x == 102 && pieces[0].y == 62 - 8 && pieces[0].w == 10 && pieces[0].colour == -1);
        CHECK(pieces[1].x == 100 + 12 + 6 + 2 && pieces[1].colour == 1);
        CHECK(pieces[2].x == 100 && pieces[2].y == 62 + 16 - 2 && pieces[2].w == 4);
    }
    CHECK(font.layout("AAAA", 0, 0, 1, 30).size() == 2); // the third would pass 30
    // Three letters and spaces at most; colour codes and new lines don't count.
    CHECK(font.layout("^1A\nA AA", 0, 0, 1, -1, 3).size() == 2);
    CHECK(parseFontdat(std::vector<uint8_t>(100), "x", font));
}

// Jedi Outcast's electricity as Raven's renderer shapes it.
static void testElectricity() {
    using raven::BoltStrip;
    using raven::electricityStrips;
    auto none = [] { return 0.0f; };
    auto same = [](const BoltStrip &x, const BoltStrip &y) {
        return x.from == y.from && x.to == y.to && x.fromWidth == y.fromWidth && x.toWidth == y.toWidth;
    };
    // 100 units: steps at 20, 40, 60, 80, and the last at the end (100 + 20
    // is past it); three strips each, from the step's point back to the
    // last one's.
    const std::vector<BoltStrip> a = electricityStrips({0, 0, 0}, {100, 0, 0}, 4, 1, 5, false, none);
    CHECK(a.size() == 15);
    if (a.size() == 15) {
        CHECK(a[2].to == (std::array<float, 3>{0, 0, 0}));      // the first step back to the start
        CHECK(a[12].from == (std::array<float, 3>{100, 0, 0})); // the last from the end
        CHECK(a[0].fromWidth == 4 && a[14].toWidth == 4);       // not tapered
        CHECK(a[1].from == a[2].from && a[0].to == a[1].to);    // the strips meet at the kinks
    }
    // The seed fixes the path: the same again, another seed elsewhere.
    const std::vector<BoltStrip> again = electricityStrips({0, 0, 0}, {100, 0, 0}, 4, 1, 5, false, none);
    CHECK(again.size() == a.size() && std::equal(a.begin(), a.end(), again.begin(), same));
    const std::vector<BoltStrip> other = electricityStrips({0, 0, 0}, {100, 0, 0}, 4, 1, 6, false, none);
    CHECK(other.size() == a.size() && !same(other[0], a[0]));
    // Tapered: 1 - how far squared, nothing at the end; 80 of the way, 4 x 0.36.
    const std::vector<BoltStrip> t = electricityStrips({0, 0, 0}, {100, 0, 0}, 4, 1, 5, true, none);
    CHECK(t.size() == 15);
    if (t.size() == 15)
        CHECK(t[12].fromWidth == 0 && std::fabs(t[14].toWidth - 4 * 0.36f) < 1e-4f);
    // Shorter than a step: nothing.
    CHECK(electricityStrips({0, 0, 0}, {15, 0, 0}, 4, 1, 5, false, none).empty());
}

static void testEffects() {
    using raven::EffectPrimitive;
    using raven::EffectSystem;
    // An effect file: its pairs, lists and groups; a primitive the engine
    // doesn't know ("flash") skipped.
    const char *text = "// a test\n"
                       "Particle\n{\n"
                       "\tcount 2 4\n"
                       "\tlife 1000\n"
                       "\tflags useAlpha\n"
                       "\tspawnFlags cheapOrgCalc absoluteVel\n"
                       "\torigin 1 2 3 4 5 6\n"
                       "\tvelocity 100 0 0\n"
                       "\talpha\n\t{\n\t\tstart 1\n\t\tend 0\n\t\tflags linear\n\t}\n"
                       "\tsize\n\t{\n\t\tstart 2 3\n\t\tflags linear nonlinear\n\t}\n"
                       "\tshaders\n\t[\n\t\tgfx/a\n\t\tgfx/b\n\t]\n"
                       "}\n"
                       "Flash\n{\n\tlife 5\n}\n"
                       "Sound\n{\n\tsounds\n\t[\n\t\tsound/x.wav\n\t]\n}\n";
    raven::EffectFile file;
    std::vector<std::string> warnings;
    CHECK(raven::parseEffectFile(text, file, &warnings));
    CHECK(file.primitives.size() == 2 && warnings.size() == 1);
    const EffectPrimitive &p = file.primitives[0];
    CHECK(p.type == EffectPrimitive::kParticle && p.count.min == 2 && p.count.max == 4 && p.life.min == 1000);
    CHECK(p.flags == (EffectPrimitive::kUseAlpha | EffectPrimitive::kLinear |
                      ((EffectPrimitive::kLinear | EffectPrimitive::kNonLinear) << EffectPrimitive::kSizeShift)));
    CHECK(p.spawnFlags == (EffectPrimitive::kCheapOrgCalc | EffectPrimitive::kAbsoluteVelocity));
    CHECK(p.origin[0].min == 1 && p.origin[0].max == 4 && p.origin[2].max == 6);
    CHECK(p.size.start.min == 2 && p.size.start.max == 3 && p.alpha.end.min == 0);
    CHECK(p.media.size() == 2 && p.media[1].second == "gfx/b");
    CHECK(file.primitives[1].type == EffectPrimitive::kSound && file.primitives[1].media.size() == 1);

    // Played: a particle moving and fading at once, a line after its delay.
    SearchSet set;
    auto zip = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_effects.pk3";
    const std::string efx = "Particle\n{\n\tlife 1000\n\tflags useAlpha\n\tspawnFlags absoluteVel\n"
                            "\tvelocity 100 0 0\n\talpha\n\t{\n\t\tstart 1\n\t\tend 0\n\t\tflags linear\n\t}\n"
                            "\tshader gfx/a\n}\n"
                            "Line\n{\n\tdelay 500\n\tlife 1000\n\tspawnFlags cheapOrg2Calc\n\torigin2 10 0 0\n"
                            "\tshader gfx/b\n}\n";
    test::writeFile(path, test::buildZip({{"effects/test.efx", std::vector<uint8_t>(efx.begin(), efx.end()), {}}}));
    CHECK(!zip->open(path));
    set.add(std::move(zip), 0);
    std::vector<std::string> shaders;
    EffectSystem::Host host;
    host.registerShader = [&](const std::string &n) {
        shaders.push_back(n);
        return int32_t(shaders.size());
    };
    EffectSystem fx(set, host);
    const int32_t id = fx.registerEffect("effects/test.efx");
    CHECK(id == 1 && fx.registerEffect("TEST") == 1 && fx.registerEffect("missing") == 0);
    CHECK(shaders == std::vector<std::string>({"gfx/a", "gfx/b"}));
    const EffectSystem::Axis view{EffectSystem::Vec3{1, 0, 0}, EffectSystem::Vec3{0, 1, 0},
                                  EffectSystem::Vec3{0, 0, 1}};
    fx.setTime(0, {-100, 0, 0}, view);
    fx.play(id, {0, 0, 0}, EffectSystem::Vec3{1, 0, 0});
    CHECK(fx.live() == 1 && fx.scheduled() == 1);
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    EffectSystem::Frame frame;
    fx.setTime(100, {-100, 0, 0}, view);
    fx.run(frame);
    // 100 ms at 100 units a second: 10, and half again of the velocity
    // times the time squared, as the original moves things.
    CHECK(frame.drawn.size() == 1 && frame.drawn[0].kind == EffectSystem::Drawn::kSprite);
    CHECK(near(frame.drawn[0].origin[0], 10.5f) && frame.drawn[0].media == 1);
    CHECK(frame.drawn[0].rgba[3] == uint8_t(0.9f * 255));
    frame = {};
    fx.setTime(600, {-100, 0, 0}, view);
    fx.run(frame);
    CHECK(frame.drawn.size() == 2 && fx.scheduled() == 0);
    CHECK(frame.drawn[1].kind == EffectSystem::Drawn::kLine && near(frame.drawn[1].end[0], 10));
    // Past the particle's life: only the line is left.
    frame = {};
    fx.setTime(1200, {-100, 0, 0}, view);
    fx.run(frame);
    CHECK(frame.drawn.size() == 1 && frame.drawn[0].kind == EffectSystem::Drawn::kLine && fx.live() == 1);
    fx.clear();
    CHECK(fx.live() == 0 && fx.registered() == 0);
    std::filesystem::remove(path);
}

static void testGhoul2Instances() {
    // buildGla's skeleton (the child 10 along x from the root; frame 1
    // lifts the root 5) and buildGlm's mesh on it.
    using jk2::Ghoul2Instances;
    SearchSet set;
    auto zip = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_ghoul2.pk3";
    test::writeFile(path, test::buildZip({{"test/model.glm", fixtures::buildGlm(), {}},
                                          {"test/skel.gla", fixtures::buildGla(), {}}}));
    CHECK(!zip->open(path));
    set.add(std::move(zip), 0);
    Ghoul2Instances g2(set);
    int32_t h = 0;
    CHECK(g2.addModel(h, "test/model.glm", 0, 0) == 0 && h == 1);
    CHECK(g2.addModel(h, "test/missing.glm", 0, 0) == -1);
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // A bolt on the child: where it is in the world, the instance at
    // (100, 0, 0). The root stands on the original's quarter turn (the
    // skeleton's x along the world's y), and the bolt's axes are turned a
    // further 270 degrees of yaw, so they come out as the world's.
    const int32_t bolt = g2.addBolt(h, 0, "child");
    CHECK(bolt == 0 && g2.addBolt(h, 0, "CHILD") == 0 && g2.addBolt(h, 0, "*body") == -1);
    Bone34 m{};
    CHECK(g2.boltMatrix(h, 0, bolt, 0, {0, 0, 0}, {100, 0, 0}, {0, 0, 0}, m));
    CHECK(near(m[3], 100) && near(m[7], 10) && near(m[11], 0));
    CHECK(near(m[0], 1) && near(m[4], 0) && near(m[1], 0) && near(m[5], 1));
    // An animation on the root: the child follows it; looping, it comes
    // round again; held, it stays on its last frame.
    CHECK(g2.setAnimation(h, 0, "root", 0, 2, Ghoul2Instances::kAnimLoop, 1, 0, -1));
    auto boltAt = [&](int32_t time) {
        g2.boltMatrix(h, 0, bolt, time, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, m);
        return m[11];
    };
    CHECK(near(boltAt(50), 5) && near(boltAt(25), 2.5f) && near(boltAt(100), 0));
    CHECK(g2.setAnimation(h, 0, "root", 0, 2, Ghoul2Instances::kAnimOverride, 1, 0, -1));
    CHECK(near(boltAt(500), 5));
    CHECK(!g2.setAnimation(h, 0, "nobody", 0, 2, 0, 1, 0, -1));
    // Blending (0x80): frozen on frame 1 (5 up), then frame 0 blended in
    // over 100 ms from 500; set again then, only the blend's length
    // changes. An animation that wasn't frozen is blended from frame 0,
    // which the original's weights (the other way round) take as frame 1.
    constexpr int32_t kFreeze = Ghoul2Instances::kAnimFreeze;
    constexpr int32_t kBlend = Ghoul2Instances::kAnimBlend;
    CHECK(g2.setAnimation(h, 0, "root", 0, 2, kFreeze, 1, 0, -1));
    CHECK(g2.setAnimation(h, 0, "root", 0, 1, kFreeze | kBlend, 1, 500, -1, 100));
    CHECK(near(boltAt(500), 5) && near(boltAt(525), 3.75f) && near(boltAt(600), 0));
    CHECK(g2.setAnimation(h, 0, "root", 0, 1, kFreeze | kBlend, 1, 500, -1, 200));
    CHECK(near(boltAt(550), 3.75f) && near(boltAt(700), 0));
    CHECK(g2.setAnimation(h, 0, "root", 0, 2, Ghoul2Instances::kAnimOverride, 1, 0, -1));
    CHECK(g2.setAnimation(h, 0, "root", 0, 1, kFreeze | kBlend, 1, 500, -1, 100));
    CHECK(near(boltAt(500), 5) && near(boltAt(550), 2.5f));
    // An angle override turning the root a quarter about its up axis
    // carries the child round with it.
    CHECK(g2.setAnimation(h, 0, "root", 0, 1, Ghoul2Instances::kAnimOverride, 1, 0, -1));
    CHECK(g2.setAngles(h, 0, "root", {0, 90, 0}, Ghoul2Instances::kAnglesPostMult, Ghoul2Instances::kPositiveZ,
                       Ghoul2Instances::kOrigin, Ghoul2Instances::kOrigin));
    g2.boltMatrix(h, 0, bolt, 0, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, m);
    CHECK(near(m[3], -10) && near(m[7], 0));
    CHECK(g2.setAngles(h, 0, "root", {0, 0, 0}, 0, Ghoul2Instances::kOrigin, Ghoul2Instances::kOrigin,
                       Ghoul2Instances::kOrigin));
    // A second model hung from the first's bolt; a surface turned off.
    CHECK(g2.addModel(h, "test/model.glm", 1, 0) == 1);
    g2.setBoltLink(h, 1, (0 << 10) | bolt);
    CHECK(g2.setSurfaceOnOff(h, "body", 2));
    std::vector<Ghoul2Instances::Drawn> drawn;
    CHECK(g2.drawn(h, 0, drawn) && drawn.size() == 2);
    if (drawn.size() == 2) {
        // The hung model on the bolt (10 along y), the first on nothing more.
        CHECK(near(drawn[1].placement[7], 10) && near(drawn[1].placement[3], 0) && near(drawn[0].placement[3], 0));
        CHECK(near(drawn[0].bones[0][1], -1) && near(drawn[1].bones[0][0], 1));
        CHECK(!drawn[0].surfaceOn[0] && !drawn[1].surfaceOn[0]);
    }
    // A copy of the instance, then the first cleaned away.
    const int32_t copy = g2.duplicate(h);
    CHECK(copy != h && g2.hasModel(copy, 1) && g2.skeletonName(copy, 0) == "test/skel");
    CHECK(g2.removeModel(h, 1) && !g2.hasModel(h, 1));
    g2.remove(h);
    CHECK(!g2.has(h) && g2.has(copy));
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
    CHECK(bsp.vertices.size() == 3 && bsp.vertices[1].pos[0] == 32 && bsp.vertices[0].color[0][1] == 128);
    CHECK(bsp.indices.size() == 3 && bsp.surfaces.size() == 1);
    CHECK(bsp.surfaces[0].type == SurfaceType::kPlanar && bsp.surfaces[0].fog == -1);
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
    CHECK(bsp.surfaces[0].lightmap[0] == -1 && bsp.surfaces[0].fog == -1);
    CHECK(bsp.fixes.lightmaps == 1 && bsp.fixes.fogs == 1 && bsp.fixes.total() == 2);
    CHECK(!parseBsp(buildBsp(46, 7, 0, -1, 4), bsp)); // a flare's bad index doesn't matter
    CHECK(bsp.surfaces[0].type == SurfaceType::kFlare && bsp.surfaces[0].numVertices == 0 && bsp.fixes.flares == 1);
    CHECK(!parseBsp(buildBsp(46, -1, 0, -1, 9), bsp));
    CHECK(bsp.surfaces[0].type == SurfaceType::kBad && bsp.fixes.types == 1);
    auto truncated = buildBsp();
    truncated.pop_back();
    CHECK(parseBsp(truncated, bsp));
    CHECK(parseBsp({'I', 'B', 'S', 'P'}, bsp));

    // Raven's: brush sides name their surface; a fog may have no brush (the
    // whole level's); vertices and surfaces have four light styles; the
    // light grid's points are shared through its index lump.
    err = parseBsp(buildBsp(1, -1, 0, -1, 1, true), bsp);
    CHECK(!err && bsp.raven && bsp.brushSides.size() == 1 && bsp.brushSides[0].surface == 0);
    CHECK(bsp.fogs.size() == 1 && bsp.fogs[0].brush == -1);
    CHECK(bsp.vertices.size() == 3 && bsp.vertices[1].pos[0] == 32 && bsp.vertices[0].color[1][0] == 127);
    const BspSurface &styled = bsp.surfaces[0];
    CHECK(styled.lightmapStyles[1] == 2 && styled.vertexStyles[2] == kNoStyle && styled.lightmap[1] == 1);
    CHECK(styled.lightmapPos[1][0] == 16 && styled.lightmapSize[0] == 16 && styled.lightmapVecs[2][2] == 1);
    CHECK(bsp.lightmapCount() == 2 && bsp.gridPoints.size() == 2 && bsp.gridIndex.size() == 9);
    CHECK(bsp.gridPoints[1].styles[0] == kNoStyle && bsp.gridIndex[8] == 1 && bsp.fixes.total() == 0);
    CHECK(parseBsp(buildBsp(46, -1, 0, -1, 1, true), bsp)); // RBSP is version 1
}

static void testPatch() {
    // A 3x3 patch bulging up in the middle: every control point at height
    // 0 but the centre one at 4, normals pointing up.
    BspVertex control[9];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            BspVertex &v = control[y * 3 + x];
            v.pos = {float(x) * 8, float(y) * 8, x == 1 && y == 1 ? 4.0f : 0.0f};
            v.st = {float(x) / 2, float(y) / 2};
            v.normal = {0, 0, 1};
            v.color[0] = {200, 100, 0, 255};
        }
    std::vector<BspVertex> verts(1); // something already there: indices must be offset
    std::vector<uint32_t> idx;
    tessellatePatch(control, 3, 3, 4, verts, idx);
    CHECK(verts.size() == 1 + 25 && idx.size() == 16 * 6);
    // Corners stay put; the middle is the Bezier point: 4 * (2 * 0.5 * 0.5)^2 = 1.
    CHECK(verts[1].pos[0] == 0 && verts[1].pos[2] == 0 && verts[25].pos[0] == 16 && verts[25].pos[1] == 16);
    const BspVertex &mid = verts[1 + 12];
    CHECK(std::fabs(mid.pos[0] - 8) < 1e-5f && std::fabs(mid.pos[2] - 1) < 1e-5f);
    CHECK(std::fabs(mid.st[0] - 0.5f) < 1e-5f && mid.color[0][0] == 200 && std::fabs(mid.normal[2] - 1) < 1e-5f);
    bool inRange = true;
    bool clockwise = true;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        for (int k = 0; k < 3; ++k)
            inRange = inRange && idx[i + k] >= 1 && idx[i + k] < verts.size();
        // Clockwise seen from above (where the normals point): the
        // geometric normal points down.
        const auto &a = verts[idx[i]].pos;
        const auto &b = verts[idx[i + 1]].pos;
        const auto &c = verts[idx[i + 2]].pos;
        const float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        clockwise = clockwise && nz < 0;
    }
    CHECK(inRange && clockwise);
    // Normals pointing down flip the winding.
    for (BspVertex &v : control)
        v.normal = {0, 0, -1};
    std::vector<BspVertex> down;
    std::vector<uint32_t> downIdx;
    tessellatePatch(control, 3, 3, 4, down, downIdx);
    const auto &a = down[downIdx[0]].pos;
    const auto &b = down[downIdx[1]].pos;
    const auto &c = down[downIdx[2]].pos;
    CHECK((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0);
    // Two blocks side by side share their edge: 5x3 control points.
    std::vector<BspVertex> wide(15, control[0]);
    std::vector<BspVertex> out;
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
    const Md3Tag *top = m.tag(1, "tag_top");
    CHECK(top && top->origin[2] == 80 && top->axis[2][2] == 1 && !m.tag(0, "tag_head") && !m.tag(5, "tag_top"));
    const Md3Surface &s = m.surfaces[0];
    CHECK(s.name == "box" && s.shaders.size() == 1 && s.shaders[0] == "models/test/box");
    CHECK((s.indices == std::vector<uint32_t>{0, 1, 2}) && s.vertexCount == 3 && s.vertices.size() == 6);
    CHECK(s.st[1][0] == 1 && s.st[2][1] == 1);
    CHECK(s.vertices[1].pos[0] == 8 && s.vertices[2].pos[1] == 8 && s.vertices[3 + 0].pos[2] == 64);
    // Normal 0 (both angles 0) points up; lat 64 lng 64 (a quarter turn each) along +y.
    CHECK(std::fabs(s.vertices[0].normal[2] - 1) < 1e-5f);
    CHECK(std::fabs(s.vertices[1].normal[1] - 1) < 1e-5f && std::fabs(s.vertices[1].normal[2]) < 1e-5f);

    CHECK(parseMd3(buildMd3(3), m)); // a triangle using a vertex past the end
    auto cut = buildMd3();
    cut.erase(cut.end() - 4, cut.end());
    CHECK(parseMd3(cut, m));
    CHECK(parseMd3({'I', 'D', 'P', '2'}, m));

    // An empty surface as the Expansion Pack's k_chair1.md3 has: 0 frames,
    // one shader, no vertices or triangles, every offset 0. The original's
    // loader takes it (its shader name read from the surface's own start),
    // and draws the rest of the model.
    auto withEmpty = buildMd3();
    withEmpty[84] = 2; // two surfaces
    const char header[] = "IDP3line01";
    withEmpty.insert(withEmpty.end(), header, header + sizeof header - 1);
    withEmpty.resize(withEmpty.size() + 64 - (sizeof header - 1 - 4), 0);
    for (int32_t v : {0, 0, 1, 0, 0, 0, 0, 0, 0, 0})
        for (int i = 0; i < 4; ++i)
            withEmpty.push_back(uint8_t(uint32_t(v) >> (8 * i)));
    err = parseMd3(withEmpty, m);
    CHECK(!err);
    CHECK(m.surfaces.size() == 2 && m.surfaces[0].indices.size() == 3);
    CHECK(m.surfaces.size() == 2 && m.surfaces[1].vertexCount == 0 && m.surfaces[1].indices.empty() &&
          m.surfaces[1].shaders.size() == 1 && m.surfaces[1].shaders[0] == "IDP3line01");
}

static void testSkinAndPlacement() {
    const Skin skin =
        parseSkin("h_Head,models/players2/munro/head.tga\r\ntag_head,\n\nbody , models/x/body \nempty,\n");
    CHECK(skin.size() == 2 && skin.at("h_head") == "models/players2/munro/head.tga" &&
          skin.at("body") == "models/x/body");

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

static void testRoff() {
    using raven::RoffFile;
    using raven::RoffSystem;
    // A version 2 ROFF: two frames of 50 ms, the second turning the long
    // way round and with a note of two pieces.
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
    for (char c : std::string("ROFF"))
        d.push_back(uint8_t(c));
    i32(2);
    i32(2);  // frames
    i32(50); // ms each
    i32(1);  // notes
    for (float f : {10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})
        f32(f);
    i32(-1);
    i32(0);
    for (float f : {0.0f, 5.0f, 0.0f, 0.0f, 270.0f, 0.0f})
        f32(f);
    i32(0);
    i32(1);
    for (char c : std::string("loop\nsound x"))
        d.push_back(uint8_t(c));
    d.push_back(0);
    RoffFile roff;
    CHECK(raven::parseRoff(d, roff) && roff.frames.size() == 2 && roff.frameMs == 50);
    CHECK(roff.frames[1].turn[1] == -90 && roff.frames[1].notes.size() == 1 && roff.frames[0].notes.empty());
    std::vector<uint8_t> bad = d;
    bad[4] = 3; // version 3
    CHECK(!raven::parseRoff(bad, roff));
    CHECK(raven::parseRoff(d, roff));

    // Played: each frame due sets linear trajectories moving its offsets
    // in a frame time; at the end, still; a translated move turns with the
    // entity.
    SearchSet set;
    auto zip = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_roff.pk3";
    test::writeFile(path, test::buildZip({{"scripts/test.rof", d, {}}}));
    CHECK(!zip->open(path));
    set.add(std::move(zip), 0);
    RoffSystem roffs(set);
    const int32_t id = roffs.cache("scripts/test.rof", false);
    CHECK(id == 1 && roffs.cache("scripts/test.rof", true) == 1 && roffs.cache("scripts/none.rof", false) == 0);
    struct Set {
        int32_t type;
        int32_t time;
        RoffSystem::Vec3 originDelta;
        RoffSystem::Vec3 anglesDelta;
    };
    std::vector<Set> sets;
    std::vector<std::string> notes;
    bool finished = false;
    RoffSystem::Side side;
    side.current = [](int32_t, RoffSystem::Vec3 &o, RoffSystem::Vec3 &a) {
        o = {1, 2, 3};
        a = {0, 0, 0};
        return true;
    };
    side.setTrajectories = [&](int32_t, int32_t type, int32_t time, const RoffSystem::Vec3 &,
                               const RoffSystem::Vec3 &od, const RoffSystem::Vec3 &,
                               const RoffSystem::Vec3 &ad) { sets.push_back({type, time, od, ad}); };
    side.finished = [&](int32_t) { finished = true; };
    side.note = [&](int32_t, const std::string &n) { notes.push_back(n); };
    CHECK(roffs.play(7, id, false, false, 1000, {0, 0, 0}));
    roffs.update(false, 1000, side); // frame 0
    roffs.update(false, 1020, side); // not due
    roffs.update(true, 1050, side);  // the other side's: nothing
    roffs.update(false, 1050, side); // frame 1
    roffs.update(false, 1100, side); // the end
    CHECK(sets.size() == 3 && sets[0].type == 2 && sets[0].time == 1000 && sets[0].originDelta[0] == 200);
    CHECK(sets[1].time == 1050 && sets[1].originDelta[1] == 100 && sets[1].anglesDelta[1] == -1800);
    CHECK(sets[2].type == 0 && finished && roffs.playing() == 0);
    CHECK(notes == std::vector<std::string>({"loop", "sound x"}));
    // Facing along y, a move along x goes along y.
    sets.clear();
    CHECK(roffs.play(8, id, true, false, 2000, {0, 90, 0}));
    roffs.update(false, 2000, side);
    CHECK(sets.size() == 1 && std::fabs(sets[0].originDelta[0]) < 1e-3f &&
          std::fabs(sets[0].originDelta[1] - 200) < 1e-3f);
    // Purged: still where it is; cleaned on both sides: the file forgotten.
    CHECK(roffs.purge(8, false, 2010, side) && sets.back().type == 0 && roffs.playing() == 0);
    roffs.clean(false);
    CHECK(roffs.cached() == 1);
    roffs.clean(true);
    CHECK(roffs.cached() == 0);
    std::filesystem::remove(path);
}

// What a world renderer gives the graphics device: each texture's pixels
// by its number, and the mesh's vertices, the last given.
class StyleRenderer : public gfx::Renderer3D {
public:
    std::map<gfx::TextureId, std::vector<uint8_t>> textures;
    std::vector<gfx::Vertex> vertices;
    int verticesSet = 0;
    std::vector<gfx::DrawCall> drawn;
    Error createTexture(int w, int h, const uint8_t *rgba, const gfx::TextureParams &, gfx::TextureId &out) override {
        out = gfx::TextureId(++_next);
        textures[out].assign(rgba, rgba + size_t(w) * size_t(h) * 4);
        return Error::success();
    }
    void destroyTexture(gfx::TextureId id) override { textures.erase(id); }
    Error createMesh(const std::vector<gfx::Vertex> &v, const std::vector<uint32_t> &, gfx::MeshId &out) override {
        vertices = v;
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t> &, gfx::MeshId &out) override {
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t> &) override { return Error::success(); }
    Error setMeshVertices(gfx::MeshId, const std::vector<gfx::Vertex> &v) override {
        vertices = v;
        ++verticesSet;
        return Error::success();
    }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return 64; }
    int height() const override { return 64; }
    std::string deviceName() const override { return "styles"; }
    void beginFrame(const gfx::Mat4 &, const gfx::Vec3 &, Color) override {}
    void draw(const gfx::DrawCall &call) override { drawn.push_back(call); }
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

private:
    uint32_t _next = 0;
};

// R_RemapShader: what's drawn with one shader drawn with another, behind
// in time by its offset, from then on, for materials made before and
// after; remapping to itself undoes it.
static void testShaderRemap() {
    SearchSet files;
    ShaderLibrary library;
    auto def = [](const std::string &name, int stages) {
        ShaderDef d;
        d.name = name;
        for (int i = 0; i < stages; ++i) {
            ShaderStage st;
            st.maps = {"textures/x"};
            d.stages.push_back(st);
        }
        return d;
    };
    library.add(def("a", 1));
    library.add(def("b", 2));
    library.add(def("c", 1));
    StyleRenderer r;
    ShaderCache shaders(r, files, library);
    auto drawn = [&](int material, float time) {
        r.drawn.clear();
        shaders.draw(material, {}, 0, time);
        return r.drawn.size() == 0 ? 0.0f : float(r.drawn.size()) * 100 + r.drawn[0].time;
    };
    const int a = shaders.material("a", Lighting::kVertex);
    CHECK(drawn(a, 10) == 110);
    shaders.remap("A", "b", 2.5f);
    CHECK(drawn(a, 10) == 207.5f);
    shaders.remap("c", "b", 0);
    CHECK(drawn(shaders.material("c", Lighting::kVertex), 10) == 210);
    shaders.remap("a", "a", 0);
    CHECK(drawn(a, 10) == 110);
    shaders.clear();
    CHECK(drawn(shaders.material("c", Lighting::kVertex), 10) == 110);
}

// A videoMap whose video isn't there: it and every stage after it left
// out, as the original's shader keeps only the stages before the first it
// turned off; nothing left, the shader isn't drawn.
static void testMissingVideo() {
    SearchSet files;
    ShaderLibrary library;
    auto stage = [](const std::string &map, const std::string &video) {
        ShaderStage st;
        if (!map.empty())
            st.maps = {map};
        st.videoMap = video;
        return st;
    };
    ShaderDef first;
    first.name = "first";
    first.stages = {stage("", "video/gone"), stage("textures/x", "")};
    ShaderDef second;
    second.name = "second";
    second.stages = {stage("textures/x", ""), stage("", "video/gone"), stage("textures/y", "")};
    library.add(first);
    library.add(second);
    StyleRenderer r;
    ShaderCache shaders(r, files, library);
    const int a = shaders.material("first", Lighting::kVertex);
    CHECK(shaders.get(a).skip && shaders.get(a).stages.empty());
    const int b = shaders.material("second", Lighting::kVertex);
    CHECK(!shaders.get(b).skip && shaders.get(b).stages.size() == 1);
}

static void testLightStyles() {
    // One quad lit by style 0 and style 6: its first lightmap at (0, 0) of
    // lightmap 0, style 6's at (4, 4) of lightmap 1; its vertices likewise.
    Bsp bsp;
    BspModel model;
    model.mins = {0, 0, 0};
    model.maxs = {64, 64, 64};
    model.numSurfaces = 1;
    bsp.models.push_back(model);
    bsp.shaders.push_back({"textures/test/wall", 0, 0});
    bsp.lightmaps.assign(2 * size_t(kLightmapSize) * kLightmapSize * 3, 0);
    auto texel = [&](int map, int x, int y) {
        return &bsp.lightmaps[((size_t(map) * kLightmapSize + size_t(y)) * kLightmapSize + size_t(x)) * 3];
    };
    texel(0, 0, 0)[0] = 40; // doubled on loading: 80
    texel(1, 4, 4)[0] = 30; // 60
    BspSurface s;
    s.type = SurfaceType::kPlanar;
    s.numVertices = 4;
    s.numIndices = 6;
    s.lightmapStyles = {0, 6, kNoStyle, kNoStyle};
    s.vertexStyles = {0, 6, kNoStyle, kNoStyle};
    s.lightmap = {0, 1, -1, -1};
    s.lightmapPos[0] = {0, 0};
    s.lightmapPos[1] = {4, 4};
    s.lightmapSize = {2, 2};
    s.fog = -1;
    bsp.surfaces.push_back(s);
    for (int i = 0; i < 4; ++i) {
        BspVertex v;
        v.pos = {float(i % 2) * 64, float(i / 2) * 64, 0};
        v.color[0] = {50, 50, 50, 255}; // doubled: 100
        v.color[1] = {20, 20, 20, 255}; // 40
        bsp.vertices.push_back(v);
    }
    bsp.indices = {0, 1, 2, 2, 1, 3};
    bsp.raven = true;
    SearchSet files;
    ShaderLibrary library;
    StyleRenderer r;
    ShaderCache shaders(r, files, library);
    WorldRenderer world(r, shaders);
    CHECK(!world.load(bsp, library));
    // Lightmap 0 holds style 0's light alone (80); style 6's (60) has a
    // lightmap of its own, at style 0's place.
    gfx::TextureId lightmap0 = 0;
    gfx::TextureId style6 = 0;
    for (const auto &[id, px] : r.textures)
        if (px.size() == size_t(kLightmapSize) * kLightmapSize * 4 && px[0] == 80)
            lightmap0 = id;
        else if (px.size() == size_t(kLightmapSize) * kLightmapSize * 4 && px[0] == 60)
            style6 = id;
    CHECK(lightmap0 && style6);
    // Drawn as the original's shader: the lightmap stage, then style 6's
    // copy of it, added, coloured by the style, then the texture over both.
    auto stylePass = [&]() -> const gfx::DrawCall * {
        r.drawn.clear();
        world.drawSurfaces(0);
        if (r.drawn.size() != 3 || r.drawn[0].texture != lightmap0 || r.drawn[1].texture != style6)
            return nullptr;
        return &r.drawn[1];
    };
    const gfx::DrawCall *pass = stylePass();
    CHECK(pass && pass->state.src == gfx::Blend::kOne && pass->state.dst == gfx::Blend::kOne &&
          pass->rgbSource == gfx::ColorSource::kConstant && pass->color[0] == 1);
    // The vertices: each style's light times its colour, over 256:
    // (100 + 40) x 255 / 256.
    CHECK(r.vertices.size() == 4 && r.vertices[0].color[0] == 140 * 255 / 256);
    // Style 6 dark: its pass adds nothing; the vertices keep style 0's.
    LightStyleColors styles = fullLightStyles();
    styles[6] = {0, 0, 0, 255};
    world.setLightStyles(styles);
    pass = stylePass();
    CHECK(pass && pass->color[0] == 0);
    CHECK(r.verticesSet == 1 && r.vertices[0].color[0] == 100 * 255 / 256);
    // A style no surface uses changes nothing.
    styles[9] = {1, 2, 3, 255};
    world.setLightStyles(styles);
    CHECK(r.verticesSet == 1);
    // Half: the pass at 128 / 255; the vertices (100 x 255 + 40 x 128) / 256.
    styles[6] = {128, 128, 128, 255};
    world.setLightStyles(styles);
    pass = stylePass();
    CHECK(pass && std::fabs(pass->color[0] - 128.0f / 255) < 1e-6f);
    CHECK(r.vertices[0].color[0] == (100 * 255 + 40 * 128) / 256);
}

static void testLightGrid() {
    // A world 0..128 x 0..64 x 0..128 holds 3 x 2 x 2 grid points.
    Bsp bsp;
    BspModel world;
    world.mins = {0, 0, 0};
    world.maxs = {128, 64, 128};
    bsp.models.push_back(world);
    bsp.gridPoints.resize(3 * 2 * 2);
    for (size_t i = 0; i < bsp.gridPoints.size(); ++i)
        bsp.gridIndex.push_back(uint32_t(i));
    auto point = [&](int x, int y, int z) -> Bsp::GridPoint & { return bsp.gridPoints[size_t((z * 2 + y) * 3 + x)]; };
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) {
                Bsp::GridPoint &p = point(x, y, z);
                p.ambient[0].fill(uint8_t(x == 0 ? 40 : 80)); // doubled on loading
                p.directed[0].fill(50);
                p.latLong = {0, 0}; // from straight above
            }
    point(2, 0, 0).styles[0] = kNoStyle; // inside a wall
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
    // Raven's styles add up, each at full until the game sets them: 40 and
    // 20, doubled, + 32.
    for (Bsp::GridPoint &p : bsp.gridPoints) {
        p.styles[1] = 5;
        p.ambient[1].fill(20);
    }
    grid.build(bsp);
    CHECK(approx(grid.sample({0, 0, 0}).ambient[0], 80 + 40 + 32));
    // Each style scaled by the colour the game gives it: style 5 at half.
    LightStyleColors styles = fullLightStyles();
    styles[5] = {102, 102, 102, 255};
    grid.setStyles(styles);
    CHECK(approx(grid.sample({0, 0, 0}).ambient[0], 80 + 40 * 102.0f / 255 + 32));
    grid.setStyles(fullLightStyles());
    // A grid of the wrong size is ignored, and sampling still gives light.
    bsp.gridIndex.resize(1);
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
    auto text = [&](const char *t, size_t size) {
        for (size_t k = 0; k < size; ++k)
            d.push_back(k < std::strlen(t) ? uint8_t(t[k]) : 0);
    };
    using Bone = std::array<float, 12>; // rows of 3 x 4
    const Bone still{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone moved{1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone turned{0, -1, 0, 10, 1, 0, 0, 0, 0, 0, 1, 0};
    const std::array<std::array<Bone, 2>, 2> frames{{{still, moved}, {still, turned}}};
    const size_t frameSize = packed ? 40 + 2 * 24 : 56 + 2 * 48;
    const int32_t framesAt = 104;
    const int32_t lodAt = framesAt + 2 * int32_t(frameSize);
    // One vertex: normal, st, one or two weights.
    const size_t surfaceSize = 168 + (24 + 2 * 20) + 2 * (24 + 20) + 12;
    const int32_t tagsAt = lodAt + 12 + int32_t(surfaceSize);
    text("RDM5", 4);
    i32(2);
    text("test", 64);
    i32(2);                             // frames
    i32(2);                             // bones
    i32(packed ? -framesAt : framesAt); // negative: packed
    i32(1);                             // levels of detail
    i32(lodAt);
    i32(1); // tags
    i32(tagsAt);
    i32(tagsAt + 36);
    for (const auto &frame : frames) {
        for (int k = 0; k < 10; ++k)
            f32(0); // bounds, origin, radius
        if (!packed)
            text("frame", 16);
        for (const Bone &b : frame) {
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
    const BspVertex &mid = bulge.points[2 * 9 + 4];
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
    BspVertex left[9];
    BspVertex right[9];
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
        const auto &a = grids[0].points[size_t(y * grids[0].width + grids[0].width - 1)].pos;
        const auto &b = grids[1].points[size_t(y * grids[1].width)].pos;
        matched =
            matched && std::fabs(a[0] - b[0]) < 0.1f && std::fabs(a[1] - b[1]) < 0.1f && std::fabs(a[2] - b[2]) < 0.1f;
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
    CHECK(anims[0].name == "BOTH_DEATH1" && anims[0].part == PlayerAnimation::Part::kBoth && anims[0].first == 0);
    CHECK(anims[2].part == PlayerAnimation::Part::kTorso && anims[2].first == 134);
    // Legs moved back by 138 - 95 into the legs model.
    CHECK(anims[3].part == PlayerAnimation::Part::kLegs && anims[3].first == 95);
    CHECK(anims[4].name == "LEGS_IDLE" && anims[4].first == 165 && anims[4].loop == 10 && anims[4].fps == 15);
    int frame = 0;
    int next = 0;
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
        const Md3Surface &s = m.surfaces[0];
        CHECK(s.name == "body" && s.shaders[0] == "models/test/body" && s.vertexCount == 3);
        CHECK(s.indices == std::vector<uint32_t>({0, 1, 2}) && s.st[0][0] == 0.25f && s.st[0][1] == 0.5f);
        auto near = [](const Vec3 &a, Vec3 b) {
            return std::fabs(a[0] - b[0]) < 0.02f && std::fabs(a[1] - b[1]) < 0.02f && std::fabs(a[2] - b[2]) < 0.02f;
        };
        // Frame 0: vertex 0 between (0,1,0) and (10,1,0); vertex 1 at 10 + 1.
        CHECK(near(s.vertices[0].pos, {5, 1, 0}) && near(s.vertices[1].pos, {11, 0, 0}));
        CHECK(near(s.vertices[0].normal, {0, 0, 1}) && near(s.vertices[1].normal, {1, 0, 0}));
        // Frame 1: bone 1 turned, so its offsets are too: (1,0,0) -> (0,1,0).
        CHECK(near(s.vertices[3 + 1].pos, {10, 1, 0}) && near(s.vertices[3 + 2].pos, {10, 2, 0}));
        CHECK(near(s.vertices[3 + 0].pos, {4.5f, 0.5f, 0}) && near(s.vertices[3 + 1].normal, {0, 1, 0}));
        // The tag follows bone 1: its x axis is the matrix's first column.
        const Md3Tag *tag = m.tag(1, "tag_weapon");
        CHECK(tag && near(tag->origin, {10, 0, 0}) && near(tag->axis[0], {0, 1, 0}) && near(tag->axis[1], {-1, 0, 0}));
        // A level of detail past its only one: that one.
        Md3Model low;
        CHECK(!parseModel(buildMdr(packed), low, 2) && low.surfaces.size() == 1 &&
              low.surfaces[0].vertices.size() == s.vertices.size());
    }
    // A broken one: the triangles past the end.
    std::vector<uint8_t> broken = buildMdr(false);
    broken.erase(broken.end() - 60, broken.end());
    Md3Model m;
    CHECK(parseModel(broken, m));
}

static const reluva::test::Suite
    kSuite("idtech3.unit.formats",
           {
               {"lexer", [](const std::filesystem::path &) { testLexer(); }},
               {"entities", [](const std::filesystem::path &) { testEntities(); }},
               {"shaders", [](const std::filesystem::path &) { testShaders(); }},
               {"shader stages", [](const std::filesystem::path &) { testShaderStages(); }},
               {"waves", [](const std::filesystem::path &) { testWaves(); }},
               {"bsp", [](const std::filesystem::path &) { testBsp(); }},
               {"surface sprites", [](const std::filesystem::path &) { testSurfaceSprites(); }},
               {"ghoul2", [](const std::filesystem::path &) { testGhoul2(); }},
               {"ghoul2 instances", [](const std::filesystem::path &) { testGhoul2Instances(); }},
               {"effects", [](const std::filesystem::path &) { testEffects(); }},
               {"electricity", [](const std::filesystem::path &) { testElectricity(); }},
               {"raven text", [](const std::filesystem::path &) { testRavenText(); }},
               {"patches", [](const std::filesystem::path &) { testPatch(); }},
               {"patch grids", [](const std::filesystem::path &) { testPatchGrids(); }},
               {"md3", [](const std::filesystem::path &) { testMd3(); }},
               {"skins and placement", [](const std::filesystem::path &) { testSkinAndPlacement(); }},
               {"light grid", [](const std::filesystem::path &) { testLightGrid(); }},
               {"light styles", [](const std::filesystem::path &) { testLightStyles(); }},
               {"shader remap", [](const std::filesystem::path &) { testShaderRemap(); }},
               {"missing video", [](const std::filesystem::path &) { testMissingVideo(); }},
               {"roff", [](const std::filesystem::path &) { testRoff(); }},
               {"mdr", [](const std::filesystem::path &) { testMdr(); }},
               {"player animation", [](const std::filesystem::path &) { testPlayerAnimation(); }},
           });
