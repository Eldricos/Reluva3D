// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World's unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("tpw");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("tpw");
    CHECK(engine && engine->detect(tmp).empty());
}

void touch(const std::filesystem::path &file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

// A release not in the table, its files there by name (in any case), is
// shown as an unknown release of SimCoaster.
void testUnknownRelease(const std::filesystem::path &tmp) {
    touch(tmp / "DATA" / "UI.WAD");
    touch(tmp / "DATA" / "challenges.SAM");
    touch(tmp / "game.exe");
    const MetaEngine *engine = findEngine("tpw");
    if (!engine)
        return test::fail("tpw isn't registered");
    const auto found = engine->detect(tmp);
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        CHECK(found[0].gameId == "simcoaster");
        CHECK(found[0].description == "SimCoaster (unknown release)");
    }
}

// Game.exe and ui.wad alone aren't enough: other Bullfrog games have both.
void testMissingChallenges(const std::filesystem::path &tmp) {
    touch(tmp / "Data" / "ui.wad");
    touch(tmp / "Game.exe");
    const MetaEngine *engine = findEngine("tpw");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"missing challenges", [](const std::filesystem::path &tmp) { testMissingChallenges(tmp); }},
           });
