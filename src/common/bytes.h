// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva {

// Values stored in a file's bytes, at `p`.
inline uint16_t readLE16(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return uint16_t(b[0] | (b[1] << 8));
}

inline uint32_t readLE32(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

inline uint64_t readLE64(const void *p) {
    return uint64_t(readLE32(p)) | uint64_t(readLE32(static_cast<const uint8_t *>(p) + 4)) << 32;
}

inline uint32_t readBE32(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

inline void writeLE16(void *p, uint16_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = uint8_t(v), b[1] = uint8_t(v >> 8);
}

inline void writeLE32(void *p, uint32_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = uint8_t(v), b[1] = uint8_t(v >> 8), b[2] = uint8_t(v >> 16), b[3] = uint8_t(v >> 24);
}

inline void writeBE32(void *p, uint32_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = uint8_t(v >> 24), b[1] = uint8_t(v >> 16), b[2] = uint8_t(v >> 8), b[3] = uint8_t(v);
}

// A four-character code as little-endian files store it: 'R','I','F','F'
// read with readLE32.
constexpr uint32_t tagLE(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
// The same code read big-endian (readBE32): 'S','G','E','N' as 0x5347454E.
constexpr uint32_t tagBE(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) << 24 | uint32_t(uint8_t(b)) << 16 | uint32_t(uint8_t(c)) << 8 | uint32_t(uint8_t(d));
}

inline uint16_t readBE16(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return uint16_t((b[0] << 8) | b[1]);
}

} // namespace reluva
