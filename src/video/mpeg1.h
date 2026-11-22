// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstdint>
#include <vector>

// What MPEG-1-like intra coding shares (ISO/IEC 11172-2): the AC
// coefficients' codes, the zigzag scan and the default intra quantiser
// matrix, as EA's Madcow video and the PlayStation's MDEC frames use them.

namespace reluva::mpeg1 {

/** An AC code: its length in bits (0: no code), the run of zeros, the level. */
struct AcEntry {
    uint8_t length = 0;
    uint8_t run = 0;  // 0xff: the end of the block
    int8_t level = 0; // 0: the escape, its run and level following in the codec's order
};

/**
 * The AC codes (table B.14) by the next 16 bits of the stream, read from
 * the top: a sign bit follows each but the escape and the end of block.
 */
const std::vector<AcEntry> &acTable();

/** The zigzag scan: position in the scan to raster index. */
const std::array<uint8_t, 64> &zigzag();

/** The default intra quantiser matrix, in raster order. */
inline constexpr uint8_t kIntraMatrix[64] = {
    8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
    34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
    35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

} // namespace reluva::mpeg1
