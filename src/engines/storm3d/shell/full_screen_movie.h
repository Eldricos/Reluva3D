// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "video/bink.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A movie played once over the whole screen, as the opening movie is:
 * scaled up by a whole number to fill as much of it as it can, centred,
 * its frame for the time since it started.
 */
class FullScreenMovie {
public:
    /**
     * Reads it, its sound decoded whole.
     * @return a failure if it isn't there or isn't a movie
     */
    Error open(const GameFiles &files, const std::string &name);
    /** Its sound, to play as it starts; null if it has none. */
    std::shared_ptr<const audio::Sound> sound() const { return _sound; }

    /** Starts it at `millis` of the clock. */
    void start(uint64_t millis) { _start = millis; }
    /**
     * Brings it to its frame for `millis`.
     * @return false once it's past its last frame, or a frame fails
     */
    bool update(uint64_t millis);
    /** Draws the frame it's at into an RGBA surface, black around it. */
    void draw(Surface &screen) const;

private:
    bink::Movie _movie;
    std::shared_ptr<audio::Sound> _sound;
    uint64_t _start = 0;
    int64_t _shown = -1;
    std::vector<uint8_t> _rgba;
};

} // namespace reluva::storm3d
