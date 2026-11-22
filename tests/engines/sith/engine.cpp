// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/sith/data/engine-record.txt.
//
//   reluva-tests sith.engine [--update]
//
// So far it recognises a Jedi Knight install and finds its CDs: add steps
// as the engine learns to read and run its games (see
// tests/engines/idtech3/engine.cpp), then --update and review the record.

#include "engines/registry.h"
#include "engines/sith/folders.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::Record rec;
    const MetaEngine *engine = findEngine("sith");
    if (!engine)
        return test::fail("sith isn't registered");
    rec.add("detect.empty", static_cast<int64_t>(engine->detect(dir).size()));

    sith::testing::makeInstall(dir);
    const std::vector<DetectedGame> found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.release", found.empty() ? std::string() : found[0].gameId + ": " + found[0].description);

    sith::testing::makeCd(tmp / "Disc 1", 1);
    sith::testing::makeCd(dir / "cd2", 2);
    const sith::GameFolders folders = sith::findFolders(dir, {});
    for (int i = 0; i < sith::kCdCount; ++i)
        rec.add("folders.cd" + std::to_string(i + 1),
                folders.cds[i].empty()
                    ? std::string("none")
                    : folders.cds[i].lexically_relative(tmp).generic_string() + " (" + folders.cdSources[i] + ")");
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sith/data/engine-record.txt", "sith.engine",
              "tests/engines/sith/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sith.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
