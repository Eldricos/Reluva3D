// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// For engine tests: a run written as a record of "step: value" lines,
// compared with the one committed (or, with --update, written as the new
// one). See docs/DEVELOPMENT.md (Engine tests).
//
//     test::Record rec;
//     rec.add("maps.read", count);
//     rec.add("map.state", test::Hash().text(name).bytes(data).hex());
//     rec.check(RELUVA_SOURCE_DIR "/tests/engines/<engine>/data/engine-record.txt",
//               "<engine>.engine", "tests/engines/<engine>/engine.cpp");

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::test {

// FNV-1a, 64 bits, over everything added: a checksum that's the same on
// every machine.
class Hash {
public:
    Hash& bytes(const void* data, size_t size);
    Hash& bytes(const std::vector<uint8_t>& v) { return bytes(v.data(), v.size()); }
    Hash& text(const std::string& s) { return bytes(s.data(), s.size() + 1); }
    Hash& i32(int32_t v) { return bytes(&v, 4); }
    Hash& f32(float v) { return bytes(&v, 4); }
    std::string hex() const;

private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

// The run's record: "step: value" lines, in order.
class Record {
public:
    void add(const std::string& step, const std::string& value) { lines_.push_back(step + ": " + value); }
    void add(const std::string& step, int64_t value) { add(step, std::to_string(value)); }
    const std::vector<std::string>& lines() const { return lines_; }
    // A step that couldn't be done at all: a line saying so, and a failure.
    void fail(const std::string& step, const std::string& why) { add(step, "FAILED " + why); }

    // Fails the test for each failed step, then compares the record with
    // the one at `path`, printing every line and the old value of each
    // that changed. With --update it writes the record there instead.
    // `suite` and `source` name the test in the file's header and messages.
    void check(const std::filesystem::path& path, const std::string& suite, const std::string& source) const;

private:
    std::vector<std::string> lines_;
};

} // namespace reluva::test
