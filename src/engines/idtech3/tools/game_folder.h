// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A game folder given to reluva-tool's id Tech 3 commands (".../BaseEF"),
// mounted as the engine mounts it.

#pragma once

#include "common/archive.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3::tool {

// The game folder's install (the folder above it) and its own name, for
// the mods list ("$modlist").
std::pair<std::filesystem::path, std::string> idTech3Folders(const char* folder);

// Mounts the game folder, or prints why it can't. `skip`: paks left out
// (lower case), as the launcher leaves out the Expansion Pack's when it's
// turned off.
bool mountIdTech3(const char* folder, reluva::SearchSet& set, const std::vector<std::string>& skip = {});

} // namespace reluva::idtech3::tool
