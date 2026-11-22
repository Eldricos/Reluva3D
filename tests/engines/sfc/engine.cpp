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
// It checks the install isn't taken for a game, and that the game can't be
// started without its program: add steps as the engine learns to read and
// run its games (see tests/engines/idtech3/engine.cpp), then --update and
// review the record.

#include "common/headless_system.h"
#include "engines/registry.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

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
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sfc/data/engine-record.txt", "sfc.engine",
              "tests/engines/sfc/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sfc.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
