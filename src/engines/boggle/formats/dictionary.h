// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <string_view>

namespace reluva::boggle {
class Dictionary {
public:
    // Decodes the binary .dic tree into an independent sorted word set.
    // Serialized bytes are preserved; callers choose input normalization.
    // Empty/user text dictionaries are a separate format, not accepted here.
    Error read(const std::vector<uint8_t>& bytes);
    bool contains(std::string_view word) const;
    bool hasPrefix(std::string_view prefix) const;
    const std::vector<std::string>& words() const { return words_; }
    size_t entries() const { return entries_; } // includes retail duplicate entries
private:
    std::vector<std::string> words_;
    size_t entries_ = 0;
};
} // namespace reluva::boggle
