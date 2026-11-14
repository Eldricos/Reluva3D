// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "video/bink.h"
#include "video/roq.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// A cinematic's movie in whichever format its game uses: Bink (Elite
// Force's, .bik) or RoQ (Quake III's and Jedi Outcast's, .roq), told by
// its file type.
class CinematicMovie {
public:
    Error open(std::vector<uint8_t> file, std::string_view extension);

    int width() const;
    int height() const;
    // Frames a second, as a fraction.
    uint32_t rateNumerator() const;
    uint32_t rateDenominator() const;

    // The next frame; false at the end.
    Error nextFrame(bool &more);
    int frameNumber() const; // the last one decoded, -1 before the first
    void toRgba(std::vector<uint8_t> &out) const;
    // Its sound whole (interleaved), read ahead of the frames, with its
    // rate and channels (0: none).
    Error decodeAllAudio(std::vector<int16_t> &sound, int &rate, int &channels);

private:
    std::unique_ptr<bink::Movie> _bink;
    std::unique_ptr<roq::Movie> _roq;
    std::vector<uint8_t> _file; // RoQ's: its sound is read from a second pass over it
};

} // namespace reluva::idtech3
