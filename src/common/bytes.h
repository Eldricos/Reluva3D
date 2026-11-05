// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva {

// Values stored in a file's bytes, at `p`.
inline uint16_t readLE16(const void* p) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return uint16_t(b[0] | (b[1] << 8));
}

inline uint32_t readLE32(const void* p) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

inline uint16_t readBE16(const void* p) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return uint16_t((b[0] << 8) | b[1]);
}

} // namespace reluva
