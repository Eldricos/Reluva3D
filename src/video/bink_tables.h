// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::bink {

// Bink's fixed tables (docs/common/video/bink.md).

/** The audio's band edges, in Hz. */
extern const uint16_t kCriticalFrequencies[25];
/** The order DCT coefficients come in. */
extern const uint8_t kScan[64];
/** The 16 code trees: each symbol's code, first bit read in bit 0, and its length. */
///@{
extern const uint16_t kTreeCodes[16][16];
extern const uint8_t kTreeLengths[16][16];
///@}
/** Run blocks' fill orders. */
extern const uint8_t kPatterns[16][64];
/** DCT dequantisers (<< 11), by coefficient in scan order. */
///@{
extern const int32_t kIntraQuant[16][64];
extern const int32_t kInterQuant[16][64];
///@}

} // namespace reluva::bink
