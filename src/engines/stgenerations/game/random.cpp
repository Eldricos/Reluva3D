// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/random.h"

#include <atomic>
#include <ctime>

namespace reluva::stgenerations {

namespace {
std::atomic<uint32_t> g_fixedSeed{0};
}

void RandomSource::setFixedSeed(uint32_t seed) {
    g_fixedSeed = seed;
}

RandomSource::RandomSource(uint32_t seed) {
    if (!seed)
        seed = g_fixedSeed ? g_fixedSeed.load() : uint32_t(std::time(nullptr));
    setSeed(seed);
}

void RandomSource::setSeed(uint32_t seed) {
    _seed = seed;
    _state = seed ? seed : 0x2545F491;
}

uint32_t RandomSource::getRandomNumber(uint32_t max) {
    _state ^= _state << 13;
    _state ^= _state >> 17;
    _state ^= _state << 5;
    if (max == 0xFFFFFFFF)
        return _state;
    return _state % (max + 1);
}

} // namespace reluva::stgenerations
