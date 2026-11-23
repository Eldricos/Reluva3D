// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/sfc/data/engine-record.txt.
//
//   reluva-tests sfc.engine [--update]
//
// It checks the install isn't taken for a game, that the game can't be
// started without its program, that what the program writes to the
// registry is kept in its save folder, and importing from CDs laid out as
// the game's (InstallShield 5): add steps as the engine learns to read and
// run its games (see tests/engines/idtech3/engine.cpp), then --update and
// review the record.

#include "common/headless_system.h"
#include "common/stream.h"
#include "engines/registry.h"
#include "engines/sfc/host/game_process.h"

#include "tests/support/installshield_writer.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <algorithm>

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::Record rec;
    const MetaEngine *engine = findEngine("sfc");
    if (!engine)
        return test::fail("sfc isn't registered");
    rec.add("detect.games", static_cast<int64_t>(engine->detect(dir).size()));
    // Without StarFleet2.exe: an error naming it, and nothing shown.
    GameTarget target{"sfc2", "sfc", "sfc2", "Star Trek: Starfleet Command II", dir, tmp / "saves", {}};
    HeadlessSystem sys;
    const Error err = engine->create(sys, target)->run();
    CHECK(err);
    CHECK(err.message.find("StarFleet2.exe") != std::string::npos);
    rec.add("run.no_program", err ? "error" : "ran");

    // A registry value the program writes (as RegSetValueExA tells of it)
    // is kept in the save folder and there again for the next run.
    const std::filesystem::path saves = tmp / "registry saves";
    const std::string key = "HKEY_LOCAL_MACHINE\\SOFTWARE\\WON\\CDKeys";
    const std::vector<uint8_t> value = {0x01, 0x23, 0xab, 0xff};
    {
        sfc::GameProcess game(dir, 16);
        game.setSaveFolder(saves);
        game.process().setRegistryValue(key, "StarfleetCommand2", 3, value);
        game.process().registryWritten(key, "StarfleetCommand2");
    }
    std::vector<uint8_t> file;
    CHECK(readFile(saves / sfc::GameProcess::kRegistryFile, file));
    // Its value's line, after the comment, tabs shown as |.
    std::string text(file.begin(), file.end());
    text = text.substr(text.find('\n') + 1);
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    std::replace(text.begin(), text.end(), '\t', '|');
    rec.add("registry.value", text);
    {
        sfc::GameProcess game(dir, 16);
        game.setSaveFolder(saves);
        uint32_t type = 0;
        const auto read =
            game.process().registryValue("hkey_local_machine\\software\\won\\cdkeys", "starfleetcommand2", &type);
        CHECK(read && *read == value && type == 3);
    }
    // Two CDs as the game's: its setup's groups, the game's files on the
    // first, the voices running on into the second, readme.txt loose on it.
    // Not the retail files: an unknown release.
    {
        using test::CabinetFile;
        const std::filesystem::path cd1 = tmp / "cd1";
        const std::filesystem::path cd2 = tmp / "cd2";
        std::vector<CabinetFile> files = {
            {"", "StarFleet2.exe", test::toBytes("MZ the program")},
            {"Assets\\Sprites", "sprites.q3", test::toBytes("MDM! pictures")},
            {"Assets\\Sound", "sfcmusic.zip", test::toBytes("music")},
            {"Assets\\Sound", "SFCVoice.zip", std::vector<uint8_t>(30000, 7), true, 1, -1, true},
            {"", "SFC2Intro.exe", test::toBytes("the opening"), true, 2},
            {"", "sfc2.ico", test::toBytes("icon"), true, 2},
            {"EReg", "Reg32.exe", test::toBytes("registration"), true, 2},
            {"SFB", "index.htm", test::toBytes("rules"), true, 2},
            {"", "readme.txt", test::toBytes("Read me."), false, 2, -1, false, true},
        };
        test::writeCabinet({cd1, cd2}, files,
                           {{"Program Executable Files", 0, 1},
                            {"Music", 2, 2},
                            {"Voices", 3, 3},
                            {"Intro Movie", 4, 4},
                            {"Install Files", 5, 5},
                            {"Ereg", 6, 6},
                            {"Extras", 7, 7},
                            {"ReadMe", 8, 8}},
                           5);
        test::writeFile(cd2 / "readme.txt", test::toBytes("Read me."));
        std::vector<ImportPlan> plans = engine->planImport(cd1);
        CHECK(plans.size() == 1);
        if (plans.size() == 1) {
            ImportPlan &plan = plans.front();
            rec.add("import.game", plan.game.gameId + " " + plan.game.description);
            rec.add("import.first_cd.files", int64_t(plan.files.size()));
            std::string message;
            CHECK(!engine->addToImport(plan, tmp, message));
            CHECK(engine->addToImport(plan, cd2, message));
            rec.add("import.both_cds.files", int64_t(plan.files.size()));
            CHECK(!runImport(plan, tmp / "imported", [](uint64_t, uint64_t) { return true; }));
            rec.add("import.detected", int64_t(engine->detect(tmp / "imported").size()));
        }
    }
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sfc/data/engine-record.txt", "sfc.engine",
              "tests/engines/sfc/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sfc.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
