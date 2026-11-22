// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's files, built here as SimCoaster's are laid
// out (docs/engines/tpw/games/simcoaster/formats/): archives and mounting
// them, settings files, string tables and their character set.
//
//   reluva-tests tpw.unit.formats

#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/formats/wad.h"
#include "engines/tpw/game_files.h"

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

} // namespace

static const reluva::test::Suite kSuite("tpw.unit.formats",
                                        {
                                            {"wad", [](const std::filesystem::path &tmp) { testWad(tmp); }},
                                            {"mount", [](const std::filesystem::path &tmp) { testMount(tmp); }},
                                            {"sam", [](const std::filesystem::path &) { testSam(); }},
                                            {"str", [](const std::filesystem::path &) { testStr(); }},
                                        });
