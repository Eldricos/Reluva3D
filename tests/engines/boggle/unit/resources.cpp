// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "engines/boggle/classic/cube_geometry.h"
#include <limits>
#include "tests/engines/boggle/fixtures.h"
#include "tests/support/test.h"

using namespace reluva;
using namespace reluva::boggle;
namespace {
void paletteTest() {
    Palette p;
    auto text = fixtures::palette();
    CHECK(!readPalette(text, p));
    CHECK(p[7].r == 7 && p[7].g == 248 && p[7].b == 21);
    CHECK(p[255].r == 255 && p[255].g == 0 && p[255].b == 253);
    for (auto bad : {std::string{}, text.substr(0, text.size() - 10), text + "1 2 3\n",
                     std::string("JASC-PAL\n0100\n999999999999\n")}) {
        CHECK(bool(readPalette(bad, p)));
        CHECK(p[7].r == 0);
    }
    for (const std::string value : {"256", "-1", "+1", "1x"}) {
        auto bad = text; bad.replace(bad.find("0 255 0"), 1, value);
        CHECK(bool(readPalette(bad, p)));
    }
}
void diceTest() {
    DiceSet out;
    for (size_t n : {size_t(16), size_t(25)}) {
        CHECK(!readDiceSet(fixtures::dice(n), out));
        CHECK(out.dice.size() == n && out.side == (n == 16 ? 4u : 5u));
        CHECK(out.dice[0][0] == "Ab");
    }
    auto highByte = fixtures::dice(); highByte.replace(0, 2, std::string(1, char(0xd1)));
    CHECK(!readDiceSet(highByte, out));
    CHECK(out.dice[0][0] == std::string(1, char(0xd1)));
    for (auto bad : {fixtures::dice(15), fixtures::dice(26), fixtures::dice() + "\n",
                     std::string("A,B,C,D,E\n"), std::string(70000, 'A')}) {
        CHECK(bool(readDiceSet(bad, out))); CHECK(out.dice.empty() && out.side == 0);
    }
    for (const std::string value : {"", "1", "A B", "ABCDEFGHI", "A,B"}) {
        auto bad = fixtures::dice(); bad.replace(0, 2, value);
        CHECK(bool(readDiceSet(bad, out)));
    }
}
void meshTest() {
    Mesh mesh;
    auto text = fixtures::mesh();
    CHECK(!readMesh(text, mesh));
    CHECK(mesh.positions.size() == 3 && mesh.faces.size() == 1 && mesh.faces[0][2] == 2);
    CHECK(mesh.positions[1][0] == 2 && mesh.texcoords[2][1] == 1);
    CHECK(mesh.materials.size() == 1 && mesh.materials[0].diffuse[1] == 0.4 && mesh.materials[0].power == 8);
    CHECK(mesh.normals.size() == 3 && mesh.normalFaces[0].size() == 3);
    auto shadow = text; shadow.erase(shadow.find("0;0;0;;"), 7);
    CHECK(!readMesh(shadow, mesh));
    CHECK(mesh.materials[0].emissive[0] == 0);
    for (size_t n = 0; n < text.size(); ++n) {
        CHECK(bool(readMesh(text.substr(0, n), mesh)));
        CHECK(mesh.positions.empty());
    }
    for (auto [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"0,1,2", "0,1,3"}, {"2;0;0", "nan;0;0"}, {"2;0;0", "1e999;0;0"},
             {"3; 0;0;0", "4294967295; 0;0;0"}, {"1; 1; 0; Material", "1; 1; 1; Material"},
             {"txt", "bin"}, {"MeshTextureCoords", "Unknown"}, {"3;0,1,2", "2;0,1"}}) {
        auto bad = text; const auto at = bad.find(from); CHECK(at != bad.npos);
        if (at != bad.npos) bad.replace(at, from.size(), to);
        CHECK(bool(readMesh(bad, mesh))); CHECK(mesh.positions.empty());
    }
    CHECK(bool(readMesh(text + "Mesh {}", mesh)));
}
void cubeGeometryTest() {
    Mesh source;
    for(unsigned i=0;i<8;++i)source.positions.push_back({(i&1)?.5:-.5,(i&2)?.5:-.5,(i&4)?.5:-.5});
    source.faces={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1},{0,3,4}};
    source.normals={{{1,0,0}},{{-1,0,0}},{{0,1,0}},{{0,-1,0}},{{0,0,1}},{{0,0,-1}},{{.707106781,-.707106781,0}}};
    source.texcoords.assign(8,{.25,.75});source.materials.resize(7);
    for(auto& material:source.materials)material.diffuse={.8,.7,.6,1};
    for(unsigned i=0;i<7;++i){source.normalFaces.push_back(std::vector<uint32_t>(source.faces[i].size(),i));source.faceMaterials.push_back(i==6?6:(i+3)%6);}
    CubeGeometry geometry;CHECK(!geometry.prepare(source,.25));
    CHECK(geometry.appliedScale==.25 && geometry.mesh.positions[7][0]==.125);
    CHECK(geometry.mesh.faces==source.faces && geometry.mesh.normals==source.normals);
    CHECK(geometry.mesh.texcoords==source.texcoords && geometry.mesh.normalFaces==source.normalFaces);
    CHECK(geometry.mesh.faceMaterials==source.faceMaterials && geometry.mesh.faces.size()==7); // includes the non-letter surface
    for(size_t d=0;d<6;++d)CHECK(geometry.letters[d]==(d+3)%6);
    auto subdivided=source;subdivided.positions.push_back({.5,0,-.5});subdivided.texcoords.push_back({.25,.75});
    subdivided.faces[0].insert(subdivided.faces[0].begin()+1,8);subdivided.normalFaces[0].insert(subdivided.normalFaces[0].begin()+1,0);
    CHECK(!geometry.prepare(subdivided,1)); // first three corners are collinear
    CHECK(geometry.mesh.faces[0].size()==5 && geometry.letters[0]==3);
    CHECK(source.positions[7][0]==.5); // source data stays unchanged
    for(double bad:{0.,-1.,2.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(bool(geometry.prepare(source,bad)) && geometry.mesh.positions.empty() && !geometry.appliedScale);
    }
    auto rejects=[&](const Mesh& bad){CHECK(!geometry.prepare(source,1));CHECK(bool(geometry.prepare(bad,1)));CHECK(geometry.mesh.faces.empty() && !geometry.appliedScale);};
    auto bad=source;bad.positions[0][0]=17;rejects(bad);
    bad=source;bad.positions[0][0]=1.01;rejects(bad);
    bad=source;bad.normals[0]={0,0,0};rejects(bad);
    bad=source;bad.texcoords[0][0]=1.01;rejects(bad);
    bad=source;bad.texcoords.pop_back();rejects(bad);
    bad=source;bad.faces[0][0]=99;rejects(bad);
    bad=source;bad.faces[0]={1,1,1};bad.normalFaces[0]={0,0,0};rejects(bad);
    bad=source;bad.normalFaces[0][0]=99;rejects(bad);
    bad=source;bad.faceMaterials[0]=99;rejects(bad);
    bad=source;bad.faceMaterials[0]=bad.faceMaterials[1];rejects(bad);
    bad=source;bad.materials[0].diffuse[3]=0;rejects(bad);
    bad=source;bad.materials[0].power=std::numeric_limits<double>::infinity();rejects(bad);
    CHECK(classicCubeMeshScale("Scripts/Die.x")==1);
    CHECK(classicCubeMeshScale("Scripts/Rounded.x")>.138 && classicCubeMeshScale("Scripts/Rounded.x")<.139);
    Resources missing;CHECK(bool(readClassicCubeGeometry(missing,"unrecognized.x",geometry)) && geometry.mesh.faces.empty());
    CHECK(bool(readClassicCubeGeometry(missing,"Scripts/Die.x",geometry)) && geometry.mesh.faces.empty());
}
void filesTest(const std::filesystem::path& tmp) {
    fixtures::install(tmp);
    Resources resources; CHECK(!resources.open(tmp));
    Palette colors; CHECK(!resources.palette("palettes/DEMO.pal", colors));
    CHECK(colors[7].r == 7);
    Language language; CHECK(!resources.language("LANGUAGE/DEMO.DLL", language));
    CHECK(language.strings.size() == 1 && language.strings[0].text == "Synthetic menu");
    image::Image picture; CHECK(!resources.image("bitmaps/demo.bmp", picture));
    CHECK(picture.w == 1 && picture.h == 1 && picture.rgba[0] == 7);
    audio::Sound wave; CHECK(!resources.sound("waves/demo.wav", wave));
    CHECK(wave.rate == 8000 && wave.samples.size() == 2 && wave.samples[1] == -1000);
    ResourceSummary summary; CHECK(!resources.check(summary));
    CHECK(summary.images == 1 && summary.sounds == 1 && summary.languages == 1 && summary.dice == 16);
    const auto original = summary.digest;
    auto changed = fixtures::palette(); changed.replace(changed.find("0 255 0"), 1, "1");
    fixtures::write(tmp / "PALETTES/demo.PAL", changed);
    CHECK(!resources.check(summary)); CHECK(summary.digest != original);
    fixtures::write(tmp / "PALETTES/demo.PAL", std::string("broken"));
    CHECK(bool(resources.check(summary))); CHECK(summary.images == 0 && summary.digest.empty());
    CHECK(bool(resources.palette("missing.pal", colors))); CHECK(colors[7].r == 0);
}
}
static const test::Suite suite("boggle.unit.resources", {
    {"palette colors, truncation and bounds", [](const auto&) { paletteTest(); }},
    {"dice counts and preserved faces", [](const auto&) { diceTest(); }},
    {"complete cube normalization, directional groups, body retention and malformed data", [](const auto&) { cubeGeometryTest(); }},
    {"mesh geometry and malformed input", [](const auto&) { meshTest(); }},
    {"case-insensitive resource set and decoded digest", filesTest},
});
