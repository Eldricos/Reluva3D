// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/attractions.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The names the game shows for its attractions, in the Buy window and on
 * their signs, from `OBJECT_NAMES`, and their descriptions, from
 * `OBJECT_DESC`. See
 * docs/engines/tpw/games/simcoaster/systems/building.md#their-names.
 */
class ObjectNames {
public:
    /** Reads OBJECT_NAMES, OLD_OBJECT_NAMES and OBJECT_DESC from the language's folder (`Language/American/`). */
    Error load(const SearchSet &files, const std::string &folder, const CharacterSet &characters);
    /** Takes the three tables' strings as they are, in their order, as tests give them. */
    void set(std::vector<std::string> names, std::vector<std::string> oldNames, std::vector<std::string> descriptions);

    /**
     * An attraction's name as two lines, as its sign has it: "Dizzy",
     * "Droid". One whose old name isn't found keeps its settings' name;
     * a name in one string has its first word on the first line.
     */
    std::array<std::string, 2> lines(const Attractions::Attraction &a) const;
    /** Its name on one line: "Dizzy Droid". */
    std::string shown(const Attractions::Attraction &a) const;
    /**
     * What the Buy window says of it, a line of OBJECT_DESC; empty for one
     * the game's strings have none for, as most rides.
     */
    std::string description(const Attractions::Attraction &a) const;

private:
    std::optional<std::array<std::string, 2>> find(const Attractions::Attraction &a, const std::string &own,
                                                   size_t begin, size_t end) const;

    std::vector<std::string> _names;
    std::vector<std::string> _oldNames;
    std::vector<std::string> _descriptions;
};

} // namespace reluva::tpw
