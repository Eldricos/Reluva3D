// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command's unit tests: detection.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("sfc");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("sfc");
    CHECK(engine && engine->detect(tmp).empty());
}

// The game's files with other contents (any other release, or a patched
// install) are still found, as an unknown release; the names' case is the
// install's, not the table's.
void testUnknownRelease(const std::filesystem::path &tmp) {
    std::filesystem::create_directories(tmp / "assets" / "sprites");
    std::ofstream(tmp / "starfleet2.exe") << "x";
    std::ofstream(tmp / "assets" / "sprites" / "SPRITES.Q3") << "x";
    const MetaEngine *engine = findEngine("sfc");
    CHECK(engine != nullptr);
    if (!engine)
        return;
    auto found = engine->detect(tmp);
    CHECK(found.size() == 1);
    CHECK(!found.empty() && found[0].gameId == "sfc2" &&
          found[0].description == "Star Trek: Starfleet Command II (unknown release)");
}

} // namespace

static const reluva::test::Suite
    kSuite("sfc.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
           });
