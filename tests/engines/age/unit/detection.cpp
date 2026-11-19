// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: finding the game.
// Every file is built in the test.

#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testDetection(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "MM2CORE.AR", buildAr({}));
    CHECK(detectFromTable("age", detectionTable(), tmp).empty()); // not the retail file
    CHECK(detectFromTable("age", fallbackTable(), tmp).empty());  // no program
    test::writeFile(tmp / "MIDTOWN2.EXE", toBytes("MZ"));
    const auto found = detectFromTable("age", fallbackTable(), tmp);
    CHECK(found.size() == 1 && found[0].gameId == "mm2");
    CHECK(findGame("mm2") && !findGame("mm1"));
}

} // namespace

static const reluva::test::Suite kSuite("age.unit.detection",
    {
        {"detection", [](const std::filesystem::path &tmp) { testDetection(tmp); }},
    });
