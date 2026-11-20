// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's test framework, shared by every test in reluva-tests.
//
// Tests come in suites named <area>.<kind>.<topic> ("idtech3.unit.collision",
// "common.unit.audio") or <area>.<kind> ("idtech3.engine"). A test file
// registers its suite with its cases, each a function given a folder of
// its own to work in:
//
//     static const reluva::test::Suite kSuite("idtech3.unit.collision", {
//         {"traces", [](const std::filesystem::path&) { testCollision(); }},
//         {"server world", [](const std::filesystem::path& tmp) { testServerWorld(tmp); }},
//     });
//
// CHECK(condition) counts a failure (and says where) without stopping the
// case. `reluva-tests [name...]` runs the suites whose names are given,
// or are under one (a whole area: `reluva-tests idtech3`); --list lists
// them; options after the names (--update) are for the tests to read.
// Each engine's CMakeLists.txt names its suites for ctest
// (reluva_add_engine_tests, cmake/ReluvaEngines.cmake).

#pragma once

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace reluva::test {

/**
 * A test case: its name, and the function that runs it. The function is
 * given an empty folder of its own, deleted again after it returns.
 */
using Case = std::pair<std::string, std::function<void(const std::filesystem::path &tmp)>>;

/** A registered suite: its name and its cases, run in this order. */
struct SuiteInfo {
    std::string name;
    std::vector<Case> cases;
};

/** Every suite registered, in no particular order (main() sorts them by name). */
std::vector<SuiteInfo> &suites();

/** Registers a suite (as a static object in its test file). */
struct Suite {
    /** Registers the suite with its cases, which run in this order. */
    Suite(std::string name, std::vector<Case> cases) { suites().push_back({std::move(name), std::move(cases)}); }
};

/** Failures counted so far by CHECK and fail(), over every suite run. */
int &failures();
/** Counts a failure, printing its own message. */
void fail(const std::string &message);
/** Whether an option was given on the command line (option("update") for --update). */
bool option(const std::string &name);

} // namespace reluva::test

/** Counts a failure, printing the file, line and condition, if `cond` is false; the case goes on. */
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                              \
            ++::reluva::test::failures();                                                                              \
        }                                                                                                              \
    } while (0)
