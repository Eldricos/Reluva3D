// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine && engine->detect(tmp).empty());
}

void touch(const std::filesystem::path &file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "not the game's";
}

// A release not in the table is still found by its files' names, in any
// case, as an unknown release.
void testUnknownRelease(const std::filesystem::path &tmp) {
    touch(tmp / "lego1.dll");
    touch(tmp / "LEGO" / "Data" / "world.wdb");
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine != nullptr);
    const std::vector<DetectedGame> found = engine->detect(tmp);
    CHECK(found.size() == 1);
    CHECK(found[0].gameId == "legoisland");
    CHECK(found[0].description == "LEGO Island (unknown release)");
}

// The game's code without its world database (a copy of the CD's root,
// say) isn't the game.
void testCodeAlone(const std::filesystem::path &tmp) {
    touch(tmp / "LEGO1.DLL");
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("legoisland.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"code alone", [](const std::filesystem::path &tmp) { testCodeAlone(tmp); }},
           });
