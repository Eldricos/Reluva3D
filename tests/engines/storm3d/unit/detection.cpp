// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include <fstream>

#include "tests/support/test.h"

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("storm3d");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("storm3d");
    CHECK(engine && engine->detect(tmp).empty());
}

// A release not in the table is found by its files' names alone, as an
// unknown release.
void testUnknownRelease(const std::filesystem::path &tmp) {
    for (const char *name : {"Armada.exe", "assets.zfs", "objtxt.zfs"})
        std::ofstream(tmp / name) << "x";
    const MetaEngine *engine = findEngine("storm3d");
    const auto found = engine ? engine->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1);
    CHECK(!found.empty() && found[0].gameId == "armada" &&
          found[0].description == "Star Trek: Armada (unknown release)");
}

// The executable alone isn't enough: the archives are needed too.
void testExecutableAlone(const std::filesystem::path &tmp) {
    std::ofstream(tmp / "Armada.exe") << "x";
    const MetaEngine *engine = findEngine("storm3d");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("storm3d.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"executable alone", [](const std::filesystem::path &tmp) { testExecutableAlone(tmp); }},
           });
