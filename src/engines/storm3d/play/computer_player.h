// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/config_file.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * What a skirmish's computer player builds, and in what order: its race's
 * build list (`AI/AIPs/<race>_build_list.aip`); and how a Medium or Hard
 * one turns defensive or offensive as its strength against the others'
 * changes, which the program does itself, the skirmish's AI DLLs that
 * RTS_CFG.h names not being shipped. See
 * docs/engines/storm3d/games/armada/systems/computer-player.md.
 */
class ComputerPlayer {
public:
    /** How it plays: each a personality and a build list (0x4307f0's 0, 1 and 2). */
    enum class Stance { kDefensive, kNormal, kOffensive };
    /** How often a Medium or Hard one weighs its stance: each 200 of the game's updates (0x430600). */
    static constexpr int kStanceUpdates = 200;
    /** The updates a second, as measured in the original's skirmish. */
    static constexpr double kUpdatesPerSecond = 86;

    /** An entry of a build list (aipdef.h's Better_Build_List_Element). */
    struct Element {
        std::string type; // a definition: "fmining"
        int count = 0;    // how many more of it
        int base = 0;     // for a station, which of its kind it's by: a mining station's moon, the nearest 1
    };
    /**
     * The build list's file for a race's definition and a stance ("fed":
     * federation_build_list.aip, federation_defensive_list.aip,
     * federation_offensive_list.aip).
     */
    static std::string buildListFileOf(const std::string &race, Stance stance = Stance::kNormal);
    /** The personality a stance loads: newdefault.aip, default_defensive.aip or default_offensive.aip. */
    static std::string personalityFileOf(Stance stance);
    /**
     * The stance for a player's strength, all its units' maxHealth, against
     * the average of every player's in the game that has any: offensive
     * from 1.1 times it, defensive at 0.7 times it or less (0x4307f0).
     */
    static Stance stanceFor(float strength, float average);
    /** A build list read through `loader`: its the_build_list; empty if it can't be read. */
    static std::vector<Element> buildListOf(const std::string &file, const ConfigFile::Loader &loader);
    /**
     * The first entry not yet met: one whose type the player has fewer of
     * (`owned`, built or on order) than that entry and those before it of
     * the same type ask for together; none when all are.
     */
    static std::optional<size_t> nextOf(const std::vector<Element> &list, const std::map<std::string, int> &owned);
};

} // namespace reluva::storm3d
