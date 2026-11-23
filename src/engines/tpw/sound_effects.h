// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/sound_map.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The game's sound effects by their numbers, as scripts start them
 * (`EVENT 5 <where> <effect>`): read from a folder's categories, their
 * sounds decoded from the banks as they're first played. See
 * docs/engines/tpw/games/simcoaster/formats/sound-maps.md.
 */
class SoundEffects {
public:
    /** The categories every world has, in `global/sound/`. */
    static const std::vector<std::string> &globalCategories();

    /**
     * Reads categories (`rides` for `cat_ridesSFX.map` and its BANK) from a
     * folder of the game's (`global/sound`); an effect a later one has too
     * is the earlier's. Fails if one can't be read.
     */
    Error load(const SearchSet &files, const std::string &folder, const std::vector<std::string> &categories);

    /** What playing an effect starts. */
    struct Choice {
        std::string bank; // the bank's file, as `global/sound/RideHD.sdt`
        uint32_t entry = 0;
        int volume = 100; // percent
    };
    /** Picks the sound an effect plays, by the clock's rolls; false for no such effect. */
    bool choose(uint32_t id, Choice &out);

    /**
     * Starts an effect on a mixer, as a sound effect.
     * @return The sound's handle; audio::kNoSound for an effect not found,
     *     or a sound that doesn't decode.
     */
    audio::SoundHandle play(audio::Mixer &mixer, uint32_t id);

private:
    struct Category {
        std::string folder;
        SoundMap map;
    };

    uint16_t roll();

    const SearchSet *_files = nullptr;
    std::vector<Category> _categories;
    std::map<std::string, std::unique_ptr<SoundBank>> _banks; // by file, read when first played
    std::map<std::pair<std::string, uint32_t>, std::shared_ptr<const audio::Sound>> _sounds;
    uint32_t _seed = 1;
};

} // namespace reluva::tpw
