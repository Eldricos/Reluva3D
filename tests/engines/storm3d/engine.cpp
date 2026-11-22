// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/storm3d/data/engine-record.txt.
//
//   reluva-tests storm3d.engine [--update]
//
// So far it only checks its install is found as Star Trek: Armada: add
// steps as the engine learns to read and run its games (see
// tests/engines/idtech3/engine.cpp), then --update and review the record.

#include "engines/registry.h"

#include <fstream>

#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    // Armada's files by name only, so an unknown release.
    for (const char *name : {"Armada.exe", "assets.zfs", "objtxt.zfs"})
        std::ofstream(dir / name) << "x";
    test::Record rec;
    const MetaEngine *engine = findEngine("storm3d");
    if (!engine)
        return test::fail("storm3d isn't registered");
    const std::vector<DetectedGame> found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    for (const DetectedGame &game : found)
        rec.add("detect.game", game.gameId + ": " + game.description);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/storm3d/data/engine-record.txt", "storm3d.engine",
              "tests/engines/storm3d/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
