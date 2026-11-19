// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-viewer's Tests window: reading the build's list of tests, planning
// ctest runs for the games picked, and reading ctest's progress
// (src/devtools/test_runs.h).

#include "devtools/test_runs.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>

using namespace reluva::devtools;

namespace {

bool has(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// As cmake/ReluvaEngines.cmake writes it (labels end with an empty one).
const char *const kList = "awe.unit.formats\tawe,unit,\n"
                          "tests.coverage\t\n"
                          "awe.gamedata.release\tgamedata,awe\tawe\tmotoe\tRELUVA_MOTOE\n"
                          "awe.gamedata.route\tgamedata,awe\tawe\tmotoe\tRELUVA_MOTOE\n"
                          "clue.gamedata.files\tgamedata,clue\tclue\tclue\tRELUVA_CLUE\r\n"
                          "\n";

void testList() {
    const std::vector<TestEntry> all = readTestList(kList);
    CHECK(all.size() == 5);
    CHECK((all[0].name == "awe.unit.formats" && all[0].labels == std::vector<std::string>{"awe", "unit"}));
    CHECK(!all[0].needsGame() && all[0].area() == "awe");
    CHECK(all[1].name == "tests.coverage" && all[1].labels.empty() && all[1].area() == "tests");
    CHECK(all[2].needsGame() && all[2].engine == "awe" && all[2].gameId == "motoe" &&
          all[2].variable == "RELUVA_MOTOE");
    CHECK(all[4].variable == "RELUVA_CLUE"); // a Windows line end taken off
}

void testPlan() {
    const std::vector<TestEntry> all = readTestList(kList);
    const std::vector<std::string> picked = {"awe.unit.formats", "awe.gamedata.release", "awe.gamedata.route",
                                             "clue.gamedata.files"};

    // Two releases of one game: a run each, as one variable holds one folder.
    std::vector<TestGame> games = {{"motoe", "awe", "motoe", "/g/motoe"}, {"motoe-2", "awe", "motoe", "/g/motoe2"}};
    std::vector<CtestRun> runs = planRuns(all, picked, games, Records::kCommitted, "");
    CHECK(runs.size() == 2);
    CHECK(runs[0].tests.size() == 4 && has(runs[0].tests, "awe.unit.formats"));
    CHECK(runs[0].set.at("RELUVA_MOTOE") == "/g/motoe");
    // Clue's step, with no game picked, runs with its variable cleared, so
    // it finds the game in the launcher's list or is skipped.
    CHECK(has(runs[0].tests, "clue.gamedata.files") && has(runs[0].unset, "RELUVA_CLUE"));
    CHECK(has(runs[0].unset, "RELUVA_GAMEDATA_UPDATE") && has(runs[0].unset, "RELUVA_GAMEDATA_TARGET"));
    CHECK((runs[1].tests == std::vector<std::string>{"awe.gamedata.release", "awe.gamedata.route"}));
    CHECK(runs[1].set.at("RELUVA_MOTOE") == "/g/motoe2" && runs[1].on.at("awe.gamedata.route") == "motoe-2");
    CHECK(runs[0].on.at("awe.gamedata.route") == "motoe" && runs[0].on.count("clue.gamedata.files") == 0);

    // A baseline of one's own: made, then compared with.
    games.pop_back();
    runs = planRuns(all, {"awe.gamedata.route"}, games, Records::kMakeMine, "/r");
    CHECK(runs.size() == 1 && runs[0].set.at("RELUVA_GAMEDATA_RECORDS_ROOT") == "/r" &&
          runs[0].set.at("RELUVA_GAMEDATA_UPDATE") == "1" && has(runs[0].unset, "RELUVA_GAMEDATA_RECORDS"));
    runs = planRuns(all, {"awe.gamedata.route"}, games, Records::kCompareMine, "/r");
    CHECK(runs[0].set.count("RELUVA_GAMEDATA_UPDATE") == 0 && has(runs[0].unset, "RELUVA_GAMEDATA_UPDATE"));
    runs = planRuns(all, {"awe.gamedata.route"}, games, Records::kUpdateCommitted, "/r");
    CHECK(runs[0].set.at("RELUVA_GAMEDATA_UPDATE") == "1" && has(runs[0].unset, "RELUVA_GAMEDATA_RECORDS_ROOT"));

    // Names not in the list are left out; nothing picked, no runs.
    CHECK(planRuns(all, {"nope"}, games, Records::kCommitted, "").empty());
    CHECK(exactPattern({"awe.unit.formats", "a+b"}) == "^(awe\\.unit\\.formats|a\\+b)$");
}

void testCtestLines() {
    std::optional<CtestProgress> p = readCtestLine("    Start  75: awe.gamedata.release");
    CHECK(p && p->name == "awe.gamedata.release" && p->state == TestState::kRunning);
    p = readCtestLine("  1/118 Test   #1: awe.unit.formats .................   Passed    0.05 sec");
    CHECK(p && p->name == "awe.unit.formats" && p->state == TestState::kPassed && p->seconds == 0.05 && p->why.empty());
    p = readCtestLine("1/1 Test #75: awe.gamedata.release .............***Failed    0.38 sec");
    CHECK(p && p->state == TestState::kFailed && p->why == "Failed");
    p = readCtestLine("72/118 Test  #75: awe.gamedata.release ...................***Skipped   0.01 sec");
    CHECK(p && p->state == TestState::kSkipped);
    p = readCtestLine("3/9 Test  #3: x.y ....***Timeout 3600.02 sec");
    CHECK(p && p->state == TestState::kFailed && p->why == "Timeout" && p->seconds > 3600);
    p = readCtestLine("4/9 Test  #4: x.z ....***Exception: SegFault  0.10 sec");
    CHECK(p && p->state == TestState::kFailed && p->why == "Exception: SegFault");
    p = readCtestLine("5/9 Test  #5: x.w ....***Not Run   0.00 sec");
    CHECK(p && p->state == TestState::kFailed);
    p = readCtestLine("6/9 Test  #6: x.v ....***Not Run (Disabled)   0.00 sec");
    CHECK(p && p->state == TestState::kSkipped);
    CHECK(!readCtestLine("100% tests passed, 0 tests failed out of 118"));
    CHECK(!readCtestLine("75: RELUVA_MOTOE isn't set"));
}

} // namespace

static const reluva::test::Suite kSuite("devtools.unit.test_runs",
                                        {
                                            {"list", [](const std::filesystem::path &) { testList(); }},
                                            {"plan", [](const std::filesystem::path &) { testPlan(); }},
                                            {"ctest lines", [](const std::filesystem::path &) { testCtestLines(); }},
                                        });
