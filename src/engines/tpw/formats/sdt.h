// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A sound bank (`.sdt`), as SimCoaster's `Data/global/sound/UIHD.sdt`: sounds
 * by number, each MPEG audio (layer I or II) or 16-bit PCM. See
 * docs/engines/tpw/games/simcoaster/formats/sdt.md.
 */
class SoundBank {
public:
    /** A sound's header. */
    struct Entry {
        std::string name;    // up to 16 characters, cut short as stored
        uint32_t offset = 0; // of its data, in the bank
        uint32_t size = 0;   // of its data
        int rate = 0;
        int channels = 0;
        bool mpeg = false;
        uint32_t pcmBytes = 0; // its size decoded, for MPEG; 0 for PCM
        /** False for an entry whose offset holds no header (some music banks' last ones). */
        bool readable = false;
    };

    /** Reads the bank's entries. Fails if its table runs past the end. */
    Error parse(std::vector<uint8_t> data);

    /** Every entry, by number. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** A sound decoded; fails for an entry not readable, or data that doesn't decode. */
    Error decode(size_t index, audio::Sound &out) const;

private:
    std::vector<uint8_t> _data;
    std::vector<Entry> _entries;
};

} // namespace reluva::tpw
