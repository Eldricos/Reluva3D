// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// PlayStation MDEC frames, version 2 (video/mdec.h), on frames built here.
//
//   reluva-tests common.unit.mdec

#include "video/mdec.h"
#include "tests/support/test.h"

#include <cstdint>
#include <vector>

using namespace reluva;

namespace {

// Bits written as MDEC reads them: 16-bit little-endian words, each
// filled from its top bit.
struct WordBits {
    std::vector<uint16_t> words;
    int used = 16;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if (used == 16) {
                words.push_back(0);
                used = 0;
            }
            words.back() |= static_cast<uint16_t>(((v >> i) & 1) << (15 - used++));
        }
    }
    void dc(int v) { put(static_cast<uint32_t>(v) & 0x3ff, 10); }
    void end() { put(2, 2); }
};

std::vector<uint8_t> frame(const WordBits &bits, int scale, int version = 2) {
    std::vector<uint8_t> out;
    for (int v : {static_cast<int>(bits.words.size()), 0x3800, scale, version}) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    for (uint16_t w : bits.words) {
        out.push_back(static_cast<uint8_t>(w));
        out.push_back(static_cast<uint8_t>(w >> 8));
    }
    return out;
}

void testColumns() {
    // Four flat macroblocks, 32x32: a DC of d is d * 2 / 8 after the
    // transform, so luma 128 + d / 4. Order: down the first column, then
    // the second.
    WordBits bits;
    const int luma[4] = {0, 16, 32, 48};
    for (int d : luma) {
        bits.dc(0);
        bits.end(); // Cr
        bits.dc(-128);
        bits.end(); // Cb: -32
        for (int k = 0; k < 4; ++k) {
            bits.dc(d);
            bits.end();
        }
    }
    mdec::Decoder decoder;
    const auto data = frame(bits, 1);
    CHECK(!decoder.decode(data.data(), data.size(), 32, 32));
    CHECK(decoder.width() == 32 && decoder.height() == 32 && decoder.quantiserScale() == 1);
    const auto &y = decoder.plane(0);
    CHECK(y[0] == 128 && y[15 * 32 + 15] == 128);
    CHECK(y[16 * 32] == 132);            // second: the bottom left
    CHECK(y[15] == 128 && y[16] == 136); // third: the top right
    CHECK(y[31 * 32 + 31] == 140);
    CHECK(decoder.plane(1)[0] == -32 && decoder.plane(2)[0] == 0);

    Surface picture;
    decoder.toSurface(picture);
    CHECK(picture.w == 32 && picture.h == 32);
    // Cb -32 (toward yellow): blue 128 - 1.772 * 32, green 128 + 0.344 * 32.
    const uint8_t *p = picture.row(0);
    CHECK(p[0] == 128 && p[1] == 139 && p[2] == 71 && p[3] == 255);
}

void testAcAndErrors() {
    // One macroblock; the top left luma block has an escape: run 0, level
    // 16, so coefficient 1 (across) is 16 * scale 2 * matrix 16 >> 3 = 64:
    // a cosine across, the same in every row.
    WordBits bits;
    for (int k = 0; k < 6; ++k) {
        bits.dc(0);
        if (k == 2) {
            bits.put(1, 6); // escape
            bits.put(0, 6);
            bits.put(16, 10);
        }
        bits.end();
    }
    mdec::Decoder decoder;
    auto data = frame(bits, 2);
    CHECK(!decoder.decode(data.data(), data.size(), 16, 16));
    const auto &y = decoder.plane(0);
    CHECK(y[0] == 139 && y[3] == 130 && y[4] == 126 && y[7] == 117);
    CHECK(y[7 * 16] == y[0] && y[7 * 16 + 7] == y[7]);
    CHECK(y[8] == 128); // the next block: flat

    // Too short: the blocks run out of bits.
    WordBits shortBits;
    shortBits.dc(0);
    shortBits.end();
    auto cut = frame(shortBits, 2);
    CHECK(decoder.decode(cut.data(), cut.size(), 16, 16));
    CHECK(decoder.decode(data.data(), data.size(), 24, 16)); // not a multiple of 16
    auto version3 = frame(bits, 2, 3);
    CHECK(decoder.decode(version3.data(), version3.size(), 16, 16));
    data[3] = 0x39;
    CHECK(decoder.decode(data.data(), data.size(), 16, 16));
}

} // namespace

static const test::Suite kSuite("common.unit.mdec",
                                {
                                    {"columns", [](const std::filesystem::path &) { testColumns(); }},
                                    {"ac and errors", [](const std::filesystem::path &) { testAcAndErrors(); }},
                                });
