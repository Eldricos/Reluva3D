// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::stgenerations {

/**
 * The game's random numbers: xorshift32, as STG-Engine drew them. Tests
 * give their own seed so runs repeat.
 */
class RandomSource {
public:
    /** Starts from `seed`, or with 0 from the clock (or setFixedSeed's). */
    explicit RandomSource(uint32_t seed = 0);

    /** The seed the numbers start again from; 0 starts from a fixed state. */
    void setSeed(uint32_t seed);
    /** The seed last set. */
    uint32_t getSeed() const { return _seed; }
    /** A number from 0 to `max`, both included. */
    uint32_t getRandomNumber(uint32_t max);
    /** A number from `min` to `max`, both included. */
    uint32_t getRandomNumberRng(uint32_t min, uint32_t max) { return getRandomNumber(max - min) + min; }
    /** A random bit, from the middle of a number. */
    bool getRandomBit() { return (getRandomNumber(0xFFFFFFFF) >> 16) & 1; }

    /**
     * The seed every RandomSource made with 0 from now on takes instead of
     * the clock's, when not 0: for tests, so a run is the same every time.
     */
    static void setFixedSeed(uint32_t seed);

private:
    uint32_t _seed = 0;
    uint32_t _state = 0;
};

} // namespace reluva::stgenerations
