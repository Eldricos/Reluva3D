// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::sith {

/**
 * A .pup puppet: for each of a character's modes (unarmed, aiming, saber,
 * swimming...), the animation each of its actions plays, and which of its
 * model's nodes are its head, torso and weapons
 * (docs/engines/sith/games/jk/formats/pup.md).
 */
struct Puppet {
    /** An action's animation. */
    struct Action {
        /** What the character does: "stand", "walk", "fire"... */
        std::string name;
        std::string keyframe;
        uint32_t flags = 0;
        /** The two priorities the file's header calls lopri and hipri. */
        ///@{
        int lowPriority = 0;
        int highPriority = 0;
        ///@}
    };

    /** A mode's actions; those it lacks are its base mode's. */
    struct Mode {
        int number = 0;
        /** The mode it's based on; -1 for none. */
        int basedOn = -1;
        std::vector<Action> actions;
    };

    std::vector<Mode> modes;
    /** The joints section: each joint's number (0 head, 1 neck, 2 torso...) and its model node. */
    std::map<int, int> joints;

    /** The action in mode `mode`, or in the modes it's based on; nullptr if none has it. */
    const Action *find(int mode, std::string_view action) const;
};

/** Reads a .pup file; fails, saying where, if a line can't be read. */
Error parsePuppet(const std::vector<uint8_t> &data, Puppet &out);

} // namespace reluva::sith
