// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::age {

// Add an install's files to the search set: every .ar archive in the game
// folder (later names, compared in lower case, before earlier ones), with
// the folder's loose files above them. See docs/formats/age/ar.md for what
// is and isn't known about the original's order.
//
// An archive that can't be opened is skipped and reported in `warnings`;
// a folder without any archive is an error.
Error mountGame(SearchSet& set, const std::filesystem::path& install, std::vector<std::string>& warnings);

} // namespace reluva::age
