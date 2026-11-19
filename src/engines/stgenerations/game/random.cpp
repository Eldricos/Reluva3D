// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/random.h"

#include <atomic>
#include <ctime>

namespace reluva::stgenerations {

namespace {
std::atomic<uint32_t> fixedSeed{0};
}

void RandomSource::setFixedSeed(uint32_t seed) {
    fixedSeed = seed;
}

RandomSource::RandomSource(uint32_t seed) {
    if (!seed)
        seed = fixedSeed ? fixedSeed.load() : uint32_t(std::time(nullptr));
    setSeed(seed);
}

void RandomSource::setSeed(uint32_t seed) {
    seed_ = seed;
    state_ = seed ? seed : 0x2545F491;
}

uint32_t RandomSource::getRandomNumber(uint32_t max) {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    if (max == 0xFFFFFFFF)
        return state_;
    return state_ % (max + 1);
}

} // namespace reluva::stgenerations
