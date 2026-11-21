// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <filesystem>
#include <string>

// What the load and save screens show of a saved game (game0.sav to
// game11.sav, one per slot): its name, from the archive's header, and its
// description, turn and player's empire, from its gameInfo file
// (docs/engines/botf/formats/sav.md).

namespace reluva::botf {

/** A saved game, as its slot shows it. */
struct SaveSummary {
    std::string name;        // up to 15 characters, UTF-8
    std::string description; // up to 50 characters, UTF-8
    int turn = 0;            // 0 is the first
    int empire = -1;         // the player's, 0 Cardassians to 4 Romulans; -1 not known
    bool multiplayer = false;
};

/** Reads `summary` from the saved game at `path`. */
Error readSaveSummary(const std::filesystem::path &path, SaveSummary &summary);

} // namespace reluva::botf
