// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/md5.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva {

namespace {

constexpr int kShift[64] = {
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
};

struct Constants {
    uint32_t k[64];
    Constants() {
        for (int i = 0; i < 64; ++i)
            k[i] = static_cast<uint32_t>(std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
    }
};

const Constants &constants() {
    static const Constants kC;
    return kC;
}

uint32_t rotl(uint32_t x, int c) {
    return (x << c) | (x >> (32 - c));
}

} // namespace

Md5::Md5() : _state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {
}

void Md5::block(const uint8_t *p) {
    const uint32_t *K = constants().k;
    uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 |
               uint32_t(p[i * 4 + 3]) << 24;

    uint32_t a = _state[0];
    uint32_t b = _state[1];
    uint32_t c = _state[2];
    uint32_t d = _state[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        f += a + K[i] + m[g];
        a = d;
        d = c;
        c = b;
        b += rotl(f, kShift[i]);
    }
    _state[0] += a;
    _state[1] += b;
    _state[2] += c;
    _state[3] += d;
}

void Md5::update(const void *data, size_t len) {
    auto *p = static_cast<const uint8_t *>(data);
    _length += len;
    while (len > 0) {
        size_t n = std::min(len, sizeof(_buffer) - _bufferLen);
        std::memcpy(_buffer + _bufferLen, p, n);
        _bufferLen += n;
        p += n;
        len -= n;
        if (_bufferLen == sizeof(_buffer)) {
            block(_buffer);
            _bufferLen = 0;
        }
    }
}

std::string Md5::hexDigest() {
    uint64_t bits = _length * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t zero = 0;
    while (_bufferLen != 56)
        update(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i)
        lenBytes[i] = static_cast<uint8_t>(bits >> (8 * i));
    update(lenBytes, 8);

    static const char *hex = "0123456789abcdef";
    std::string out;
    for (uint32_t word : _state) {
        for (int i = 0; i < 4; ++i) {
            uint8_t byte = static_cast<uint8_t>(word >> (8 * i));
            out += hex[byte >> 4];
            out += hex[byte & 15];
        }
    }
    return out;
}

} // namespace reluva
