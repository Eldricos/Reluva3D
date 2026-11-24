// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "video/bink.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A Bink movie playing over and over by the clock, as the front end's
 * panels do: the frame for a time since it started, decoded as it's
 * reached (and from the start again once it's past the last).
 */
class PanelMovie {
public:
    /**
     * Reads the movie: `name` as the game's files have it
     * ("animations/single01.bik").
     * @return a failure if it isn't there or isn't a movie
     */
    Error open(const GameFiles &files, const std::string &name);
    bool isOpen() const { return _open; }
    int width() const { return _movie.width(); }
    int height() const { return _movie.height(); }

    /** Starts it from its first frame at `millis` of the clock. */
    void restart(uint64_t millis);
    /**
     * Brings it to its frame for `millis`, decoding what's between.
     * @return false if a frame fails to decode
     */
    bool update(uint64_t millis);
    /** The frame it's at, as RGBA, width x height. */
    const std::vector<uint8_t> &rgba() const { return _rgba; }

private:
    bink::Movie _movie;
    bool _open = false;
    uint64_t _start = 0;
    int64_t _shown = -1; // the frame in _rgba, counted from the start, over every loop
    std::vector<uint8_t> _rgba;
};

} // namespace reluva::storm3d
