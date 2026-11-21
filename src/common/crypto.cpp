// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/crypto.h"

#include <cstring>
#include <random>

namespace reluva::crypto {

namespace {

// --- GF(2^255 - 19) ---------------------------------------------------------
// A number as sixteen 16-bit limbs, least first, held in 64 bits so sums
// and products need no carry until the end; limbs may go negative between
// carries. 2^256 = 2 * (2^255 - 19) + 38, so what carries out of the top
// comes back in at the bottom times 38.
using Fe = std::array<int64_t, 16>;

void carry(Fe &a) {
    for (size_t i = 0; i < 16; ++i) {
        const int64_t c = a[i] >> 16; // floor, negative limbs included
        a[i] -= c * 65536;
        if (i < 15)
            a[i + 1] += c;
        else
            a[0] += 38 * c;
    }
}

Fe add(const Fe &a, const Fe &b) {
    Fe out;
    for (size_t i = 0; i < 16; ++i)
        out[i] = a[i] + b[i];
    return out;
}

Fe sub(const Fe &a, const Fe &b) {
    Fe out;
    for (size_t i = 0; i < 16; ++i)
        out[i] = a[i] - b[i];
    return out;
}

Fe mul(const Fe &a, const Fe &b) {
    // Limbs within about 2^17 either way: products of 2^34, 31 sums of up
    // to 16, the top half times 38: all well inside 63 bits.
    std::array<int64_t, 31> t{};
    for (size_t i = 0; i < 16; ++i)
        for (size_t j = 0; j < 16; ++j)
            t[i + j] += a[i] * b[j];
    Fe out;
    for (size_t i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    for (size_t i = 0; i < 16; ++i)
        out[i] = t[i];
    carry(out);
    carry(out);
    return out;
}

// a^(p - 2) = 1/a; p - 2 = 2^255 - 21, every bit set but 2 and 4.
Fe invert(const Fe &a) {
    Fe r = a;
    for (int bit = 253; bit >= 0; --bit) {
        r = mul(r, r);
        if (bit != 2 && bit != 4)
            r = mul(r, a);
    }
    return r;
}

Fe unpack(const Key &in) {
    Fe out;
    for (size_t i = 0; i < 16; ++i)
        out[i] = int64_t(in[2 * i]) | (int64_t(in[2 * i + 1]) << 8);
    out[15] &= 0x7FFF; // the top bit is ignored (RFC 7748, section 5)
    return out;
}

Key pack(Fe a) {
    carry(a);
    carry(a);
    carry(a);
    // Now 0 <= a < 2^256; take p off while that leaves it non-negative
    // (at most twice).
    for (int pass = 0; pass < 2; ++pass) {
        Fe m;
        int64_t borrow = 0;
        for (size_t i = 0; i < 16; ++i) {
            const int64_t p = i == 0 ? 0xFFED : i == 15 ? 0x7FFF : 0xFFFF;
            const int64_t v = a[i] - p - borrow;
            borrow = v < 0 ? 1 : 0;
            m[i] = v + borrow * 65536;
        }
        if (!borrow)
            a = m;
    }
    Key out;
    for (size_t i = 0; i < 16; ++i) {
        out[2 * i] = uint8_t(a[i]);
        out[2 * i + 1] = uint8_t(a[i] >> 8);
    }
    return out;
}

// Swaps a and b if `swap`, the same steps either way.
void cswap(Fe &a, Fe &b, int64_t swap) {
    const int64_t mask = -swap;
    for (size_t i = 0; i < 16; ++i) {
        const int64_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

// --- ChaCha20 ---------------------------------------------------------------

uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void putLe32(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

uint32_t rotl(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

void quarterRound(std::array<uint32_t, 16> &x, size_t a, size_t b, size_t c, size_t d) {
    x[a] += x[b];
    x[d] = rotl(x[d] ^ x[a], 16);
    x[c] += x[d];
    x[b] = rotl(x[b] ^ x[c], 12);
    x[a] += x[b];
    x[d] = rotl(x[d] ^ x[a], 8);
    x[c] += x[d];
    x[b] = rotl(x[b] ^ x[c], 7);
}

} // namespace

Key x25519(const Key &scalar, const Key &point) {
    Key k = scalar;
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
    const Fe x1 = unpack(point);
    Fe x2{};
    x2[0] = 1;
    Fe z2{};
    Fe x3 = x1;
    Fe z3{};
    z3[0] = 1;
    Fe a24{}; // (486662 - 2) / 4 = 121665
    a24[0] = 0xDB41;
    a24[1] = 1;
    int64_t swap = 0;
    // The Montgomery ladder, as RFC 7748 section 5 writes it.
    for (int t = 254; t >= 0; --t) {
        const int64_t bit = (k[size_t(t) / 8] >> (t % 8)) & 1;
        swap ^= bit;
        cswap(x2, x3, swap);
        cswap(z2, z3, swap);
        swap = bit;
        const Fe a = add(x2, z2);
        const Fe aa = mul(a, a);
        const Fe b = sub(x2, z2);
        const Fe bb = mul(b, b);
        const Fe e = sub(aa, bb);
        const Fe c = add(x3, z3);
        const Fe d = sub(x3, z3);
        const Fe da = mul(d, a);
        const Fe cb = mul(c, b);
        const Fe sum = add(da, cb);
        x3 = mul(sum, sum);
        const Fe diff = sub(da, cb);
        z3 = mul(x1, mul(diff, diff));
        x2 = mul(aa, bb);
        z2 = mul(e, add(aa, mul(a24, e)));
    }
    cswap(x2, x3, swap);
    cswap(z2, z3, swap);
    return pack(mul(x2, invert(z2)));
}

Key x25519Public(const Key &secret) {
    Key base{};
    base[0] = 9;
    return x25519(secret, base);
}

Key randomKey() {
    std::random_device random;
    Key k;
    for (size_t i = 0; i < k.size(); i += 4) {
        const uint32_t v = random();
        putLe32(&k[i], v);
    }
    return k;
}

std::array<uint8_t, 64> chacha20Block(const Key &key, const Nonce &nonce, uint32_t counter) {
    std::array<uint32_t, 16> state = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574}; // "expand 32-byte k"
    for (size_t i = 0; i < 8; ++i)
        state[4 + i] = le32(&key[4 * i]);
    state[12] = counter;
    for (size_t i = 0; i < 3; ++i)
        state[13 + i] = le32(&nonce[4 * i]);
    std::array<uint32_t, 16> x = state;
    for (int round = 0; round < 10; ++round) {
        quarterRound(x, 0, 4, 8, 12);
        quarterRound(x, 1, 5, 9, 13);
        quarterRound(x, 2, 6, 10, 14);
        quarterRound(x, 3, 7, 11, 15);
        quarterRound(x, 0, 5, 10, 15);
        quarterRound(x, 1, 6, 11, 12);
        quarterRound(x, 2, 7, 8, 13);
        quarterRound(x, 3, 4, 9, 14);
    }
    std::array<uint8_t, 64> out;
    for (size_t i = 0; i < 16; ++i)
        putLe32(&out[4 * i], x[i] + state[i]);
    return out;
}

void chacha20(const Key &key, const Nonce &nonce, uint32_t counter, uint8_t *data, size_t size) {
    for (size_t at = 0; at < size; at += 64, ++counter) {
        const std::array<uint8_t, 64> stream = chacha20Block(key, nonce, counter);
        for (size_t i = 0; i < 64 && at + i < size; ++i)
            data[at + i] ^= stream[i];
    }
}

Tag poly1305(const Key &key, const uint8_t *data, size_t size) {
    // The accumulator and r as five 26-bit limbs; 2^130 = 5 mod
    // (2^130 - 5), so what's over 2^130 comes back in times 5.
    constexpr uint64_t kMask = (1u << 26) - 1;
    const uint32_t t0 = le32(&key[0]) & 0x0FFFFFFF; // r, clamped
    const uint32_t t1 = le32(&key[4]) & 0x0FFFFFFC;
    const uint32_t t2 = le32(&key[8]) & 0x0FFFFFFC;
    const uint32_t t3 = le32(&key[12]) & 0x0FFFFFFC;
    const uint64_t r0 = t0 & kMask;
    const uint64_t r1 = ((t0 >> 26) | (uint64_t(t1) << 6)) & kMask;
    const uint64_t r2 = ((t1 >> 20) | (uint64_t(t2) << 12)) & kMask;
    const uint64_t r3 = ((t2 >> 14) | (uint64_t(t3) << 18)) & kMask;
    const uint64_t r4 = t3 >> 8;
    const uint64_t s1 = r1 * 5;
    const uint64_t s2 = r2 * 5;
    const uint64_t s3 = r3 * 5;
    const uint64_t s4 = r4 * 5;
    uint64_t h0 = 0;
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    uint64_t h3 = 0;
    uint64_t h4 = 0;
    for (size_t at = 0; at < size; at += 16) {
        // A block with a 1 after its last byte (RFC 8439, section 2.5.1).
        uint8_t block[17] = {};
        const size_t n = size - at < 16 ? size - at : 16;
        std::memcpy(block, data + at, n);
        block[n] = 1;
        const uint32_t m0 = le32(block);
        const uint32_t m1 = le32(block + 4);
        const uint32_t m2 = le32(block + 8);
        const uint32_t m3 = le32(block + 12);
        h0 += m0 & kMask;
        h1 += ((m0 >> 26) | (uint64_t(m1) << 6)) & kMask;
        h2 += ((m1 >> 20) | (uint64_t(m2) << 12)) & kMask;
        h3 += ((m2 >> 14) | (uint64_t(m3) << 18)) & kMask;
        h4 += (m3 >> 8) | (uint64_t(block[16]) << 24);
        const uint64_t d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
        uint64_t d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
        uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
        uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
        uint64_t d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;
        h0 = d0 & kMask;
        d1 += d0 >> 26;
        h1 = d1 & kMask;
        d2 += d1 >> 26;
        h2 = d2 & kMask;
        d3 += d2 >> 26;
        h3 = d3 & kMask;
        d4 += d3 >> 26;
        h4 = d4 & kMask;
        h0 += (d4 >> 26) * 5;
        h1 += h0 >> 26;
        h0 &= kMask;
    }
    // Fully carried, then less p if that's not negative.
    h2 += h1 >> 26;
    h1 &= kMask;
    h3 += h2 >> 26;
    h2 &= kMask;
    h4 += h3 >> 26;
    h3 &= kMask;
    h0 += (h4 >> 26) * 5;
    h4 &= kMask;
    h1 += h0 >> 26;
    h0 &= kMask;
    uint64_t g0 = h0 + 5;
    uint64_t g1 = h1 + (g0 >> 26);
    g0 &= kMask;
    uint64_t g2 = h2 + (g1 >> 26);
    g1 &= kMask;
    uint64_t g3 = h3 + (g2 >> 26);
    g2 &= kMask;
    const uint64_t g4 = h4 + (g3 >> 26) - (uint64_t(1) << 26);
    g3 &= kMask;
    if (!(g4 >> 63)) { // h + 5 reached 2^130: h was at least p
        h0 = g0;
        h1 = g1;
        h2 = g2;
        h3 = g3;
        h4 = g4;
    }
    // Plus s, mod 2^128.
    const uint32_t w[4] = {uint32_t(h0 | (h1 << 26)), uint32_t((h1 >> 6) | (h2 << 20)),
                           uint32_t((h2 >> 12) | (h3 << 14)), uint32_t((h3 >> 18) | (h4 << 8))};
    Tag tag;
    uint64_t c = 0;
    for (size_t i = 0; i < 4; ++i) {
        c += uint64_t(w[i]) + le32(&key[16 + 4 * i]);
        putLe32(&tag[4 * i], uint32_t(c));
        c >>= 32;
    }
    return tag;
}

namespace {

// The data Poly1305 signs for the sealed box: the AAD and the ciphertext,
// each padded to 16, then their lengths.
Tag aeadTag(const Key &key, const Nonce &nonce, const uint8_t *aad, size_t aadSize, const uint8_t *cipher,
            size_t size) {
    const std::array<uint8_t, 64> first = chacha20Block(key, nonce, 0);
    Key oneTime;
    std::memcpy(oneTime.data(), first.data(), 32);
    std::vector<uint8_t> mac;
    mac.reserve(aadSize + size + 48);
    mac.insert(mac.end(), aad, aad + aadSize);
    mac.resize((mac.size() + 15) / 16 * 16, 0);
    mac.insert(mac.end(), cipher, cipher + size);
    mac.resize((mac.size() + 15) / 16 * 16, 0);
    for (const uint64_t n : {uint64_t(aadSize), uint64_t(size)})
        for (int i = 0; i < 8; ++i)
            mac.push_back(uint8_t(n >> (8 * i)));
    return poly1305(oneTime, mac.data(), mac.size());
}

} // namespace

std::vector<uint8_t> seal(const Key &key, const Nonce &nonce, const uint8_t *aad, size_t aadSize, const uint8_t *plain,
                          size_t size) {
    std::vector<uint8_t> out(plain, plain + size);
    chacha20(key, nonce, 1, out.data(), out.size());
    const Tag tag = aeadTag(key, nonce, aad, aadSize, out.data(), out.size());
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}

bool open(const Key &key, const Nonce &nonce, const uint8_t *aad, size_t aadSize, const uint8_t *sealed, size_t size,
          std::vector<uint8_t> &plain) {
    if (size < 16)
        return false;
    const size_t n = size - 16;
    const Tag tag = aeadTag(key, nonce, aad, aadSize, sealed, n);
    uint8_t diff = 0; // compared in full, whatever differs first
    for (size_t i = 0; i < 16; ++i)
        diff |= uint8_t(tag[i] ^ sealed[n + i]);
    if (diff)
        return false;
    plain.assign(sealed, sealed + n);
    chacha20(key, nonce, 1, plain.data(), plain.size());
    return true;
}

} // namespace reluva::crypto
