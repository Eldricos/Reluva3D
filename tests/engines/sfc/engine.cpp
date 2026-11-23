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
// started without its program, and that what the program writes to the
// registry is kept in its save folder: add steps as the engine learns to read and
// run its games (see tests/engines/idtech3/engine.cpp), then --update and
// review the record.

#include "common/headless_system.h"
#include "common/stream.h"
#include "engines/registry.h"
#include "engines/sfc/host/game_process.h"

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
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sfc/data/engine-record.txt", "sfc.engine",
              "tests/engines/sfc/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sfc.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
