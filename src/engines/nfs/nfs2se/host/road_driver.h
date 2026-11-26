// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace reluva::nfs {
class GameHost;
}

namespace reluva::nfs::nfs2se {

/**
 * Drives the player's car in the hosted retail nfs2sen.exe along the
 * track's virtual road, for races run to their end without hands
 * (docs/engines/nfs/games/nfs2se/research/road-driver.md): the controls
 * the game samples from the keys replaced each frame before it passes them
 * on, so that a replay and the other copies on a link have them too. It aims a
 * little ahead along the road, goes as fast as the game's computer cars
 * are told to there, and
 * backs off a wall it's stuck against.
 */
class RoadDriver {
public:
    /** When it drives, and where on the road. */
    struct Options {
        uint64_t fromMs = 0;   // the game's clock
        uint64_t toMs = ~0ull; //
        float right = 0;       // metres right of the road's points (negative: left)
        float grip = 1.0f;     // times the grip it takes the bends with (14 m/s²), if the track has no speeds
    };

    /**
     * Puts the driver into `host`, loaded and not yet run: at the game's
     * call passing on the local player's controls (0x4403a9). Fails if the program isn't the
     * retail nfs2sen.exe, as the driver's addresses are its own.
     */
    Error install(GameHost &host, const Options &options);

    /** Called with a line now and then: what it does, and each finish with the place. */
    std::function<void(const std::string &)> log;

private:
    void drive(GameHost &host, uint32_t car);

    Options _options;
    uint64_t _logged = 0;
    // Stuck against something: since when it's been slow, and until when
    // it backs off.
    uint64_t _slowSince = 0;
    uint64_t _reverseUntil = 0;
    // Then, until when it aims this far to the side (metres), to get past
    // what it hit; the other side each time.
    uint64_t _asideUntil = 0;
    float _aside = 0;
    int _backOffs = 0;
    int32_t _progressMark = 0; // the car's progress (laps times points) when last well on its way
    int32_t _bestProgress = 0; // the farthest it has been, and when it got there
    uint64_t _advancedAt = 0;
    bool _shifted = false; // a shift button sent last frame: the game takes a press, not a hold
    int _laps = 0;
};

} // namespace reluva::nfs::nfs2se
