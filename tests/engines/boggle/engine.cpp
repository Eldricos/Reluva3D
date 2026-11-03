// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Boggle (1997) as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/boggle/data/engine-record.txt.
//
//   reluva-tests boggle.engine [--update]
//
// The synthetic resource corpus is checked and fingerprinted. It must not
// match the retail detection table; add gameplay steps when implemented.

#include "engines/registry.h"
#include "engines/boggle/resources.h"
#include "tests/engines/boggle/fixtures.h"

#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

void engineTest(const std::filesystem::path& tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    boggle::fixtures::install(dir);
    test::Record rec;
    const MetaEngine* engine = findEngine("boggle");
    if (!engine)
        return test::fail("boggle isn't registered");
    rec.add("detect.games", static_cast<int64_t>(engine->detect(dir).size()));
    boggle::Resources resources;
    if (auto err = resources.open(dir)) return test::fail(err.message);
    boggle::ResourceSummary summary;
    if (auto err = resources.check(summary)) return test::fail(err.message);
    rec.add("resources.images", int64_t(summary.images));
    rec.add("resources.sounds", int64_t(summary.sounds));
    rec.add("resources.palettes", int64_t(summary.palettes));
    rec.add("resources.dice", int64_t(summary.dice));
    rec.add("resources.vertices", int64_t(summary.vertices));
    rec.add("resources.strings", int64_t(summary.strings));
    rec.add("resources.decoded", summary.digest);
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/boggle/data/engine-record.txt", "boggle.engine",
              "tests/engines/boggle/engine.cpp");
}

} // namespace

static const reluva::test::Suite suite("boggle.engine", {
    {"synthetic resource install", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});
