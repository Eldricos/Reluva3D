// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <filesystem>
#include <set>
#include <string_view>

namespace reluva::boggle {
// Native, per-target words. Never writes to the original game's dictionary.
class UserDictionary {
public:
    Error load(const std::filesystem::path& savePath, const std::filesystem::path& gamePath = {});
    Error add(const std::vector<std::string>& words); // persists before changing memory
    bool contains(std::string_view word) const { return words_.contains(std::string(word)); }
    const std::set<std::string>& words() const { return words_; }
private:
    std::filesystem::path file_;
    std::set<std::string> words_;
};
} // namespace reluva::boggle
