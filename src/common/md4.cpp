// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/md4.h"

#include <cstring>
#include <vector>

namespace reluva {

namespace {

uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void block(uint32_t h[4], const uint8_t* p) {
    uint32_t x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = uint32_t(p[4 * i]) | uint32_t(p[4 * i + 1]) << 8 | uint32_t(p[4 * i + 2]) << 16 |
               uint32_t(p[4 * i + 3]) << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    // Round 1: F(x, y, z) = (x & y) | (~x & z), shifts 3, 7, 11, 19.
    auto f = [](uint32_t x1, uint32_t y, uint32_t z) { return (x1 & y) | (~x1 & z); };
    static const int s1[4] = {3, 7, 11, 19};
    for (int i = 0; i < 16; ++i) {
        const uint32_t t = rotl(a + f(b, c, d) + x[i], s1[i & 3]);
        a = d, d = c, c = b, b = t;
    }
    // Round 2: G(x, y, z) = majority, the words by column, + 0x5a827999.
    auto g = [](uint32_t x1, uint32_t y, uint32_t z) { return (x1 & y) | (x1 & z) | (y & z); };
    static const int s2[4] = {3, 5, 9, 13};
    for (int i = 0; i < 16; ++i) {
        const int k = (i % 4) * 4 + i / 4;
        const uint32_t t = rotl(a + g(b, c, d) + x[k] + 0x5a827999u, s2[i & 3]);
        a = d, d = c, c = b, b = t;
    }
    // Round 3: H(x, y, z) = x ^ y ^ z, the words in bit-reversed order, + 0x6ed9eba1.
    static const int order3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    static const int s3[4] = {3, 9, 11, 15};
    for (int i = 0; i < 16; ++i) {
        const uint32_t t = rotl(a + (b ^ c ^ d) + x[order3[i]] + 0x6ed9eba1u, s3[i & 3]);
        a = d, d = c, c = b, b = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d;
}

} // namespace

std::array<uint8_t, 16> md4(const void* data, size_t len) {
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const auto* p = static_cast<const uint8_t*>(data);
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        block(h, p + i);
    // The rest, a 1 bit, zeros, and the length in bits, little-endian.
    std::vector<uint8_t> tail(p + i, p + len);
    tail.push_back(0x80);
    while (tail.size() % 64 != 56)
        tail.push_back(0);
    const uint64_t bits = uint64_t(len) * 8;
    for (int k = 0; k < 8; ++k)
        tail.push_back(uint8_t(bits >> (8 * k)));
    for (size_t k = 0; k < tail.size(); k += 64)
        block(h, tail.data() + k);
    std::array<uint8_t, 16> out{};
    for (int k = 0; k < 16; ++k)
        out[size_t(k)] = uint8_t(h[k / 4] >> (8 * (k % 4)));
    return out;
}

} // namespace reluva
