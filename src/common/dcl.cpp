// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/dcl.h"

namespace reluva {

namespace {

const int kMaxBits = 13;

// Code lengths, run-length coded as in blast.c: each byte is (count - 1) << 4 | length.
const uint8_t kLitLen[] = {11, 124, 8,   7,   28,  7,   188, 13, 76, 4,  10, 8,  12, 10, 12, 10, 8,  23, 8,  9,
                           7,  6,   7,   8,   7,   6,   55,  8,  23, 24, 12, 11, 7,  9,  11, 12, 6,  7,  22, 5,
                           7,  24,  6,   11,  9,   6,   7,   22, 7,  11, 38, 7,  9,  8,  25, 11, 8,  11, 9,  12,
                           8,  12,  5,   38,  5,   38,  5,   11, 7,  5,  6,  21, 6,  10, 53, 8,  7,  24, 10, 27,
                           44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45, 44, 173};
const uint8_t kLenLen[] = {2, 35, 36, 53, 38, 23};
const uint8_t kDistLen[] = {2, 20, 53, 230, 247, 151, 248};
const uint16_t kBase[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
const uint8_t kExtra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

struct Huffman {
    uint16_t count[kMaxBits + 1];
    uint16_t symbol[256];

    void construct(const uint8_t *rep, int n) {
        uint8_t length[256];
        int symbols = 0;
        for (int i = 0; i < n; i++) {
            int len = rep[i] & 15;
            for (int left = (rep[i] >> 4) + 1; left > 0; left--)
                length[symbols++] = uint8_t(len);
        }
        for (int len = 0; len <= kMaxBits; len++)
            count[len] = 0;
        for (int s = 0; s < symbols; s++)
            count[length[s]]++;
        uint16_t offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; len++)
            offs[len + 1] = uint16_t(offs[len] + count[len]);
        for (int s = 0; s < symbols; s++)
            if (length[s])
                symbol[offs[length[s]]++] = uint16_t(s);
    }
};

class Decoder {
public:
    Decoder(const uint8_t *src, uint32_t size) : src_(src), size_(size) {}

    // Returns -1 when the input runs out.
    int bits(int need) {
        uint32_t val = bitBuf_;
        while (bitCnt_ < need) {
            if (pos_ >= size_)
                return -1;
            val |= uint32_t(src_[pos_++]) << bitCnt_;
            bitCnt_ += 8;
        }
        bitBuf_ = val >> need;
        bitCnt_ -= need;
        return int(val & ((1u << need) - 1));
    }

    // Codes are stored bit-inverted, first bit first (blast.c's decode()).
    int decode(const Huffman &h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            int bit = bits(1);
            if (bit < 0)
                return -1;
            code |= bit ^ 1;
            int count = h.count[len];
            if (code < first + count)
                return h.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -2;
    }

private:
    const uint8_t *src_;
    uint32_t size_, pos_ = 0;
    uint32_t bitBuf_ = 0;
    int bitCnt_ = 0;
};

} // namespace

bool decompressDCL(const uint8_t *src, uint32_t srcSize, std::vector<uint8_t> &out) {
    static Huffman litCode, lenCode, distCode;
    static bool built = false;
    if (!built) {
        litCode.construct(kLitLen, sizeof(kLitLen));
        lenCode.construct(kLenLen, sizeof(kLenLen));
        distCode.construct(kDistLen, sizeof(kDistLen));
        built = true;
    }

    Decoder in(src, srcSize);
    int lit = in.bits(8);
    int dict = in.bits(8);
    if (lit < 0 || lit > 1 || dict < 4 || dict > 6)
        return false;

    for (;;) {
        int flag = in.bits(1);
        if (flag < 0)
            return false;
        if (flag) {
            int sym = in.decode(lenCode);
            if (sym < 0)
                return false;
            int extra = in.bits(kExtra[sym]);
            if (extra < 0)
                return false;
            int len = kBase[sym] + extra;
            if (len == 519)
                return true; // end code
            int distBits = len == 2 ? 2 : dict;
            int dsym = in.decode(distCode);
            int low = in.bits(distBits);
            if (dsym < 0 || low < 0)
                return false;
            uint32_t dist = (uint32_t(dsym) << distBits) + uint32_t(low) + 1;
            if (dist > out.size())
                return false;
            size_t from = out.size() - dist;
            for (int i = 0; i < len; i++) {
                uint8_t b = out[from + i]; // copy first: push_back may reallocate
                out.push_back(b);
            }
        } else {
            int sym = lit ? in.decode(litCode) : in.bits(8);
            if (sym < 0)
                return false;
            out.push_back(uint8_t(sym));
        }
    }
}

} // namespace reluva
