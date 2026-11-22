// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * Jedi Knight from its CD 1 (or a copy of it): the files its installer
 * copies, in the install's names, and CD 1's cut scenes and marker in
 * `cd1/` (docs/engines/sith/games/jk/README.md). Empty if `source` isn't
 * CD 1.
 */
std::vector<ImportPlan> planImport(const std::filesystem::path &source);

/**
 * Adds CD 2's cut scenes and marker, in `cd2/`, to a plan made from CD 1.
 * @return False, with why in `message`, if `folder` isn't CD 2 or it's
 * added already.
 */
bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message);

} // namespace reluva::sith
