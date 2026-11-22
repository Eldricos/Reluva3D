// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: writing alt.res archives and saved
// games, and the LZSS and checks that takes.

#include "common/bytes.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/formats/save_writer.h"
#include "engines/botf/game/game_state.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

Bytes slurp(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void testCompression() {
    // Repeats, a run, text and every byte value: each reads back the same,
    // whatever the window starts filled with.
    Bytes mixed;
    for (int i = 0; i < 3000; ++i)
        mixed.push_back(uint8_t(i % 7 == 0 ? i * 37 : 'a' + i % 5));
    const std::string words = "Sol Sol Sol Earth Mars Venus Sol Earth";
    for (const Bytes &raw : {mixed, Bytes(5000, 0x20), Bytes(words.begin(), words.end()), Bytes{8}, Bytes{}}) {
        const Bytes packed = compressLzss(raw.data(), raw.size());
        Bytes zeros;
        Bytes spaces;
        CHECK(!decompressLzss(packed.data(), packed.size(), raw.size(), zeros) && zeros == raw);
        CHECK(!decompressLzss(packed.data(), packed.size(), raw.size(), spaces, nullptr, 0x20) && spaces == raw);
    }
    // 5,000 spaces: 17 bytes to a reference, two bytes and a bit each.
    CHECK(compressLzss(Bytes(5000, 0x20).data(), 5000).size() < 700);
}

void testChecks() {
    // As the game's own saves store and check these: version's 8, four
    // zeros, a count of 2, a province's system number.
    const Bytes version = {0x01, 0x08};
    const Bytes zeros = {0x01, 0x00, 0x10, 0x01};
    const Bytes two = {0x03, 0x02, 0x00, 0x00, 0x02};
    const Bytes system = {0x03, 0x2A, 0x00};
    CHECK(altResCheck(version.data(), version.size()) == 0x0108);
    CHECK(altResCheck(zeros.data(), zeros.size()) == 0x2330);
    CHECK(altResCheck(two.data(), two.size()) == 0x3F30);
    CHECK(altResCheck(system.data(), system.size()) == 0x1A63);
    CHECK(altResCheck(nullptr, 0) == 0);
}

void testWriteAltRes(const std::filesystem::path &tmp) {
    const std::string text = "Romulus";
    const Bytes raw(text.begin(), text.end());
    AltResFile first{"first", compressLzss(raw.data(), raw.size()), uint32_t(raw.size())};
    first.check = altResCheck(first.stored.data(), first.stored.size());
    const std::vector<AltResFile> files = {first, {"empty", {}, 0, 1, 0, true}, {"placed", {}, 0, 1, 0, false}};
    CHECK(!writeAltRes(tmp / "out.res", "game3.sav", files));
    // Padded to the header and a whole 16 KB block.
    CHECK(std::filesystem::file_size(tmp / "out.res") == 24 + 16384);
    AltResArchive a;
    CHECK(!a.open(tmp / "out.res") && a.name() == "game3.sav" && a.entries().size() == 3);
    Bytes back;
    CHECK(!a.read("first", back) && back == raw);
    CHECK(a.entries()[0].offset == 24 && a.entries()[0].check == first.check && a.entries()[2].index == 2);
    // An empty file at 0, or where it would be.
    CHECK(a.entries()[1].offset == 0 && a.entries()[2].offset == 24 + first.stored.size());
    // Names too long for the header or the directory aren't written.
    CHECK(bool(writeAltRes(tmp / "x.res", "a name of sixteen", files)));
    CHECK(bool(writeAltRes(tmp / "x.res", "x", {{"a file name of 16", {}, 0}})));
}

void testSaveFiles(const std::filesystem::path &tmp) {
    writeFile(tmp / "game0.sav", makeFullSave());
    SaveFiles files;
    CHECK(!files.open(tmp / "game0.sav") && files.name() == "game0.sav" && files.has("systinfo"));
    // result.lst stays in its chunks, not decoded.
    CHECK(files.has("result.lst") && !files.file("result.lst") && files.file("empsInfo"));
    // Written unchanged, then read and written again: the same bytes.
    CHECK(!files.write(tmp / "once.sav"));
    SaveFiles again;
    CHECK(!again.open(tmp / "once.sav") && !again.write(tmp / "twice.sav"));
    CHECK(slurp(tmp / "once.sav") == slurp(tmp / "twice.sav"));
    AltResArchive a;
    Bytes stored;
    CHECK(!a.open(tmp / "once.sav") && !a.readStored(a.entries().back(), stored) && stored == resultList());
    // A file changed is compressed again, with its check.
    (*files.file("empsInfo"))[0] = 'C';
    files.add("provInfo4") = {1, 0, 0, 0, 0, 0};
    CHECK(!files.write(tmp / "changed.sav"));
    std::array<SavedEmpire, 5> empires;
    CHECK(!readSavedEmpires(tmp / "changed.sav", empires) && empires[0].name == "C");
    CHECK(empires[4].systems == std::vector<int>{0});
    CHECK(!a.open(tmp / "changed.sav"));
    for (const AltResArchive::Entry &e : a.entries())
        if (e.name == "empsInfo") {
            CHECK(!a.readStored(e, stored) && e.check == altResCheck(stored.data(), stored.size()));
        }
}

void testWriteSavedGame(const std::filesystem::path &tmp) {
    writeFile(tmp / "game0.sav", makeFullSave());
    GameState state;
    CHECK(!GameState::load(tmp / "game0.sav", state));
    SavedGame &g = state.data();
    g.summary.description = "Caf\xc3\xa9";
    g.summary.turn = 4;
    g.galaxy.sectors[2].claims = 0x10;
    g.galaxy.sectors[2].revealed = 0x12;
    SavedSystem &romulus = g.galaxy.systems[0];
    romulus.population = 360;
    romulus.maxPopulation = 340;
    romulus.controllingRace = -1;
    romulus.buying = false;
    romulus.planets[0].name = "Remus";
    romulus.planets[0].terraformDone = 120;
    romulus.buildings[0].count = 4;
    romulus.buildings.push_back({30, 1, 1});
    g.galaxy.stellarObjects[0].name = "Nebula C2";
    g.empires[4].credits = 900;
    g.empires[4].territoryAccess[1] = true;
    g.empires[4].systems = {0};
    g.ships[0].name = "IRW Valdore";
    g.ships[0].experience = 50;
    g.taskForces[0].destinationX = 2;
    g.taskForces[0].destinationY = 1;
    CHECK(!state.save(tmp / "game5.sav"));
    // Padded as the game's.
    CHECK((std::filesystem::file_size(tmp / "game5.sav") - 24) % 16384 == 0);
    SavedGame b;
    CHECK(!readSavedGame(tmp / "game5.sav", b));
    CHECK(b.summary.name == "game5.sav" && b.summary.description == "Caf\xc3\xa9" && b.summary.turn == 4);
    CHECK(b.summary.galaxyShape == 2 && b.summary.galaxySize == 1);
    CHECK(b.summary.empire == 4 && b.galaxy.sectors[2].claims == 0x10 && b.galaxy.sectors[2].revealed == 0x12);
    const SavedSystem &r = b.galaxy.systems[0];
    CHECK(r.name == "Romulus" && r.population == 360 && r.maxPopulation == 340 && r.controllingRace == -1);
    CHECK(!r.buying && r.planets[0].name == "Remus" && r.planets[0].terraformDone == 120);
    CHECK(r.buildings.size() == 2 && r.buildings[0].count == 4 && r.buildings[1].building == 30);
    CHECK(b.galaxy.stellarObjects[0].name == "Nebula C2");
    CHECK(b.empires[4].credits == 900 && b.empires[4].territoryAccess[1] &&
          b.empires[4].systems == g.empires[4].systems);
    CHECK(b.ships[0].name == "IRW Valdore" && b.ships[0].experience == 50);
    CHECK(b.taskForces[0].destinationX == 2 && b.taskForces[0].destinationY == 1);
    // The credits as the turn began move with them; the planet's new name
    // ends in a line break, as its old one did.
    SaveFiles written;
    CHECK(!written.open(tmp / "game5.sav"));
    const std::vector<uint8_t> &emps = *written.file("empsInfo");
    CHECK(readLE32(&emps[4 * 444 + 0x98]) == 1000);
    const std::vector<uint8_t> &systems = *written.file("systInfo");
    CHECK(std::string(systems.begin() + 0x328, systems.begin() + 0x328 + 7) == std::string("Remus\n\0", 7));
    // Saved again unchanged: the same file.
    GameState reread;
    CHECK(!GameState::load(tmp / "game5.sav", reread) && !reread.save(tmp / "game5.sav"));
    SavedGame c;
    CHECK(!readSavedGame(tmp / "game5.sav", c) && c.empires[4].credits == 900);

    // What the files have no room for isn't written: a system no longer
    // the player's (its record would lose its production queue).
    GameState lost = state;
    lost.data().galaxy.systems[0].control = 1;
    CHECK(bool(lost.save(tmp / "game6.sav")));
    CHECK(!std::filesystem::exists(tmp / "game6.sav"));
    // Nor a game that wasn't read from a save.
    CHECK(bool(GameState(g).save(tmp / "game6.sav")));
}

void testShipsAddedAndRemoved(const std::filesystem::path &tmp) {
    writeFile(tmp / "game0.sav", makeFullSave());
    GameState state;
    CHECK(!GameState::load(tmp / "game0.sav", state));
    SavedGame &g = state.data();
    // 40 ships, more than the list has room for, in task forces of one;
    // then the first task force taken out, as the game takes one out:
    // the last in its place.
    for (int i = 1; i <= 40; ++i) {
        SavedShip ship = g.ships[0];
        ship.id = g.nextShipId++;
        ship.name = "IRW Ship " + std::to_string(i);
        ship.taskForce = ship.id;
        ship.agility = 13;
        SavedTaskForce force;
        force.id = ship.id;
        force.owner = 4;
        force.ships = 1;
        force.shipIds = {ship.id};
        force.speed = 2;
        force.abilities = 0x0C12;
        g.ships.push_back(ship);
        g.taskForces.push_back(force);
        ++g.nextTaskForceId;
    }
    g.ships.erase(g.ships.begin());
    g.taskForces[0] = g.taskForces.back();
    g.taskForces.pop_back();
    CHECK(!state.save(tmp / "game1.sav"));
    SavedGame b;
    CHECK(!readSavedGame(tmp / "game1.sav", b));
    CHECK(b.ships.size() == 40 && b.ships[0].name == "IRW Ship 1" && b.ships[39].agility == 13);
    CHECK(b.taskForces.size() == 40 && b.taskForces[0].id == 40 && b.taskForces[0].shipIds == std::vector<int>{40});
    CHECK(b.taskForces[0].speed == 2 && b.taskForces[0].abilities == 0x0C12 && b.taskForces[0].firstColonyShip == -1);
    CHECK(b.nextShipId == 41 && b.nextTaskForceId == 41);
    SaveFiles files;
    CHECK(!files.open(tmp / "game1.sav"));
    // The room grown by half each time from 32 to 48; GTForceList in the
    // task forces' order; a new task force's unused ship numbers -1.
    CHECK(readLE32(&(*files.file("GShipHead"))[0x14]) == 48 && files.file("GShipList")->size() == 48 * 116);
    const std::vector<uint8_t> &list = *files.file("GTForceList");
    CHECK(readLE16(&list[0]) == 1 && readLE16(&list[39 * 108]) == 40);
    CHECK(readLE32(&list[0x0C]) == 0xFFFFFFFF);
    // The Romulans' task forces, listed in their numbers' order.
    const std::vector<uint8_t> &romulans = *files.file("alienTFInfo4");
    CHECK(romulans.size() == 4 + 40 * 2 && readLE32(&romulans[0]) == 40);
    CHECK(readLE16(&romulans[4]) == 1 && readLE16(&romulans[4 + 39 * 2]) == 40);

    // A ship a computer player's plans list (an agent's AgtSh#) can't be
    // taken out.
    SaveFiles planned;
    CHECK(!planned.open(tmp / "game0.sav"));
    planned.add("AgtSh4") = {0, 0};
    CHECK(!planned.write(tmp / "planned.sav"));
    GameState plans;
    CHECK(!GameState::load(tmp / "planned.sav", plans));
    plans.data().ships.clear();
    plans.data().taskForces.clear();
    CHECK(bool(plans.save(tmp / "game2.sav")));
}

} // namespace

static const reluva::test::Suite
    kSuite("botf.unit.saving",
           {
               {"lzss compression", [](const std::filesystem::path &) { testCompression(); }},
               {"checks", [](const std::filesystem::path &) { testChecks(); }},
               {"alt.res written", [](const std::filesystem::path &tmp) { testWriteAltRes(tmp); }},
               {"save files", [](const std::filesystem::path &tmp) { testSaveFiles(tmp); }},
               {"saved game written", [](const std::filesystem::path &tmp) { testWriteSavedGame(tmp); }},
               {"ships added and removed", [](const std::filesystem::path &tmp) { testShipsAddedAndRemoved(tmp); }},
           });
