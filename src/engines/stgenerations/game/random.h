// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::stgenerations {

// The game's random numbers: xorshift32, as STG-Engine drew them. A seed of
// 0 takes one from the clock; tests give their own so runs repeat.
class RandomSource {
public:
    explicit RandomSource(uint32_t seed = 0);

    void setSeed(uint32_t seed);
    uint32_t getSeed() const { return seed_; }
    // 0 .. max, both included.
    uint32_t getRandomNumber(uint32_t max);
    uint32_t getRandomNumberRng(uint32_t min, uint32_t max) { return getRandomNumber(max - min) + min; }
    bool getRandomBit() { return (getRandomNumber(0xFFFFFFFF) >> 16) & 1; }

    // Seeds from now on (every RandomSource made with 0 after this) when
    // not 0: for tests, so a run is the same every time.
    static void setFixedSeed(uint32_t seed);

private:
    uint32_t seed_ = 0;
    uint32_t state_ = 0;
};

} // namespace reluva::stgenerations
