// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/tpw/data/engine-record.txt.
//
//   reluva-tests tpw.engine [--update]
//
// So far it checks that the install is found as an unknown release of
// SimCoaster: add steps as the engine learns to read and run its games
// (see tests/engines/idtech3/engine.cpp), then --update and review the
// record.

#include "engines/registry.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir / "Data");
    for (const char *name : {"Game.exe", "Data/ui.wad", "Data/Challenges.sam"})
        std::ofstream(dir / name) << name;
    test::Record rec;
    const MetaEngine *engine = findEngine("tpw");
    if (!engine)
        return test::fail("tpw isn't registered");
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    for (const DetectedGame &g : found)
        rec.add("detect.game", g.gameId + ": " + g.description);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/tpw/data/engine-record.txt", "tpw.engine",
              "tests/engines/tpw/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("tpw.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
