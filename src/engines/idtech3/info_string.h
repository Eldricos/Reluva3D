// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// Info strings, as id Tech 3 passes settings around: "\key\value\key\value".
// Keys are matched without regard to case, as the original matches them.
std::string infoValue(const std::string& info, const std::string& key);
// Sets `key` (replacing it if there, else adding it at the end); an empty
// value removes it. Backslashes, quotes and semicolons are left out of
// both, as the original refuses them.
void setInfoValue(std::string& info, const std::string& key, const std::string& value);
std::vector<std::pair<std::string, std::string>> infoPairs(const std::string& info);

} // namespace reluva::idtech3
