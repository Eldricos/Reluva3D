// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva {

const std::vector<const MetaEngine *> &allEngines();
const MetaEngine *findEngine(std::string_view id);

// Ask every engine whether it recognises the game in `dir`.
std::vector<DetectedGame> detectGames(const std::filesystem::path &dir);
// Where in `folder` a game is when it isn't there itself: the first folder
// below it (two levels at most, in name order) a game is found in, as an
// install's folder holds Jedi Outcast's in GameData; empty if none.
std::filesystem::path gameFolderBelow(const std::filesystem::path &folder);
// Ask every engine what it could import from `source`.
std::vector<ImportPlan> planImports(const std::filesystem::path &source);

} // namespace reluva
