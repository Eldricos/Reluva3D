// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's test framework, shared by every test in reluva-tests.
//
// Tests come in suites named <area>.<kind>.<topic> ("idtech3.unit.collision",
// "common.unit.audio") or <area>.<kind> ("idtech3.engine"). A test file
// registers its suite with its cases, each a function given a folder of
// its own to work in:
//
//     static const reluva::test::Suite suite("idtech3.unit.collision", {
//         {"traces", [](const std::filesystem::path&) { testCollision(); }},
//         {"server world", [](const std::filesystem::path& tmp) { testServerWorld(tmp); }},
//     });
//
// CHECK(condition) counts a failure (and says where) without stopping the
// case. `reluva-tests [name...]` runs the suites whose names are given,
// or start with one (a whole area: `reluva-tests idtech3`); --list lists
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

using Case = std::pair<std::string, std::function<void(const std::filesystem::path& tmp)>>;

struct SuiteInfo {
    std::string name;
    std::vector<Case> cases;
};

// Every suite registered, in no particular order.
std::vector<SuiteInfo>& suites();

// Registers a suite (as a static object in its test file).
struct Suite {
    Suite(std::string name, std::vector<Case> cases) { suites().push_back({std::move(name), std::move(cases)}); }
};

// Failures counted so far by CHECK and fail().
int& failures();
// A failure with its own message.
void fail(const std::string& message);
// Whether an option was given on the command line (option("update") for
// --update).
bool option(const std::string& name);

} // namespace reluva::test

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);       \
            ++::reluva::test::failures();                                                       \
        }                                                                                       \
    } while (0)
