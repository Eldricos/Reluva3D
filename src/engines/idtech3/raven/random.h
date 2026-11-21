// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::idtech3::raven {

/**
 * The Microsoft C library's rand(), which jk2sp.exe and jk2mp.exe both
 * link (jk2sp.exe's 0x4b3a43, jk2mp.exe's 0x4d554b): 0 to 32767 a draw.
 * Both seed it from their clock (srand), so their draws differ each run.
 */
class CrtRandom {
public:
    explicit CrtRandom(uint32_t seed = 1) : _holdrand(seed) {}
    void seed(uint32_t s) { _holdrand = s; }
    int32_t next() {
        _holdrand = _holdrand * 214013u + 2531011u;
        return int32_t((_holdrand >> 16) & 0x7fff);
    }
    /** jk2sp.exe's flrand for its renderer (0x4095d0): rand() of 32768ths across the range. */
    float flrand(float min, float max) { return float(double(next()) * (max - min) / 32768.0 + min); }

private:
    uint32_t _holdrand;
};

/**
 * jk2mp.exe's Q_flrand and Q_irand (0x434140, 0x434190), on a seed of
 * their own (0x89abcdef at the start, 0x4f7d70), which its weather and
 * surface sprites share.
 */
class QRandom {
public:
    float flrand(float min, float max) {
        step();
        return float(double(_holdrand >> 17) * (max - min) / 32768.0 + min);
    }
    /** From `min` to `max`, both included. */
    int32_t irand(int32_t min, int32_t max) {
        step();
        return min + ((max - min + 1) * int32_t(_holdrand >> 17) >> 15);
    }

private:
    void step() { _holdrand = _holdrand * 214013u + 2531011u; }
    uint32_t _holdrand = 0x89abcdef;
};

} // namespace reluva::idtech3::raven
