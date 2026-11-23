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
 * (docs/engines/nfs/games/nfs2se/research/road-driver.md): the car's
 * controls, as the game reads them, replaced each time it does. It aims a
 * little ahead along the road, slows for the bends it sees coming, and
 * backs off a wall it's stuck against.
 */
class RoadDriver {
public:
    /** When it drives, and where on the road. */
    struct Options {
        uint64_t fromMs = 0;   // the game's clock
        uint64_t toMs = ~0ull; //
        float right = 0;       // metres right of the road's points (negative: left)
        float grip = 1.0f;     // times the grip it takes the bends with (14 m/s²)
    };

    /**
     * Puts the driver into `host`, loaded and not yet run: in place of the
     * human cars' input function, which the game sets at 0x43c942. Fails
     * if the program isn't the retail nfs2sen.exe, as the driver's
     * addresses are its own.
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
    int _laps = 0;
};

} // namespace reluva::nfs::nfs2se
