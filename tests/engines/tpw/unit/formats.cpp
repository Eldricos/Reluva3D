// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's files, built here as SimCoaster's are laid
// out (docs/engines/tpw/games/simcoaster/formats/): archives and mounting
// them, settings files, string tables and their character set, fonts,
// sound banks.
//
//   reluva-tests tpw.unit.formats

#include "engines/tpw/formats/animation.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/hmp.h"
#include "engines/tpw/formats/lnd.h"
#include "engines/tpw/formats/md2.h"
#include "engines/tpw/formats/omp.h"
#include "engines/tpw/formats/plb.h"
#include "engines/tpw/formats/rse.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/saved_park.h"
#include "engines/tpw/formats/sound_map.h"
#include "engines/tpw/formats/sprites.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/formats/wad.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/sound_effects.h"
#include "engines/tpw/ui/sprites.h"
#include "engines/tpw/textures.h"

#include "tests/engines/tpw/fixtures.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::tpw;
using namespace reluva::tpw::testing;

namespace {

const std::vector<WadFile> kRide = {
    {"ride.sam", toBytes("Ride.Speed 3\r\n"), true},           {"qickload.txt", toBytes("ride.md2\r\n")},
    {"stexture\\wood.tga", toBytes("small wood"), true},       {"paint.tga", toBytes("small paint")},
    {"textures\\wood.fsh", toBytes("big wood, packed"), true},
};

void testWad(const std::filesystem::path &tmp) {
    test::writeFile(tmp / "ride.wad", makeWad(kRide));
    WadArchive wad;
    CHECK(!wad.open(tmp / "ride.wad"));
    CHECK(wad.entries().size() == 5);
    // A name with a folder sets it for those after.
    const std::vector<std::string> names = wad.list();
    CHECK((names == std::vector<std::string>{"ride.sam", "qickload.txt", "stexture/wood.tga", "stexture/paint.tga",
                                             "textures/wood.fsh"}));
    Bytes out;
    CHECK(!wad.read("STEXTURE\\Paint.TGA", out) && out == toBytes("small paint"));
    CHECK(!wad.read("textures/wood.fsh", out) && out == toBytes("big wood, packed"));
    CHECK(!wad.read("ride.sam", out) && out == toBytes("Ride.Speed 3\r\n"));
    CHECK(wad.entries()[0].refPack && !wad.entries()[1].refPack && wad.entries()[4].hash == 0x104);
    CHECK(!wad.has("paint.tga") && wad.read("paint.tga", out));

    // With a prefix, the folder the archive stands for.
    WadArchive prefixed;
    CHECK(!prefixed.open(tmp / "ride.wad", "levels/water/rides/ride"));
    CHECK(prefixed.has("levels/water/rides/ride/stexture/wood.tga") && !prefixed.has("stexture/wood.tga"));
    CHECK(prefixed.list()[0] == "levels/water/rides/ride/ride.sam");

    // Damage: a packed file not the size its record gives, a record past
    // the end, not an archive at all.
    std::vector<WadFile> wrong = kRide;
    wrong[0].packedSizeField = 3;
    test::writeFile(tmp / "wrong.wad", makeWad(wrong));
    WadArchive bad;
    CHECK(!bad.open(tmp / "wrong.wad") && bad.read("ride.sam", out));
    Bytes cut = makeWad(kRide);
    put32(cut, 0x58 + 12, uint32_t(cut.size()));
    test::writeFile(tmp / "cut.wad", cut);
    CHECK(bad.open(tmp / "cut.wad"));
    test::writeFile(tmp / "not.wad", toBytes("PK\3\4 not one"));
    CHECK(bad.open(tmp / "not.wad"));
}

// The Data folder: each archive as the folder of its name, the loose files
// over them.
void testMount(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    test::writeFile(data / "levels" / "water" / "rides" / "ride.wad", makeWad(kRide));
    test::writeFile(data / "ui.WAD", makeWad({{"button.md2", toBytes("archived button")}}));
    test::writeFile(data / "ui" / "button.md2", toBytes("loose button"));
    test::writeFile(data / "Config.sam", toBytes("Defaults.DefaultConfig 1\r\n"));
    test::writeFile(data / "broken.wad", toBytes("DWFB"));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    CHECK(warnings.size() == 1);
    Bytes out;
    CHECK(!files.read("levels/water/rides/ride/stexture/paint.tga", out) && out == toBytes("small paint"));
    CHECK(!files.read("ui/button.md2", out) && out == toBytes("loose button"));
    CHECK(!files.read("config.sam", out));
    CHECK(mountGame(files, tmp / "nothing", warnings));
}

// A model's textures: its own folder's first, then its world's shared
// ones, then the smaller copies; a bank of the name before the .tga it names.
void testFindTexture(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    const std::filesystem::path ride = data / "levels" / "water" / "rides" / "ride";
    test::writeFile(ride / "ride.MD2", toBytes("model"));
    test::writeFile(ride / "textures" / "own.fsh", toBytes("bank"));
    test::writeFile(ride / "textures" / "own.tga", toBytes("picture"));
    test::writeFile(ride / "textures" / "plain.tga", toBytes("picture"));
    test::writeFile(ride / "stexture" / "small.fsh", toBytes("bank"));
    test::writeFile(ride / "stexture" / "shared.fsh", toBytes("bank"));
    test::writeFile(data / "levels" / "water" / "sharetex" / "shared.fsh", toBytes("bank"));
    test::writeFile(data / "levels" / "water" / "ssharete" / "tiny.tga", toBytes("picture"));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    const std::string model = "levels/water/rides/ride/ride.MD2";
    CHECK(findTexture(files, model, "own.tga") == "levels/water/rides/ride/textures/own.fsh");
    CHECK(findTexture(files, model, "plain.tga") == "levels/water/rides/ride/textures/plain.tga");
    CHECK(findTexture(files, model, "shared.tga") == "levels/water/sharetex/shared.fsh");
    CHECK(findTexture(files, model, "small.tga") == "levels/water/rides/ride/stexture/small.fsh");
    CHECK(findTexture(files, model, "tiny.tga") == "levels/water/ssharete/tiny.tga");
    CHECK(findTexture(files, model, "missing.tga").empty());
    image::Image picture;
    CHECK(loadPicture(files, "levels/water/rides/ride/textures/own.fsh", picture));
}

// Settings: names with their values (as many as fields after the group),
// comments, grids.
void testSam() {
    const std::string text = "#\tTheme Park 2 Ride Description File\r\n"
                             "Info.Id\t\t\t1304\r\n"
                             "Info.Name\t\t\"Jacob's Drop\" \r\n"
                             "Info.Shape\r\n"
                             "---\r\n"
                             "***\r\n"
                             "*2*\r\n"
                             "---\r\n"
                             "\r\n"
                             "Research.Group\t\t0\t\t0 = Available initially\r\n"
                             "Upgrades[1].CostOfResearch\t-0.5 # 100\r\n"
                             "PeepTypes[0].Cash.Boredom.Generation\t300\t40\t0\r\n"
                             "Info.WhichUIType 4 Dont display in UI\r\n"
                             "info.whichuitype 5\n";
    SamFile sam;
    CHECK(!sam.parse(toBytes(text)));
    CHECK(sam.entries().size() == 8);
    CHECK(sam.number("Info.Id") == 1304 && sam.text("info.name") == "Jacob's Drop");
    const SamFile::Entry *shape = sam.find("Info.Shape");
    CHECK(shape && shape->values.empty() && (shape->grid == std::vector<std::string>{"***", "*2*"}));
    const SamFile::Entry *group = sam.find("Research.Group");
    CHECK(group && group->values.size() == 1 && group->line == 10);
    CHECK(sam.number("Upgrades[1].CostOfResearch") == -0.5);
    const SamFile::Entry *peep = sam.find("PeepTypes[0].Cash.Boredom.Generation");
    CHECK(peep && peep->values.size() == 3 && peep->values[1].number == 40);
    CHECK(sam.number("Info.WhichUIType") == 5); // the last of one name
    CHECK(sam.number("Info.Name", 7) == 7 && sam.text("Info.Id", "none") == "none" &&
          sam.number("Info.Nothing", 2) == 2);

    // A name without its value, or without all its values; a grid without
    // its end; a line that isn't a setting.
    CHECK(sam.parse(toBytes("Info.Id\r\nInfo.Name \"x\"\r\n")));
    CHECK(sam.parse(toBytes("A[0].B.C 1 comment\r\n")));
    CHECK(sam.parse(toBytes("Info.Shape\r\n---\r\n***\r\n")));
    CHECK(sam.parse(toBytes("just words\r\n")));
    CHECK(sam.parse(toBytes("NoDot 1\r\n")));
}

// A character set of five codes: 1 'H', 2 'i', 3 ' ', 4 '£', 5 '™'.
Bytes makeCharacterSet() {
    Bytes b = {'B', 'F', 'M', 'U', 0, 0, 5, 0};
    for (uint16_t c : {u'H', u'i', u' ', u'\u00a3', u'\u2122'}) {
        b.push_back(uint8_t(c));
        b.push_back(uint8_t(c >> 8));
    }
    return b;
}

// A table numbered 1042: "Hi", then "£{1} Hi™" (a parameter between two
// texts), then an empty string.
Bytes makeStringTable() {
    Bytes b = {'B', 'F', 'S', 'T'};
    put32(b, 4, 1042);
    put32(b, 8, 3);
    b.resize(24);
    const std::vector<Bytes> strings = {
        {1, 2, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0},
        {1, 1, 0, 0, 4, 0, 0, 0, 2, 1, 0, 0, 1, 4, 0, 0, 3, 1, 2, 5, 0, 0, 0, 0},
        {0, 0, 0, 0},
    };
    for (size_t i = 0; i < strings.size(); ++i) {
        put32(b, 12 + 4 * i, uint32_t(b.size() - 12));
        b.insert(b.end(), strings[i].begin(), strings[i].end());
    }
    return b;
}

void testStr() {
    CharacterSet characters;
    CHECK(!characters.parse(makeCharacterSet()));
    CHECK(characters.size() == 5 && characters.unicode(4) == U'\u00a3' && characters.unicode(0) == 0 &&
          characters.unicode(6) == 0);
    const uint8_t unknown[] = {1, 9};
    CHECK(characters.toUtf8(unknown, 2) == "H\xef\xbf\xbd");

    StringTable table;
    CHECK(!table.parse(makeStringTable(), characters));
    CHECK(table.id() == 1042 && table.size() == 3);
    CHECK(table.text(0) == "Hi" && table.text(1) == "\xc2\xa3{1} Hi\xe2\x84\xa2" && table.text(2).empty());
    CHECK(table.parts(1).size() == 3 && table.parts(1)[1].parameter && table.parts(1)[1].number == 1);
    CHECK(table.parts(7).empty() && table.text(7).empty());

    // Cut short, a part of a kind not known, not a table at all.
    Bytes cut = makeStringTable();
    cut.resize(cut.size() - 6);
    CHECK(table.parse(cut, characters));
    Bytes odd = makeStringTable();
    odd[24] = 7;
    CHECK(table.parse(odd, characters));
    CHECK(table.parse(toBytes("BFMU"), characters) && characters.parse(toBytes("BFST")));
}

// A glyph's record: its character, its pixels' stored size, their size at 4
// bits, the storage, then its box, then the pixels.
void putGlyph(Bytes &b, char32_t c, uint32_t storage, int w, int h, int left, int top, int advance, const Bytes &px) {
    const size_t at = b.size();
    put32(b, at, uint32_t(c));
    put32(b, at + 4, uint32_t(px.size()));
    put32(b, at + 8, uint32_t((w * h + 1) / 2));
    put32(b, at + 12, storage);
    b.insert(b.end(), {uint8_t(w), 0, uint8_t(h), 0, uint8_t(left), uint8_t(top), uint8_t(advance), 0});
    b.insert(b.end(), px.begin(), px.end());
}

// Three glyphs: an 'L' at 1 bit a pixel (3x3), a dot at 4 bits (2x2) a
// pixel left of the pen, and one of the storage not read.
Bytes makeFont() {
    Bytes b = {'F', '4', 'F', 'B', 1, 13, 3, 0};
    b.resize(8 + 12);
    put32(b, 8, uint32_t(b.size()));
    // 100 100 111, then padding: 1001 0011 1000 0000.
    putGlyph(b, U'L', 2, 3, 3, 1, 6, 5, {0x93, 0x80, 0, 0, 0, 0, 0, 0});
    put32(b, 12, uint32_t(b.size()));
    putGlyph(b, U'.', 0, 2, 2, -1, 7, 3, {0x0f, 0x84, 0, 0, 0, 0, 0, 0});
    put32(b, 16, uint32_t(b.size()));
    putGlyph(b, U'?', 1, 2, 2, 0, 0, 3, {0x05, 0x01, 0x63, 0x0d, 0, 0, 0, 0});
    return b;
}

void testBf4() {
    Font font;
    CHECK(!font.parse(makeFont()));
    CHECK(font.glyphs().size() == 3 && font.kind() == 1 && font.size() == 13);
    const Font::Glyph *l = font.glyph(1);
    CHECK(l && l->character == U'L' && l->width == 3 && l->height == 3 && l->left == 1 && l->top == 6 &&
          l->advance == 5);
    CHECK(l && (l->coverage == Bytes{15, 0, 0, 15, 0, 0, 15, 15, 15}));
    const Font::Glyph *dot = font.glyph(2);
    CHECK(dot && dot->readable && dot->left == -1 && (dot->coverage == Bytes{0, 15, 8, 4}));
    const Font::Glyph *odd = font.glyph(3);
    CHECK(odd && !odd->readable && odd->coverage.empty() && odd->width == 2);
    CHECK(!font.glyph(0) && !font.glyph(4));

    // Cut short; not a font.
    Bytes cut = makeFont();
    cut.resize(cut.size() - 40);
    CHECK(font.parse(cut));
    CHECK(font.parse(toBytes("BFST")));
}

// A bank of three: PCM mono, an MPEG sound's header (its data not real
// MPEG), and an entry pointing at no header.
void testSdt() {
    Bytes bank;
    put32(bank, 0, 3);
    bank.resize(16);
    put32(bank, 4, uint32_t(bank.size()));
    putSound(bank, "bkart_b1.wav", 0x02, 22050, {1, 0, 0xff, 0xff, 0x00, 0x80}, 0);
    put32(bank, 8, uint32_t(bank.size()));
    putSound(bank, "a_name_cut_at_16.mp2", 0x25, 44100, {0xff, 0xf5, 0, 0}, 4000);
    put32(bank, 12, uint32_t(bank.size()));
    bank.insert(bank.end(), {0xd2, 0xcb, 0xb0, 0xa2, 0, 0, 0, 0});
    SoundBank sdt;
    CHECK(!sdt.parse(bank));
    CHECK(sdt.entries().size() == 3);
    if (sdt.entries().size() != 3)
        return;
    const SoundBank::Entry &pcm = sdt.entries()[0];
    CHECK(pcm.readable && pcm.name == "bkart_b1.wav" && !pcm.mpeg && pcm.rate == 22050 && pcm.channels == 1);
    const SoundBank::Entry &mpeg = sdt.entries()[1];
    CHECK(mpeg.readable && mpeg.name == "a_name_cut_at_16" && mpeg.mpeg && mpeg.channels == 2 && mpeg.rate == 44100 &&
          mpeg.pcmBytes == 4000 && mpeg.size == 4);
    CHECK(!sdt.entries()[2].readable);
    audio::Sound sound;
    CHECK(!sdt.decode(0, sound) && sound.rate == 22050 && (sound.samples == std::vector<int16_t>{1, -1, -32768}));
    CHECK(sdt.decode(1, sound)); // not MPEG frames
    CHECK(sdt.decode(2, sound) && sdt.decode(3, sound));
    Bytes cut = bank;
    put32(cut, 0, 1000);
    CHECK(sdt.parse(cut));
}

// The model fixtures.h builds, then cut and broken.
void testMd2() {
    const Bytes data = makeModel();
    Model model;
    CHECK(!model.parse(data));
    CHECK(model.kind() == Model::Kind::kModel && model.name() == "test.MD2" && model.otherNodes() == 1);
    CHECK(model.textures().size() == 2 && model.textures()[1].name == "two.tga" && model.textures()[1].flags == 0x31);
    CHECK(model.meshes().size() == 1);
    if (model.meshes().size() != 1)
        return;
    const Model::Mesh &mesh = model.meshes()[0];
    CHECK(mesh.name == "box" && mesh.positions.size() == 5 && mesh.uv.size() == 4 && mesh.faces.size() == 2);
    CHECK((mesh.positions[2] == gfx::Vec3{4, 1, 1}) && (mesh.positions[4] == gfx::Vec3{2, 0, 0}));
    CHECK((gfx::transformPoint(mesh.transform, mesh.positions[2]) == gfx::Vec3{14, 6, 1}));
    CHECK(mesh.faces[1].normal == 5 && (mesh.faces[1].vertex == std::array<uint16_t, 3>{0, 2, 3}));
    CHECK(mesh.normals.size() == 6 && (mesh.normals[5] == gfx::Vec3{0, 1, 0}));
    CHECK(mesh.uv[1][0] == 1 && mesh.uv[1][1] == 0 && mesh.uv[2][0] == 0 && mesh.uv[2][1] == 1 && mesh.uv[3][0] == 1);
    CHECK(mesh.groups.size() == 2 && mesh.groups[0].texture == 1 && mesh.groups[0].faceCount == 1);
    CHECK(mesh.groups[1].texture == Model::Group::kNoTexture && mesh.groups[1].firstFace == 1 &&
          mesh.groups[1].flags == 1);

    // An animated texture: its frames' names, then the animation's, take
    // the places of the names after it.
    const Bytes animated = makeModel(true);
    CHECK(!model.parse(animated));
    CHECK(model.textures().size() == 3 && model.textures()[1].name == "fr01.tga" &&
          (model.textures()[1].frames == std::vector<std::string>{"fr01.tga", "fr02.tga"}) &&
          model.textures()[1].animation == "fr.tga" && model.textures()[2].name == "three.tga");
    CHECK(model.textures()[0].frames.empty() && model.meshes().size() == 1);
    // Names that don't add up: one each, too few for an animated texture.
    Bytes fewNames = animated;
    put32(fewNames, 0x54, uint32_t(0xb8 + 24 + 20 * 3));
    CHECK(model.parse(fewNames));

    // No table: the header alone, or an animation's.
    Bytes summary(data.begin(), data.begin() + 0xb8);
    put32(summary, 0x50, 0);
    CHECK(!model.parse(summary) && model.kind() == Model::Kind::kSummary && model.meshes().empty());
    put32(summary, 0x30, 0x20);
    CHECK(!model.parse(summary) && model.kind() == Model::Kind::kAnimation);

    Bytes old = data;
    put32(old, 4, 24);
    CHECK(model.parse(old));
    CHECK(model.parse(Bytes(data.begin(), data.begin() + 0x100)));
    // A face's vertex past the mesh's four.
    Bytes badFace = data;
    const size_t mesh0 = data.size() - 0xa0;
    put16(badFace, size_t(data[mesh0 + 0x50 + 0x20] | data[mesh0 + 0x50 + 0x21] << 8) + 2, 4);
    CHECK(model.parse(badFace));
    // The mesh made its own child.
    Bytes loop = data;
    put32(loop, mesh0 + 12, uint32_t(mesh0));
    CHECK(model.parse(loop));
}

// A height map two tiles across and one deep: its samples, then a height
// and a flag for each tile; then cut, and of another kind.
void testHmp() {
    Bytes b(0x30);
    put16(b, 0, 3);
    put16(b, 2, 0xab1e);
    put16(b, 4, 5);
    put16(b, 6, 100);
    put16(b, 8, 2);
    put16(b, 10, 1);
    put32(b, 12, 0x30);
    put32(b, 16, 0x30 + 50);
    put32(b, 20, 0x30 + 52);
    putFloat(b, 0x18 + 16, 20.5f);
    for (size_t i = 0; i < 50; ++i)
        b.push_back(uint8_t(i));
    b.insert(b.end(), {49, 60, 1, 0});
    HeightMap map;
    CHECK(!map.parse(b));
    CHECK(map.width() == 2 && map.depth() == 1);
    // Row by row across both tiles: the second tile's first sample is the row's sixth.
    CHECK(map.sample(5, 0) == 5 && map.sample(0, 1) == 10 && map.sample(9, 4) == 49);
    CHECK((map.tileHeights() == Bytes{49, 60}) && (map.tileFlags() == Bytes{1, 0}));
    CHECK(map.bounds()[4] == 20.5f);
    CHECK(map.parse(Bytes(b.begin(), b.end() - 1)));
    Bytes other = b;
    put16(other, 4, 4);
    CHECK(map.parse(other));
    put16(other, 2, 0);
    CHECK(map.parse(other));
}

// A picture of the ground: its width, height and bytes a pixel, then its
// pixels, each the given bytes.
void putLandPicture(Bytes &b, uint32_t w, uint32_t h, const Bytes &pixel) {
    const size_t at = b.size();
    put32(b, at, w);
    put32(b, at + 4, h);
    put32(b, at + 8, uint32_t(pixel.size()));
    for (uint32_t i = 0; i < w * h; ++i)
        b.insert(b.end(), pixel.begin(), pixel.end());
}

// A ground of 3 x 2 cells: its header, its cells, then three sizes of six
// textures (A, R, G, B) and seven masks; then cut, and out of order.
void testLnd() {
    Bytes b = {3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    putLandPicture(b, 3, 2, {255, 10, 20, 30});
    for (uint8_t level = 0; level < 3; ++level) {
        const size_t at = b.size();
        put32(b, at, 64u >> level);
        b.push_back(level);
        for (int t = 0; t < 6; ++t)
            putLandPicture(b, 4, 4, {0, uint8_t(t), 2, 3});
        for (int m = 0; m < 7; ++m)
            putLandPicture(b, 4, 2, {uint8_t(100 + m)});
    }
    Land land;
    CHECK(!land.parse(b));
    CHECK(land.width() == 3 && land.depth() == 2 && land.settings()[9] == 10);
    const uint8_t *cell = land.cell(2, 1);
    CHECK(cell[0] == 255 && cell[1] == 10 && cell[3] == 30);
    CHECK(land.levels().size() == 3 && land.levels()[2].scale == 16);
    if (land.levels().size() == 3) {
        const Land::Level &first = land.levels()[0];
        CHECK(first.textures.size() == 6 && first.masks.size() == 7);
        // A, R, G, B read as R, G, B, opaque.
        const uint8_t texel[4] = {5, 2, 3, 255};
        CHECK(first.textures[5].w == 4 && std::memcmp(first.textures[5].pixel(1, 1), texel, 4) == 0);
        CHECK(first.masks[6].width == 4 && first.masks[6].height == 2 && first.masks[6].pixels[0] == 106);
    }
    CHECK(land.parse(Bytes(b.begin(), b.end() - 1)));
    // The first size's number, after its scale, made 1.
    Bytes wrongOrder = b;
    wrongOrder[11 + 12 + 3 * 2 * 4 + 4] = 1;
    CHECK(land.parse(wrongOrder));
    Bytes other = b;
    other[0] = 2;
    CHECK(land.parse(other));
}

// An attribute map of 4 x 3 tiles, its chunks, then one cut short.
void testAttributeMap() {
    Bytes b = toBytes("TP2MTheme Park 2 Attribute Map File");
    b.push_back(0);
    const Bytes map = toBytes("MAP ");
    b.insert(b.end(), map.begin(), map.end());
    const size_t size = b.size();
    put32(b, size, 28 + 12);
    put32(b, size + 4, 4);
    put32(b, size + 8, 3);
    for (size_t i = 0; i < 5; ++i)
        put32(b, size + 12 + 4 * i, 8);
    for (uint8_t i = 0; i < 12; ++i)
        b.push_back(i);
    const Bytes end = toBytes("END ");
    b.insert(b.end(), end.begin(), end.end());
    put32(b, b.size(), 0);
    AttributeMap attributes;
    CHECK(!attributes.parse(b));
    CHECK(attributes.width() == 4 && attributes.height() == 3 && attributes.at(1, 2) == 9);
    CHECK(attributes.parse(Bytes(b.begin(), b.begin() + long(size) + 20)));
    CHECK(attributes.parse(toBytes("TP2X")));
}

// A saved park: its version; its cells by (x, y), their tiles, what's
// built on them, those of kind 7 with an entry before; then cut short, and
// without its cells' end.
void testSavedPark() {
    const Bytes data = tpw::testing::makeSavedPark([](int x, int y, Bytes &body) {
        body[46] = uint8_t(x == 5 ? 1 : 0); // a row of tiles 1
        if (y == 7 && x < 4)
            body[13] = 1; // path
        if (x == 9 && y == 9) {
            body[0] = 7;
            body[13] = 2; // a queue
        }
    });
    SavedPark saved;
    CHECK(!saved.parse(data));
    CHECK(saved.version() == "Test_1" && !saved.state().empty());
    CHECK(saved.cell(5, 100).tile == 1 && saved.cell(6, 100).tile == 0);
    CHECK(saved.cell(3, 7).built == SavedPark::Built::kPath && saved.cell(4, 7).built == SavedPark::Built::kNothing);
    CHECK(saved.cell(9, 9).kind == 7 && saved.cell(9, 9).built == SavedPark::Built::kQueue);
    CHECK(saved.cell(127, 127).kind == 3);
    CHECK(saved.parse(Bytes(data.begin(), data.end() - 2000)));
    CHECK(saved.parse(toBytes("TPWS")));
}

// Two objects, then the names of the files their numbers came from; then
// cut short.
void testOmp() {
    Bytes b = toBytes("OBJ_");
    put32(b, 4, 2);
    put32(b, 8, 60);
    const int32_t first[9] = {1, 0, 3, 330100, 0, 155600, 35000, 0, 0};
    const int32_t second[9] = {2, 20, 0, 349500, 17000, 472400, 0, 100000, -66000};
    for (const int32_t *record : {first, second}) {
        const size_t at = b.size();
        for (size_t i = 0; i < 9; ++i)
            put32(b, at + 4 * i, uint32_t(record[i]));
        b.resize(at + 60);
    }
    const Bytes incl = toBytes("INCL");
    b.insert(b.end(), incl.begin(), incl.end());
    put32(b, b.size(), 1);
    put32(b, b.size(), 7);
    const Bytes name = toBytes("par.h");
    b.insert(b.end(), name.begin(), name.end());
    b.insert(b.end(), {0, 0});
    ObjectMap objects;
    CHECK(!objects.parse(b));
    CHECK(objects.objects().size() == 2 && objects.includes() == std::vector<std::string>{"par.h"});
    if (objects.objects().size() == 2) {
        const ObjectMap::Object &a = objects.objects()[0];
        CHECK(a.kind == 1 && a.number == 3 && std::fabs(a.position[0] - 330.1f) < 1e-3f && a.radius == 35);
        const ObjectMap::Object &c = objects.objects()[1];
        CHECK(c.variant == 20 && c.position[1] == 17 && c.extra[1] == -66);
    }
    CHECK(objects.parse(Bytes(b.begin(), b.begin() + 100)));
    CHECK(objects.parse(Bytes(b.begin(), b.end() - 3)));
    CHECK(objects.parse(toBytes("OBJX")));
}

// A script as the game's compiler lays one out: a header of its sizes,
// its code (NAME, a loop testing a variable, ENDSLICE, BRANCH back), its
// strings and a variable's name; then broken.
void testRse() {
    Bytes b = toBytes("RSSEQ");
    b.insert(b.end(), {0x0f, 0x01, 0x00});
    put32(b, 0x08, 1);  // variables
    put32(b, 0x0c, 5);  // stack
    put32(b, 0x10, 50); // time slice
    put32(b, 0x14, 10); // limbo
    put32(b, 0x18, 0);
    put32(b, 0x1c, 0);
    const Bytes pad = toBytes("Pad Pad Pad Pad ");
    b.insert(b.end(), pad.begin(), pad.end());
    const uint32_t code[] = {0x80000025, 0x10000000,             // NAME "Test"
                             0x80000026, 0x40000000,             // a2: TEST VAR_X
                             0x80000003, 0x40000000, 0x0000ffff, // COPY VAR_X -1
                             0x80000006,                         // ENDSLICE
                             0x8000001f, 0x20000002};            // BRANCH a2
    put32(b, b.size(), uint32_t(std::size(code)));
    for (uint32_t w : code)
        put32(b, b.size(), w);
    put32(b, b.size(), 5);
    const Bytes strings = toBytes("Test");
    b.insert(b.end(), strings.begin(), strings.end());
    b.push_back(0);
    put32(b, b.size(), 6);
    const Bytes var = toBytes("VAR_X");
    b.insert(b.end(), var.begin(), var.end());
    b.push_back(0);
    Script script;
    CHECK(!script.parse(b));
    CHECK(script.stackSize() == 5 && script.limboSize() == 10 && script.timeSlice() == 50);
    CHECK(script.instructions().size() == 5 && script.variables() == std::vector<std::string>{"VAR_X"});
    if (script.instructions().size() == 5) {
        const Script::Instruction &copy = script.instructions()[2];
        CHECK(copy.opcode == 3 && copy.address == 4 && copy.operands[1].value == -1);
        CHECK(script.instructions()[4].operands[0].kind == Script::OperandKind::kAddress);
    }
    CHECK(script.string(0) == "Test" && script.string(9).empty());
    CHECK(std::string(Script::opcodeName(17)) == "WAITANIM" && Script::operandCount(76) == 7);
    CHECK(script.disassemble() == "#setstack 5\n#setlimbo 10\nvariable VAR_X\t\t; 0\n\n\tNAME\t\"Test\"\n"
                                  ".a2\tTEST\tVAR_X\n\tCOPY\tVAR_X\t-1\n\tENDSLICE\n\tBRANCH\ta2\n");
    // An opcode not known, an operand of no known kind, the strings cut.
    Bytes unknown = b;
    put32(unknown, 0x34 + 7 * 4, 0x80000004);
    CHECK(script.parse(unknown));
    Bytes flags = b;
    put32(flags, 0x34 + 4, 0x30000000);
    CHECK(script.parse(flags));
    CHECK(script.parse(Bytes(b.begin(), b.end() - 3)));
}

// An animation as the game lays one out: its header, a Bezier position
// track, rotation and scale keys, a vertex track, one track's record
// moving node 2, then its root; posed between keys; then a broken one.
void testAnimation() {
    Bytes b(0xb8);
    put32(b, 0, 0x1cd15d46);
    put32(b, 4, 221);
    put32(b, 0x30, 0x20);
    // Positions: (0,0,0) at frame 0 to (3,0,0) at 10, control points on the line.
    const size_t values = b.size();
    for (float x : {0.0f, 1.0f, 2.0f, 3.0f}) {
        putFloat(b, b.size(), x);
        putFloat(b, b.size(), 0);
        putFloat(b, b.size(), 0);
    }
    const size_t frames = b.size();
    put32(b, frames, 0);
    put32(b, frames + 4, 10);
    const size_t position = b.size();
    put32(b, position, 0x12);
    put16(b, position + 4, 4);
    put16(b, position + 6, 2);
    put32(b, position + 8, uint32_t(values));
    put32(b, position + 12, uint32_t(frames));
    // Rotations, x y z w: none at frame 0, a half turn about z at 10.
    const size_t rotation = b.size();
    const float keys[2][4] = {{0, 0, 0, 1}, {0, 0, 1, 0}};
    for (size_t k = 0; k < 2; ++k) {
        const size_t at = b.size();
        put16(b, at, uint16_t(10 * k));
        // The word after the frame doesn't end the keys: the record counts them.
        put16(b, at + 2, k ? 0 : 0xffff);
        for (size_t i = 0; i < 4; ++i)
            putFloat(b, at + 4 + 4 * i, keys[k][i]);
    }
    // Scales: 1 at frame 0, 3 at 10.
    const size_t scale = b.size();
    for (size_t k = 0; k < 2; ++k) {
        const size_t at = b.size();
        put32(b, at, uint32_t(10 * k));
        for (size_t i = 0; i < 3; ++i)
            putFloat(b, at + 4 + 4 * i, k ? 3.0f : 1.0f);
    }
    // Vertices: position 0 at (1, -2, 3) at frame 0, (3, 0, -1) at 10, in
    // signed 10-bit steps of 0.5 from (0, -1, 2); a second number past
    // the mesh's, left alone.
    const size_t vertices = b.size();
    b.resize(vertices + 0x34);
    put16(b, vertices, 3);
    put16(b, vertices + 2, 1);
    const size_t runs = b.size();
    put32(b, vertices + 12, uint32_t(runs));
    const float offset[3] = {0, -1, 2};
    for (size_t i = 0; i < 3; ++i) {
        putFloat(b, vertices + 0x14 + 4 * i, offset[i]);
        putFloat(b, vertices + 0x20 + 4 * i, 0.5f);
    }
    b.resize(runs + 20);
    const size_t numbers = b.size();
    put16(b, numbers, 0);
    put16(b, numbers + 2, 7);
    const size_t keyFrames = b.size();
    put16(b, keyFrames, 0);
    put16(b, keyFrames + 2, 10);
    const size_t keyWords = b.size();
    auto word = [](int x, int y, int z) {
        return uint32_t(x & 0x3ff) | uint32_t(y & 0x3ff) << 10 | uint32_t(z & 0x3ff) << 20;
    };
    put32(b, keyWords, word(2, -2, 2));     // (1, -2, 3)
    put32(b, keyWords + 4, word(0, 0, 0));  // the number past the mesh's
    put32(b, keyWords + 8, word(6, 2, -6)); // (3, 0, -1)
    put32(b, keyWords + 12, word(0, 0, 0));
    put16(b, runs, 2);
    put16(b, runs + 2, 2);
    put32(b, runs + 4, uint32_t(numbers));
    put32(b, runs + 8, uint32_t(keyFrames));
    put32(b, runs + 12, uint32_t(keyWords));

    const size_t track = b.size();
    put32(b, track, 0);
    put32(b, track + 4, 0x1 | 0x18 | 0x180 | 0x1000);
    put32(b, track + 0x0c, 10);
    put32(b, track + 0x10, 2 << 16 | 2); // scale keys, rotation keys
    put32(b, track + 0x14, 2);
    put32(b, track + 0x18, uint32_t(position));
    put32(b, track + 0x1c, uint32_t(rotation));
    put32(b, track + 0x20, uint32_t(scale));
    put32(b, track + 0x28, uint32_t(vertices));
    b.resize(track + 0x40);
    const size_t root = b.size();
    b.resize(root + 0x48);
    put32(b, root + 8, 10);
    put32(b, root + 0x2c, uint32_t(track));

    Animation animation;
    CHECK(!animation.parse(b));
    CHECK(animation.frames() == 10 && animation.tracks().size() == 1);
    if (animation.tracks().size() != 1)
        return;
    const Animation::Track &t = animation.tracks()[0];
    CHECK(t.target == 2 && t.curve == Animation::Curve::kBezier && t.rotations.size() == 2 && t.scales.size() == 2);
    // Halfway: at x 1.5, scaled 2, turned a quarter (x onto y).
    const gfx::Mat4 m = Animation::pose(t, 5, gfx::identity());
    CHECK(std::fabs(m[12] - 1.5f) < 1e-4f);
    CHECK(std::fabs(m[0]) < 1e-4f && std::fabs(m[1] - 2) < 1e-4f && std::fabs(m[10] - 2) < 1e-4f);
    // Held past the last key.
    CHECK(std::fabs(Animation::pose(t, 20, gfx::identity())[12] - 3) < 1e-4f);
    // The vertex track: halfway at (2, -1, 1); the number past the mesh's
    // positions left alone, and position 1, not in the run.
    CHECK(t.vertexRuns.size() == 1 && t.vertexRuns[0].places.size() == 4);
    std::vector<gfx::Vec3> places{{9, 9, 9}, {8, 8, 8}};
    Animation::poseVertices(t, 5, places);
    CHECK((places[0] == gfx::Vec3{2, -1, 1}) && (places[1] == gfx::Vec3{8, 8, 8}));
    Animation::poseVertices(t, 0, places);
    CHECK((places[0] == gfx::Vec3{1, -2, 3}));

    Bytes model = b;
    put32(model, 0x50, 0xb8);
    CHECK(animation.parse(model));
    Bytes kind = b;
    put32(kind, position, 0x13);
    CHECK(animation.parse(kind));
    Bytes lost = b;
    put32(lost, root + 0x2c, uint32_t(track + 4));
    CHECK(animation.parse(lost));
    Bytes cut = b;
    put16(cut, runs, 900);
    CHECK(animation.parse(cut));
}

// A particle library of two effects, as the game's: its count and record
// size, then each record's 70 words and its name; then cut and broken.
void testParticleLibrary() {
    Bytes b(8 + 2 * 320);
    put32(b, 0, 2);
    put32(b, 4, 320);
    const size_t second = 8 + 320;
    put32(b, second + 4 * 29, 3000);
    put32(b, second + 4 * 54, 0xa2f84400);
    put32(b, second + 4 * 69, 0xa0806bf0);
    const std::string name = "Twinkle";
    std::copy(name.begin(), name.end(), b.begin() + long(second + 280));
    ParticleLibrary library;
    CHECK(!library.parse(b));
    CHECK(library.effects().size() == 2 && library.effects()[1].name == "Twinkle");
    CHECK(library.effects()[1].lifeMs == 3000 && library.effects()[1].colours[0] == 0xa2f84400 &&
          library.effects()[1].colours[15] == 0xa0806bf0 && library.effects()[0].name.empty());
    CHECK(library.parse(Bytes(b.begin(), b.end() - 1)));
    Bytes size = b;
    put32(size, 4, 300);
    CHECK(library.parse(size));
}

const std::vector<std::pair<uint32_t, std::vector<MapGroup>>> kEffects = {
    {224, {{100, 0xffff, {{1, 0xffff, 1669, 1}}}}},
    {3,
     {{12, 0x5555, {{2, 0x7fff, 100, 2}, {3, 0xffff, 200, 2}}, {{0, 10}, {11, 100}}},
      {80, 0xaaaa, {{1, 0xffff, 300, 1}}}}},
};

// A category of two effects: one sound; two groups, the first of two
// sounds and links to other groups. Read back, picked from by rolls, cut.
void testSoundMap() {
    SoundMap map;
    const Bytes effects = makeEffectMap(kEffects);
    const Bytes banks = makeBankMap({"Sound\\Ride", "Sound\\sSfx"});
    CHECK(!map.parse(effects, banks));
    CHECK((map.banks() == std::vector<std::string>{"Sound\\Ride", "Sound\\sSfx"}));
    CHECK(map.effects().size() == 2 && !map.find(4));
    const SoundMap::Effect *one = map.find(224);
    CHECK(one && one->groups.size() == 1 && one->groups[0].sounds.size() == 1);
    if (!one || one->groups.empty() || one->groups[0].sounds.empty())
        return;
    const SoundMap::Sound &s = one->groups[0].sounds[0];
    CHECK(s.entry == 0 && s.bank == 0 && s.millis == 1669 && one->groups[0].volumeHigh == 100);
    const SoundMap::Effect *two = map.find(3);
    CHECK(two && two->groups.size() == 2);
    if (!two || two->groups.size() != 2)
        return;
    CHECK(two->groups[0].volumeLow == 12 && two->groups[0].weight == 0x5555 && two->groups[1].sounds.size() == 1);
    const auto &links = two->groups[0].links;
    CHECK(links.size() == 2 && two->groups[1].links.empty());
    if (links.size() == 2)
        CHECK(links[0].group == 0 && links[0].low == 0 && links[0].high == 10 && links[1].group == 1 &&
              links[1].low == 11 && links[1].high == 100);
    const SoundMap::Group *group = nullptr;
    const SoundMap::Sound *picked = SoundMap::pick(*two, 0x1000, 0x8000, &group);
    CHECK(group == &two->groups[0] && picked && picked->entry == 2);
    picked = SoundMap::pick(*two, 0x1000, 0x7fff, &group);
    CHECK(picked && picked->entry == 1);
    picked = SoundMap::pick(*two, 0x6000, 0, &group);
    CHECK(group == &two->groups[1] && picked && picked->entry == 0 && picked->millis == 300);
    CHECK(map.parse(Bytes(effects.begin(), effects.end() - 1), banks));
    CHECK(map.parse(effects, Bytes(banks.begin(), banks.end() - 1)));
}

// A category in a game's folder, its sound in a bank beside it: chosen and
// played into a mixer, which then mixes it.
void testSoundEffects(const std::filesystem::path &tmp) {
    const std::filesystem::path sound = tmp / "game" / "Data" / "global" / "sound";
    test::writeFile(sound / "cat_testSFX.map", makeEffectMap(kEffects));
    test::writeFile(sound / "cat_testBANK.map", makeBankMap({"Sound\\Ride", "Sound\\sSfx"}));
    Bytes bank;
    put32(bank, 0, 1);
    put32(bank, 4, 8);
    putSound(bank, "loud.wav", 0x02, 22050, Bytes(2000, 0x40), 0);
    test::writeFile(sound / "RideHD.sdt", bank);
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    SoundEffects effects;
    CHECK(!effects.load(files, "global/sound", {"test"}));
    SoundEffects::Choice choice;
    CHECK(effects.choose(224, choice) && choice.bank == "global/Sound/RideHD.sdt" && choice.entry == 0 &&
          choice.volume == 100);
    CHECK(!effects.choose(7, choice));
    audio::Mixer mixer(22050);
    CHECK(effects.play(mixer, 224) != audio::kNoSound);
    CHECK(effects.play(mixer, 7) == audio::kNoSound);
    std::vector<int16_t> out(2 * 100);
    mixer.mix(out.data(), 100);
    CHECK(std::any_of(out.begin(), out.end(), [](int16_t v) { return v != 0; }));
    SoundEffects missing;
    CHECK(missing.load(files, "global/sound", {"none"}));
}

// A sheet of two frames: a 3 × 2 one of a run and copies, and a 1 × 1
// one; its palette's colours stored blue first.
Bytes makeSpriteSheet() {
    Bytes b(8 + 1024);
    put16(b, 0, 3);
    put16(b, 2, 3);
    put32(b, 4, 2);
    b[8 + 4 * 7] = 10; // colour 7: blue 10, green 20, red 30, opaque
    b[8 + 4 * 7 + 1] = 20;
    b[8 + 4 * 7 + 2] = 30;
    b[8 + 4 * 7 + 3] = 255;
    auto frame = [&](int w, int h, int left, int top, const Bytes &rows) {
        const size_t at = b.size();
        put32(b, at, uint32_t(rows.size()));
        put16(b, at + 4, uint16_t(w));
        put16(b, at + 6, uint16_t(h));
        put16(b, at + 8, 128);
        put16(b, at + 10, 128);
        put32(b, at + 12, uint32_t(left));
        put32(b, at + 16, uint32_t(top));
        b.insert(b.end(), rows.begin(), rows.end());
    };
    // Row 0: 0xfe 7, two sevens, then 1 copied: 0. Row 1: three 7s.
    frame(3, 2, -1, -2, {4, 0xfe, 7, 1, 0, 2, 0xfd, 7});
    frame(1, 1, 0, -1, {2, 1, 7});
    return b;
}

// A sprite's frames and animations read back; a frame's run past its row
// fails; the frame an animation shows for headings and times.
void testSprites() {
    SpriteSheet sheet;
    const Bytes data = makeSpriteSheet();
    CHECK(!sheet.parse(data));
    CHECK(sheet.frames().size() == 2 && sheet.palette()[7].r == 30 && sheet.palette()[7].b == 10);
    if (sheet.frames().size() == 2) {
        const SpriteSheet::Frame &f = sheet.frames()[0];
        CHECK(f.width == 3 && f.height == 2 && f.left == -1 && f.top == -2 && f.scaleX == 128);
        CHECK((f.pixels == Bytes{7, 7, 0, 7, 7, 7}));
    }
    Bytes cut = data;
    cut.resize(cut.size() - 2);
    CHECK(sheet.parse(cut));
    Bytes wide = data;
    wide[8 + 1024 + 20 + 1] = 0xfc; // four sevens in a row of three
    CHECK(sheet.parse(wide));

    Bytes esp(350);
    const std::string magic = "ESP_FILE2.00SPR_KB.TPS";
    std::copy(magic.begin(), magic.end(), esp.begin());
    // Slot 1: from frame 125, 8 frames in 5 directions.
    put16(esp, 270 + 4, 125);
    esp[270 + 4 + 2] = 8;
    esp[270 + 4 + 3] = 5;
    SpriteSet set;
    CHECK(!set.parse(esp));
    CHECK(set.sheet() == "SPR_KB.TPS" && set.animations()[1].first == 125 && set.animations()[1].frames == 8);
    CHECK(set.animations()[0].frames == 0);

    const SpriteSet::Animation &walk = set.animations()[1];
    constexpr float kQuarter = 1.5707963f;
    // Away, at its first frame; right, the third direction, three frames
    // on; left, mirrored; towards, round once and four on.
    SpritePose p = spritePose(walk, 0, 0);
    CHECK(p.frame == 125 && !p.mirror);
    p = spritePose(walk, kQuarter, 3);
    CHECK(p.frame == 125 + 2 * 8 + 3 && !p.mirror);
    p = spritePose(walk, -kQuarter, 0);
    CHECK(p.frame == 125 + 2 * 8 && p.mirror);
    p = spritePose(walk, 2 * kQuarter, 12);
    CHECK(p.frame == 125 + 4 * 8 + 4 && !p.mirror);
    CHECK(set.parse(Bytes(esp.begin(), esp.end() - 1)));
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.formats", {
                                   {"wad", [](const std::filesystem::path &tmp) { testWad(tmp); }},
                                   {"sound-map", [](const std::filesystem::path &) { testSoundMap(); }},
                                   {"sprites", [](const std::filesystem::path &) { testSprites(); }},
                                   {"sound-effects", [](const std::filesystem::path &tmp) { testSoundEffects(tmp); }},
                                   {"mount", [](const std::filesystem::path &tmp) { testMount(tmp); }},
                                   {"textures", [](const std::filesystem::path &tmp) { testFindTexture(tmp); }},
                                   {"sam", [](const std::filesystem::path &) { testSam(); }},
                                   {"str", [](const std::filesystem::path &) { testStr(); }},
                                   {"bf4", [](const std::filesystem::path &) { testBf4(); }},
                                   {"sdt", [](const std::filesystem::path &) { testSdt(); }},
                                   {"md2", [](const std::filesystem::path &) { testMd2(); }},
                                   {"animation", [](const std::filesystem::path &) { testAnimation(); }},
                                   {"particle-library", [](const std::filesystem::path &) { testParticleLibrary(); }},
                                   {"hmp", [](const std::filesystem::path &) { testHmp(); }},
                                   {"lnd", [](const std::filesystem::path &) { testLnd(); }},
                                   {"attribute-map", [](const std::filesystem::path &) { testAttributeMap(); }},
                                   {"saved-park", [](const std::filesystem::path &) { testSavedPark(); }},
                                   {"omp", [](const std::filesystem::path &) { testOmp(); }},
                                   {"rse", [](const std::filesystem::path &) { testRse(); }},
                               });
