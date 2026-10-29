// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/md5.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva {

namespace {

constexpr int kShift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

struct Constants {
    uint32_t k[64];
    Constants() {
        for (int i = 0; i < 64; ++i)
            k[i] = static_cast<uint32_t>(std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
    }
};

const Constants& constants() {
    static const Constants c;
    return c;
}

uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

} // namespace

Md5::Md5() : state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::block(const uint8_t* p) {
    const uint32_t* K = constants().k;
    uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 |
               uint32_t(p[i * 4 + 3]) << 24;

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
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
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

void Md5::update(const void* data, size_t len) {
    auto* p = static_cast<const uint8_t*>(data);
    length_ += len;
    while (len > 0) {
        size_t n = std::min(len, sizeof(buffer_) - bufferLen_);
        std::memcpy(buffer_ + bufferLen_, p, n);
        bufferLen_ += n;
        p += n;
        len -= n;
        if (bufferLen_ == sizeof(buffer_)) {
            block(buffer_);
            bufferLen_ = 0;
        }
    }
}

std::string Md5::hexDigest() {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint8_t byte : digest()) {
        out += hex[byte >> 4];
        out += hex[byte & 15];
    }
    return out;
}

std::array<uint8_t, 16> Md5::digest() {
    uint64_t bits = length_ * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t zero = 0;
    while (bufferLen_ != 56)
        update(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i)
        lenBytes[i] = static_cast<uint8_t>(bits >> (8 * i));
    update(lenBytes, 8);
    std::array<uint8_t, 16> out{};
    for (size_t w = 0; w < 4; ++w)
        for (size_t i = 0; i < 4; ++i)
            out[w * 4 + i] = static_cast<uint8_t>(state_[w] >> (8 * i));
    return out;
}

std::array<uint8_t, 16> hmacMd5(const void* key, size_t keyLen, const void* message, size_t len) {
    // RFC 2104: MD5((K ^ opad) || MD5((K ^ ipad) || message)), the key
    // hashed first if it's longer than MD5's 64-byte block.
    std::array<uint8_t, 64> k{};
    if (keyLen > k.size()) {
        Md5 h;
        h.update(key, keyLen);
        const auto d = h.digest();
        std::copy(d.begin(), d.end(), k.begin());
    } else if (keyLen) {
        std::memcpy(k.data(), key, keyLen);
    }
    std::array<uint8_t, 64> pad;
    for (size_t i = 0; i < 64; ++i)
        pad[i] = k[i] ^ 0x36;
    Md5 inner;
    inner.update(pad.data(), pad.size());
    inner.update(message, len);
    const auto innerDigest = inner.digest();
    for (size_t i = 0; i < 64; ++i)
        pad[i] = k[i] ^ 0x5c;
    Md5 outer;
    outer.update(pad.data(), pad.size());
    outer.update(innerDigest.data(), innerDigest.size());
    return outer.digest();
}

} // namespace reluva
