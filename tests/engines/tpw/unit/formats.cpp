// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's files, built here as SimCoaster's are laid
// out (docs/engines/tpw/games/simcoaster/formats/): archives and mounting
// them.
//
//   reluva-tests tpw.unit.formats

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

} // namespace

static const reluva::test::Suite kSuite("tpw.unit.formats",
                                        {
                                            {"wad", [](const std::filesystem::path &tmp) { testWad(tmp); }},
                                            {"mount", [](const std::filesystem::path &tmp) { testMount(tmp); }},
                                        });
