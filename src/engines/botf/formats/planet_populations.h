// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// planspac.bin in stbof.res: a planet's maximum population by its type,
// size and bonuses (docs/engines/botf/formats/planspac.md).

namespace reluva::botf {

/** A planet bonus's level from its value in a save: 0 none, 1 some (310), 2 much (620). */
int bonusLevel(int value);

/** planspac.bin's maximum populations. */
class PlanetPopulations {
public:
    /** Reads the file: 40 bytes a planet type and size. */
    Error load(const std::vector<uint8_t> &data);
    /**
     * A planet's maximum population, or 0 for a type and size the file
     * hasn't (gas giants).
     * @param size 0 small, 1 medium, 2 large
     * @param growthLevel, energyLevel its bonuses' levels, bonusLevel()'s
     */
    int maxPopulation(int type, int size, int growthLevel, int energyLevel) const;

private:
    std::map<std::pair<int, int>, std::array<int, 9>> _rows; // type, size
};

} // namespace reluva::botf
