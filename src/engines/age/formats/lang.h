// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <map>
#include <string>

namespace reluva::age {

/**
 * The game's strings: mmlang.dll's string tables (its only resources),
 * by id. See docs/engines/age/games/mm2/formats/mmlang.md.
 */
class GameStrings {
public:
    /** Reads mmlang.dll's string tables, forgetting any read before. */
    Error load(const SearchSet &files);
    /** Whether load() found any strings. */
    bool loaded() const { return !_strings.empty(); }
    /** The string, or `fallback` if there's none. */
    std::string get(uint32_t id, const std::string &fallback = {}) const;

private:
    std::map<uint32_t, std::string> _strings;
};

namespace strings {
/**
 * The Crash Course lessons' names, one after another from these, in the
 * order of race/<city>/crash<n>data.csv: London's, then San Francisco's.
 */
constexpr uint32_t kLondonLessons = 532;
constexpr uint32_t kSfLessons = 545;
constexpr uint32_t kLessonCount = 13;
} // namespace strings

} // namespace reluva::age
