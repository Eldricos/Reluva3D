// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A category of sound effects, as SimCoaster's `Data/global/sound/` keeps
 * its rides': its banks (`cat_ridesBANK.map`, the banks' folders and
 * names) and its effects (`cat_ridesSFX.map`), each a number scripts start
 * it by and the sounds in the banks it picks from. See
 * docs/engines/tpw/games/simcoaster/formats/sound-maps.md.
 */
class SoundMap {
public:
    /** One of a group's sounds. */
    struct Sound {
        uint32_t bank = 0;   // in banks(), from 0
        uint32_t entry = 0;  // in the bank, from 0
        uint16_t upTo = 0;   // picked if a roll of 0 to 0xffff is at most this, and above the sound before's
        uint32_t millis = 0; // how long it is
    };

    /** Sounds an effect may play, one of them picked. */
    struct Group {
        int volumeLow = 100; // percent: a volume between the two *(inferred)*
        int volumeHigh = 100;
        uint16_t weight = 0; // its chance of being the group played, of 0xffff in all
        std::vector<Sound> sounds;
    };

    struct Effect {
        uint32_t id = 0;
        std::vector<Group> groups;
    };

    /** Reads a category from its two files. Fails if either runs short. */
    Error parse(const std::vector<uint8_t> &effects, const std::vector<uint8_t> &banks);

    /** The banks' names, as `Sound\Ride`: `Sound/RideHD.sdt` beside the category's folder. */
    const std::vector<std::string> &banks() const { return _banks; }
    const std::vector<Effect> &effects() const { return _effects; }
    /** The effect of an id; nullptr for none. */
    const Effect *find(uint32_t id) const;

    /**
     * The sound an effect plays for two rolls of 0 to 0xffff, the first
     * choosing its group, the second the sound in it; nullptr if it has none.
     */
    static const Sound *pick(const Effect &effect, uint16_t groupRoll, uint16_t soundRoll, const Group **group);
    /** The sound a roll of 0 to 0xffff picks in a group; nullptr if it has none. */
    static const Sound *pick(const Group &group, uint16_t roll);

private:
    std::vector<std::string> _banks;
    std::vector<Effect> _effects;
};

} // namespace reluva::tpw
