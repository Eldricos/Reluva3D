// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/attractions.h"

#include <array>
#include <string>

namespace reluva::tpw {

/**
 * The names the game shows for its attractions, in the Buy window and on
 * their signs: pairs of `OBJECT_NAMES`, a first word and the rest. See
 * docs/engines/tpw/games/simcoaster/systems/building.md#their-names.
 */
class ObjectNames {
public:
    /** Reads OBJECT_NAMES and OLD_OBJECT_NAMES from the language's folder (`Language/American/`). */
    Error load(const SearchSet &files, const std::string &folder, const CharacterSet &characters);

    /**
     * An attraction's name as two lines, as its sign has it: "Dizzy",
     * "Droid". One without a pair keeps its settings' name, its first
     * word on the first line.
     */
    std::array<std::string, 2> lines(const Attractions::Attraction &a) const;
    /** Its name on one line: "Dizzy Droid". */
    std::string shown(const Attractions::Attraction &a) const;

private:
    StringTable _names;
    StringTable _oldNames;
};

} // namespace reluva::tpw
