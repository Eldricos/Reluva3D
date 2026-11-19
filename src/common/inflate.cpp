// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Deflate decoding as RFC 1951 describes it: blocks stored, or coded with
// the fixed Huffman codes or ones the block gives; codes decoded a bit at
// a time against the count of codes of each length (canonical codes).

#include "common/inflate.h"

#include <array>

namespace reluva {

namespace {

constexpr int kMaxBits = 15;

struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{}; // codes of each length
    std::vector<uint16_t> symbols;              // in code order
};

struct Inflater {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;
    bool overrun = false;
    std::vector<uint8_t>& out;
    size_t maxSize;

    int bit() {
        if (bitCount == 0) {
            if (pos >= size) {
                overrun = true;
                return 0;
            }
            bitBuf = data[pos++];
            bitCount = 8;
        }
        const int b = int(bitBuf & 1);
        bitBuf >>= 1;
        --bitCount;
        return b;
    }
    uint32_t bits(int n) { // least significant first
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= uint32_t(bit()) << i;
        return v;
    }
    int decode(const Huffman& h) {
        // Codes of each length follow the shorter ones', most significant
        // bit first.
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= bit();
            const int count = h.count[size_t(len)];
            if (code - first < count)
                return h.symbols[size_t(index + code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
            if (overrun)
                return -1;
        }
        return -1;
    }
};

bool build(Huffman& h, const uint8_t* lengths, int n) {
    h.count.fill(0);
    for (int i = 0; i < n; ++i)
        ++h.count[lengths[i]];
    h.count[0] = 0;
    // Not more codes of a length than there's room for.
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[size_t(len)];
        if (left < 0)
            return false;
    }
    std::array<uint16_t, kMaxBits + 2> offsets{};
    for (int len = 1; len <= kMaxBits; ++len)
        offsets[size_t(len + 1)] = uint16_t(offsets[size_t(len)] + h.count[size_t(len)]);
    h.symbols.assign(size_t(offsets[kMaxBits + 1]), 0);
    for (int i = 0; i < n; ++i)
        if (lengths[i])
            h.symbols[offsets[lengths[i]]++] = uint16_t(i);
    return true;
}

constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Inflater& in, const Huffman& lit, const Huffman& dist) {
    while (true) {
        const int sym = in.decode(lit);
        if (sym < 0 || in.overrun)
            return false;
        if (sym < 256) {
            if (in.out.size() >= in.maxSize)
                return false;
            in.out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return true;
        } else {
            const int l = sym - 257;
            if (l >= 29)
                return false;
            const size_t length = kLengthBase[l] + in.bits(kLengthExtra[l]);
            const int d = in.decode(dist);
            if (d < 0 || d >= 30)
                return false;
            const size_t distance = kDistBase[d] + in.bits(kDistExtra[d]);
            if (distance > in.out.size() || in.out.size() + length > in.maxSize || in.overrun)
                return false;
            const size_t from = in.out.size() - distance;
            for (size_t i = 0; i < length; ++i)
                in.out.push_back(in.out[from + i]);
        }
    }
}

} // namespace

Error inflateStream(const uint8_t* data, size_t dataSize, std::vector<uint8_t>& out, size_t& consumed,
                    size_t maxSize) {
    out.clear();
    consumed = 0;
    Inflater in{data, dataSize, 0, 0, 0, false, out, maxSize};
    static const Huffman fixedLit = [] {
        std::array<uint8_t, 288> l{};
        for (size_t i = 0; i < 288; ++i)
            l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        Huffman h;
        build(h, l.data(), 288);
        return h;
    }();
    static const Huffman fixedDist = [] {
        std::array<uint8_t, 30> l;
        l.fill(5);
        Huffman h;
        build(h, l.data(), 30);
        return h;
    }();
    bool last = false;
    while (!last) {
        last = in.bit() != 0;
        const uint32_t type = in.bits(2);
        if (in.overrun)
            return Error::fail("Deflate data ends early");
        if (type == 0) {
            // Stored: from the next byte, its length and that inverted.
            in.bitCount = 0;
            if (in.pos + 4 > dataSize)
                return Error::fail("Deflate data ends early");
            const uint32_t len = data[in.pos] | uint32_t(data[in.pos + 1]) << 8;
            const uint32_t nlen = data[in.pos + 2] | uint32_t(data[in.pos + 3]) << 8;
            in.pos += 4;
            if ((len ^ 0xffff) != nlen || in.pos + len > dataSize || out.size() + len > maxSize)
                return Error::fail("Damaged deflate data");
            out.insert(out.end(), data + in.pos, data + in.pos + len);
            in.pos += len;
        } else if (type == 1) {
            if (!codes(in, fixedLit, fixedDist))
                return Error::fail("Damaged deflate data");
        } else if (type == 2) {
            // The block's own codes: their lengths, coded with a code
            // whose lengths come first, in this order.
            static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            const int nlen = int(in.bits(5)) + 257, ndist = int(in.bits(5)) + 1, ncode = int(in.bits(4)) + 4;
            if (nlen > 286 || ndist > 30)
                return Error::fail("Damaged deflate data");
            std::array<uint8_t, 320> lengths{};
            for (int i = 0; i < ncode; ++i)
                lengths[kOrder[i]] = uint8_t(in.bits(3));
            Huffman lencode;
            if (!build(lencode, lengths.data(), 19))
                return Error::fail("Damaged deflate data");
            lengths.fill(0);
            for (int i = 0; i < nlen + ndist;) {
                int sym = in.decode(lencode);
                if (sym < 0 || in.overrun)
                    return Error::fail("Damaged deflate data");
                if (sym < 16) {
                    lengths[size_t(i++)] = uint8_t(sym);
                    continue;
                }
                uint8_t value = 0;
                int repeat;
                if (sym == 16) {
                    if (i == 0)
                        return Error::fail("Damaged deflate data");
                    value = lengths[size_t(i - 1)];
                    repeat = 3 + int(in.bits(2));
                } else if (sym == 17) {
                    repeat = 3 + int(in.bits(3));
                } else {
                    repeat = 11 + int(in.bits(7));
                }
                if (i + repeat > nlen + ndist)
                    return Error::fail("Damaged deflate data");
                while (repeat--)
                    lengths[size_t(i++)] = value;
            }
            Huffman lit, dist;
            if (!build(lit, lengths.data(), nlen) || !build(dist, lengths.data() + nlen, ndist) ||
                !codes(in, lit, dist))
                return Error::fail("Damaged deflate data");
        } else {
            return Error::fail("Damaged deflate data");
        }
    }
    consumed = in.pos;
    return Error::success();
}

} // namespace reluva
