// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::video::indeo5 {

// A Huffman code described by rows: row i's codes are i one bits, a zero
// bit (except in the last row), then xbits[i] bits of an index; the
// symbols number the rows' codes in order.
struct Codebook {
    int rows = 0;
    uint8_t xbits[16] = {};
};

// What a block's symbols mean: each symbol a run of zero coefficients and
// the value after it, apart from the end-of-block and escape symbols.
struct RunValueMap {
    uint8_t endOfBlock, escape;
    uint8_t run[256];
    int8_t value[256];
};

extern const Codebook kMacroblockCodebooks[8];
extern const Codebook kBlockCodebooks[8];
extern const RunValueMap kRunValueMaps[9];
// The picture sizes a GOP header can pick, width and height over 4.
extern const uint8_t kPictureSizes[30];
// Dequantisation: base matrices (by the band's matrix number) and the
// scale for each quantiser level.
extern const uint16_t kBaseQuant8x8Inter[5][64];
extern const uint16_t kBaseQuant8x8Intra[5][64];
extern const uint16_t kBaseQuant4x4Inter[16];
extern const uint16_t kBaseQuant4x4Intra[16];
extern const uint8_t kScaleQuant8x8Inter[5][24];
extern const uint8_t kScaleQuant8x8Intra[5][24];
extern const uint8_t kScaleQuant4x4Inter[24];
extern const uint8_t kScaleQuant4x4Intra[24];

} // namespace reluva::video::indeo5
