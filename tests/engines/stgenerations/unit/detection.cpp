// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations's unit tests: detection. Add a file (and its topic, in the
// engine's CMakeLists.txt) for each part of the engine as it grows.

#include "engines/registry.h"

#include "tests/support/test.h"

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine* engine = findEngine("stgenerations");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path& tmp) {
    const MetaEngine* engine = findEngine("stgenerations");
    CHECK(engine && engine->detect(tmp).empty());
}

} // namespace

static const reluva::test::Suite suite("stgenerations.unit.detection", {
    {"registered", [](const std::filesystem::path&) { testRegistered(); }},
    {"empty folder", [](const std::filesystem::path& tmp) { testEmptyFolder(tmp); }},
});
