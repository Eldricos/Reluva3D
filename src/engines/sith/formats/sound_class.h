// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::sith {

/**
 * A .snd sound class: the sounds a thing makes, each for an event of its
 * own (docs/engines/sith/games/jk/formats/snd.md).
 */
struct SoundClass {
    /** One event's sound. Fields a line leaves out are empty. */
    struct Entry {
        /** What makes the sound: "startmove", "hurtimpact", "lwalkmetal"... */
        std::string event;
        /** The .wav, in the archives' sound or voice folder. */
        std::string wav;
        std::optional<uint32_t> flags;
        std::optional<float> minRadius;
        std::optional<float> maxRadius;
        std::optional<float> maxVolume;
    };

    std::vector<Entry> entries;

    /** The first entry for an event, matched ignoring case, or nullptr. */
    const Entry *find(std::string_view event) const;
};

/** Reads a .snd file; fails if a line has a number it can't read. */
Error parseSoundClass(const std::vector<uint8_t> &data, SoundClass &out);

} // namespace reluva::sith
