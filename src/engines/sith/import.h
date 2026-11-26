// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A game from its CD 1, a copy of it, or its image (a cue sheet or an .iso
 * in the folder `source` names): the files its installer copies, in the
 * install's names, and CD 1's cut scenes and marker in `cd1/`, its music
 * beside them (docs/engines/sith/games/jk/README.md,
 * docs/engines/sith/games/mots/README.md). Empty if `source` isn't a
 * game's CD 1.
 */
std::vector<ImportPlan> planImport(const std::filesystem::path &source);

/**
 * Adds the game's CD 2, from the CD, a copy or its image, in `cd2/`, to a
 * plan made from CD 1: Jedi Knight's cut scenes, marker and music; for
 * Mysteries of the Sith, Jedi Knight's CD 2's marker, which it asks for
 * once.
 * @return False, with why in `message`, if `folder` isn't the game's CD 2
 * or it's added already.
 */
bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message);

} // namespace reluva::sith
