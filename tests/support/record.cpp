// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/support/record.h"

#include "tests/support/test.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace reluva::test {

Hash& Hash::bytes(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h_ ^= p[i];
        h_ *= 0x100000001b3ull;
    }
    return *this;
}

std::string Hash::hex() const {
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h_));
    return out;
}

namespace {
// A record file's lines, without its comments.
std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line[0] != '#')
            out.push_back(line);
    return out;
}
} // namespace

void Record::check(const std::filesystem::path& path, const std::string& suite, const std::string& source) const {
    for (const std::string& line : lines_)
        if (line.find(": FAILED ") != std::string::npos)
            test::fail(line);

    if (option("update")) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << "# The record of " << suite << " (" << source << ").\n"
            << "# Written by `reluva-tests " << suite << " --update`; review its diff before committing.\n";
        for (const std::string& line : lines_)
            out << line << "\n";
        std::printf("Record written to %s (%zu lines)\n", path.string().c_str(), lines_.size());
        return;
    }

    const std::vector<std::string> want = readLines(path);
    if (want.empty())
        return test::fail("no record at " + path.string() + ": make one with reluva-tests " + suite + " --update");
    int differ = 0;
    const size_t n = std::max(want.size(), lines_.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string got = i < lines_.size() ? lines_[i] : "(nothing)";
        const std::string expected = i < want.size() ? want[i] : "(nothing)";
        if (got == expected) {
            std::printf("  ok    %s\n", got.c_str());
            continue;
        }
        ++differ;
        std::printf("  DIFF  %s\n        was %s\n", got.c_str(), expected.c_str());
    }
    if (differ)
        test::fail(std::to_string(differ) + " line(s) differ from " + path.string() + ". If the change is meant, run " +
             "reluva-tests " + suite + " --update and review the record's diff.");
}

} // namespace reluva::test
