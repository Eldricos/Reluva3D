// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string_view>
#include <vector>

namespace reluva::nfs {

/**
 * The integers of a commented text file (NFS III's and NFS II SE's sky
 * files, NFS II SE's light files), in order, with its C comments left out:
 * a comment names the value after it, and may hold values of its own that
 * the game doesn't read.
 */
std::vector<int> commentedNumbers(std::string_view text);

} // namespace reluva::nfs
