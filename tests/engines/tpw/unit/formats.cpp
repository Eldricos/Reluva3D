// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's files, built here as SimCoaster's are laid
// out (docs/engines/tpw/games/simcoaster/formats/): archives and mounting
// them, settings files, string tables and their character set, fonts,
// sound banks.
//
//   reluva-tests tpw.unit.formats

#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/md2.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/formats/wad.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/textures.h"

#include "tests/engines/tpw/fixtures.h"
#include "tests/support/test.h"

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

// A sound's 0x28-byte header and its data: the format word is the rate,
// 16 bits, then the kind (0x20 MPEG, 0x01 stereo).
void putSound(Bytes &b, const std::string &name, uint8_t kind, uint16_t rate, const Bytes &data, uint32_t pcmBytes) {
    const size_t at = b.size();
    put32(b, at, 0x28);
    put32(b, at + 4, uint32_t(data.size()));
    for (size_t i = 0; i < 16; ++i)
        b.push_back(i < name.size() ? uint8_t(name[i]) : 0);
    b.insert(b.end(), {uint8_t(rate), uint8_t(rate >> 8), 16, kind});
    put32(b, at + 0x1c, 0);
    put32(b, at + 0x20, pcmBytes);
    put32(b, at + 0x24, 0);
    b.insert(b.end(), data.begin(), data.end());
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
    CHECK(mesh.uv[3][0] == 1 && mesh.uv[3][1] == 1);
    CHECK(mesh.groups.size() == 2 && mesh.groups[0].texture == 1 && mesh.groups[0].faceCount == 1);
    CHECK(mesh.groups[1].texture == Model::Group::kNoTexture && mesh.groups[1].firstFace == 1 &&
          mesh.groups[1].flags == 1);

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

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.formats", {
                                   {"wad", [](const std::filesystem::path &tmp) { testWad(tmp); }},
                                   {"mount", [](const std::filesystem::path &tmp) { testMount(tmp); }},
                                   {"textures", [](const std::filesystem::path &tmp) { testFindTexture(tmp); }},
                                   {"sam", [](const std::filesystem::path &) { testSam(); }},
                                   {"str", [](const std::filesystem::path &) { testStr(); }},
                                   {"bf4", [](const std::filesystem::path &) { testBf4(); }},
                                   {"sdt", [](const std::filesystem::path &) { testSdt(); }},
                                   {"md2", [](const std::filesystem::path &) { testMd2(); }},
                               });
