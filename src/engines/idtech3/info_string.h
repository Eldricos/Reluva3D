// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

/**
 * A key's value in an info string, as id Tech 3 passes settings around
 * ("\key\value\key\value"), or empty if it isn't there. Keys are matched
 * without regard to case, as the original matches them.
 */
std::string infoValue(const std::string &info, const std::string &key);
/**
 * Sets `key`: an entry it already has is removed and the new one added at
 * the end; an empty value only removes it. Backslashes, quotes and
 * semicolons are left out of both, as the original refuses them.
 */
void setInfoValue(std::string &info, const std::string &key, const std::string &value);
/** An info string's keys and values, in order. */
std::vector<std::pair<std::string, std::string>> infoPairs(const std::string &info);

} // namespace reluva::idtech3
