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
 * build list (`AI/AIPs/<race>_build_list.aip`). See
 * docs/engines/storm3d/games/armada/systems/computer-player.md.
 */
class ComputerPlayer {
public:
    /** An entry of a build list (aipdef.h's Better_Build_List_Element). */
    struct Element {
        std::string type; // a definition: "fmining"
        int count = 0;    // how many more of it
        int base = 0;     // for a station, which of its kind it's by: a mining station's moon, the nearest 1
    };
    /** The build list's file for a race's definition ("fed": federation_build_list.aip). */
    static std::string buildListFileOf(const std::string &race);
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
