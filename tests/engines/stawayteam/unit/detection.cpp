// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's unit tests: detection.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine && engine->detect(tmp).empty());
}

// A release not in the table is found by its files' names alone, as an
// unknown release.
void testUnknownRelease(const std::filesystem::path &tmp) {
    for (const char *name : {"Star Trek.exe", "DATA.dat"})
        std::ofstream(tmp / name) << "x";
    const MetaEngine *engine = findEngine("stawayteam");
    const auto found = engine ? engine->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1);
    CHECK(!found.empty() && found[0].gameId == "stawayteam" &&
          found[0].description == "Star Trek: Away Team (unknown release)");
}

// The program alone isn't the game: its data is all in DATA.dat.
void testProgramAlone(const std::filesystem::path &tmp) {
    std::ofstream(tmp / "Star Trek.exe") << "x";
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"program alone", [](const std::filesystem::path &tmp) { testProgramAlone(tmp); }},
           });
