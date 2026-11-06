// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::bink {

// Bink's fixed tables (docs/common/bink.md).
extern const uint16_t kCriticalFrequencies[25]; // audio band edges, Hz
extern const uint8_t kScan[64];                 // DCT coefficient order
extern const uint16_t kTreeCodes[16][16];       // the 16 code trees: each symbol's code,
extern const uint8_t kTreeLengths[16][16];      // first bit read in bit 0, and its length
extern const uint8_t kPatterns[16][64];         // run blocks' fill orders
extern const int32_t kIntraQuant[16][64];       // DCT dequantisers (<< 11), by coefficient
extern const int32_t kInterQuant[16][64];       // in scan order

} // namespace reluva::bink
