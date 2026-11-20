// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva {

/** Every engine built, the testbed first. */
const std::vector<const MetaEngine *> &allEngines();
/** The engine with this id (MetaEngine::id), or nullptr if none is built. */
const MetaEngine *findEngine(std::string_view id);

/** What every engine recognises in `dir` (MetaEngine::detect), together. */
std::vector<DetectedGame> detectGames(const std::filesystem::path &dir);
/** What every engine could import from `source` (MetaEngine::planImport), together. */
std::vector<ImportPlan> planImports(const std::filesystem::path &source);

} // namespace reluva
