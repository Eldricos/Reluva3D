// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The particle effects' library (`.plb`, SimCoaster's
 * `Data/Particle/Tp2.plb`): the effects scripts and settings start by
 * number (`EVENT 2 <emitter> <effect>`, `Info.CreateParticleEffect`), each
 * its parameters, its name and its colours over a particle's life. See
 * docs/engines/tpw/games/simcoaster/formats/plb.md.
 */
class ParticleLibrary {
public:
    /** One effect: what's known of its record. */
    struct Effect {
        std::string name; // "Twinkle"
        /** A particle's colour over its life, first to last: A, R, G, B from the top byte down. */
        std::array<uint32_t, 16> colours{};
        uint32_t lifeMs = 0; // how long a particle lasts (word 29) *(inferred)*
        /** The record's words before its name, for what isn't worked out. */
        std::array<uint32_t, 70> words{};
    };

    /** Reads the library. Fails without its count and record size, or on a record past the end. */
    Error parse(const std::vector<uint8_t> &data);

    /** The effects, by their numbers: the first, "NULL", none. */
    const std::vector<Effect> &effects() const { return _effects; }

private:
    std::vector<Effect> _effects;
};

} // namespace reluva::tpw
