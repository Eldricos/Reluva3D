// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
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
    {"mesh geometry and malformed input", [](const auto&) { meshTest(); }},
    {"case-insensitive resource set and decoded digest", filesTest},
});
