// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A saved game's file (`save\s<n>.sav`). Its head is the program's own,
 * in text: its version and its description, 128 bytes in hex (0x543be0),
 * which is all the saved games' lists read of one (0x543ab0); after that,
 * Reluva's own: which campaign's mission it is, and the mission as
 * MissionView::save() writes it. See
 * docs/engines/storm3d/games/armada/formats/saved-game.md.
 */
struct SavedGame {
    /** The description's room in the file, its NUL included. */
    static constexpr size_t kDescriptionBytes = 128;

    std::string description;
    int campaign = 0; // as the single-player screen numbers them, the finale 4
    int mission = 0;  // its place in the campaign
    std::string map;  // its map ("fed1.bzn")
    std::vector<uint8_t> state;

    /** The file as Reluva writes it. */
    std::vector<uint8_t> bytes() const;
    /**
     * Reads a file Reluva wrote.
     * @return a failure for one it can't read: the program's own among them
     */
    Error parse(const std::vector<uint8_t> &file);
    /** What a list shows of a file, the program's own or Reluva's: its description; none if it isn't a save. */
    static std::optional<std::string> descriptionOf(const std::vector<uint8_t> &file);
    /** The name of a new save among those there: s<n>.sav, n one past the highest (0x53ea20). */
    static std::string nextName(const std::vector<std::string> &names);
};

} // namespace reluva::storm3d
