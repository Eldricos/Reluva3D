// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tests: runs the suites the test files register (tests/support/test.h).
//
//   reluva-tests                      every suite
//   reluva-tests <name>...            those named, or whose names start with one
//                                     ("idtech3" runs all of id Tech 3's)
//   reluva-tests --list               the suites and their cases
//   reluva-tests --covered-by <name>... fails if a suite isn't under one of the
//                                     names (ctest's list, so none is missed)
//   ... --update                      options for the tests themselves

#include "tests/support/test.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>

namespace reluva::test {

std::vector<SuiteInfo>& suites() {
    static std::vector<SuiteInfo> all;
    return all;
}

int& failures() {
    static int count = 0;
    return count;
}

void fail(const std::string& message) {
    std::fprintf(stderr, "FAILED: %s\n", message.c_str());
    ++failures();
}

namespace {
std::set<std::string>& options() {
    static std::set<std::string> set;
    return set;
}

// Whether `name` is `filter`, or under it ("idtech3" covers
// "idtech3.unit.collision", not "idtech3x").
bool under(const std::string& name, const std::string& filter) {
    return name == filter || (name.size() > filter.size() && name.compare(0, filter.size(), filter) == 0 &&
                              name[filter.size()] == '.');
}
} // namespace

bool option(const std::string& name) { return options().count(name) > 0; }

} // namespace reluva::test

int main(int argc, char** argv) {
    using namespace reluva::test;
    std::vector<std::string> filters;
    bool list = false, coveredBy = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list")
            list = true;
        else if (a == "--covered-by")
            coveredBy = true;
        else if (a.rfind("--", 0) == 0)
            options().insert(a.substr(2));
        else
            filters.push_back(a);
    }
    auto& all = suites();
    std::sort(all.begin(), all.end(), [](const SuiteInfo& a, const SuiteInfo& b) { return a.name < b.name; });

    if (coveredBy) {
        int missing = 0;
        for (const SuiteInfo& s : all)
            if (std::none_of(filters.begin(), filters.end(), [&](const std::string& f) { return under(s.name, f); })) {
                std::fprintf(stderr, "The suite %s isn't run by ctest: add it to its CMakeLists.txt's TOPICS\n",
                             s.name.c_str());
                ++missing;
            }
        if (!missing)
            std::printf("Every suite is run by ctest (%zu)\n", all.size());
        return missing ? 1 : 0;
    }
    if (list) {
        for (const SuiteInfo& s : all) {
            std::printf("%s\n", s.name.c_str());
            for (const Case& c : s.cases)
                std::printf("    %s\n", c.first.c_str());
        }
        return 0;
    }

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "reluva_tests";
    int ran = 0;
    for (const SuiteInfo& s : all) {
        if (!filters.empty() &&
            std::none_of(filters.begin(), filters.end(), [&](const std::string& f) { return under(s.name, f); }))
            continue;
        const auto start = std::chrono::steady_clock::now();
        const int before = failures();
        for (const Case& c : s.cases) {
            std::filesystem::path tmp = root / s.name / c.first;
            std::error_code ec;
            std::filesystem::remove_all(tmp, ec);
            std::filesystem::create_directories(tmp, ec);
            const int caseBefore = failures();
            c.second(tmp);
            if (failures() != caseBefore)
                std::fprintf(stderr, "  in %s: %s\n", s.name.c_str(), c.first.c_str());
            std::filesystem::remove_all(tmp, ec);
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::printf("%s %s (%zu cases, %lld ms)\n", failures() == before ? "ok    " : "FAILED", s.name.c_str(),
                    s.cases.size(), static_cast<long long>(ms.count()));
        ++ran;
    }
    if (!ran) {
        std::fprintf(stderr, "No suite matches; reluva-tests --list lists them\n");
        return 1;
    }
    if (failures())
        std::fprintf(stderr, "%d check(s) failed\n", failures());
    else
        std::printf("All tests passed\n");
    return failures() ? 1 : 0;
}
