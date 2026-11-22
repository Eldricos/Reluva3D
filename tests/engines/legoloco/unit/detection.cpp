// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("legoloco");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("legoloco");
    CHECK(engine && engine->detect(tmp).empty());
}

// The release's files by name but with other contents (a later patch, say)
// aren't taken for it.
void testOtherContents(const std::filesystem::path &tmp) {
    std::filesystem::create_directories(tmp / "Exe");
    std::filesystem::create_directories(tmp / "art-res");
    for (const char *name : {"Exe/Loco.exe", "art-res/resource.RFH"})
        std::ofstream(tmp / name) << "x";
    const MetaEngine *engine = findEngine("legoloco");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"other contents", [](const std::filesystem::path &tmp) { testOtherContents(tmp); }},
           });
