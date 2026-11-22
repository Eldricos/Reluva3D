// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * An episode.jk file: an episode's title and kind, and its steps, the
 * levels and cut scenes in the order they're played
 * (docs/engines/sith/games/jk/formats/episode-jk.md).
 */
struct Episode {
    /** One step of the episode. */
    struct Step {
        /** Its number, which the steps' gotos name. */
        int line = 0;
        /** The CD it's on: 1 or 2, or 0 for either. */
        int cd = 0;
        /** The level number the player is shown. */
        int level = 0;
        /** CUT (a cut scene), LEVEL or DECIDE (the dark or light side's choice), as written. */
        std::string kind;
        /** The cut scene's .smk or the level's .jkl. */
        std::string file;
        /** The Force powers' inventory bins the step gives, light and dark; 0 for none. */
        ///@{
        int lightPower = 0;
        int darkPower = 0;
        ///@}
        /** The step that follows, and the other one a level or a choice may go to; -1 for none. */
        ///@{
        int gotoA = -1;
        int gotoB = -1;
        ///@}
    };

    std::string title;
    /** 1 single player, 2 deathmatch, 8 Capture the Flag. */
    int type = 0;
    /** The count on its SEQ line: the number of steps in every retail episode. */
    int declaredSteps = 0;
    std::vector<Step> steps;

    /** The step numbered `line`, or nullptr. */
    const Step *find(int line) const;
};

/** Reads an episode.jk file; fails if a step's line can't be read. */
Error parseEpisode(const std::vector<uint8_t> &data, Episode &out);

} // namespace reluva::sith
