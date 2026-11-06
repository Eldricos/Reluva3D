// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stharbinger {

// One folder holding a part of the game: a CD (or a copy of one), the DS9
// folder of one, or a folder the game was installed or extracted to.
struct GamePart {
    std::filesystem::path folder;
    bool scripts = false; // the scene scripts: CC.ALL, or disc 1's SSE/SSE.ALL
    bool data = false;    // DS9's ANIMS or PALETTES: backgrounds, animations, sounds
    bool exe = false;     // DEEP.EXE (where the credits are)
    int disc = -1;        // 0 or 1 (DS9/TORPEDOS/CD), -1 if it's no CD
};

// The parts of the game in `dir`: `dir` itself, or else each of its
// folders that is one (copies of both CDs side by side, say), in name order;
// the first takes precedence. None if there's no part of the game.
std::vector<GamePart> findGameParts(const std::filesystem::path& dir);

// Enough to play: the scene scripts and the data, from any of the parts.
bool playable(const std::vector<GamePart>& parts);

// The release, from a table of the releases' file signatures: each
// entry's files are looked for in one part's folder (a CD as it is, or an
// install), so a CD is recognised wherever it is (alone, beside the other,
// or in Reluva's import as disc1/). The part with the scene scripts is
// tried first, then the others in order; none if no part matches an entry.
std::optional<DetectedGame> recogniseRelease(const std::string& engineId, const std::vector<GamePart>& parts,
                                             const std::vector<GameVariant>& table);

} // namespace reluva::stharbinger
