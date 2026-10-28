// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <map>
#include <string>

namespace reluva::age {

// The game's strings: mmlang.dll's string tables (its only resources),
// by id. See docs/formats/age/mmlang.md.
class GameStrings {
public:
    Error load(const SearchSet& files);
    bool loaded() const { return !strings_.empty(); }
    // The string, or `fallback` if there's none.
    std::string get(uint32_t id, const std::string& fallback = {}) const;

private:
    std::map<uint32_t, std::string> strings_;
};

namespace strings {
// The Crash Course lessons' names, one after another from these, in the
// order of race/<city>/crash<n>data.csv: London's, then San Francisco's.
constexpr uint32_t kLondonLessons = 532, kSfLessons = 545, kLessonCount = 13;
} // namespace strings

} // namespace reluva::age
