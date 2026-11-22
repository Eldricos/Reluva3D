// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// EA's compression (common/refpack.h): RefPack and EA Huffman streams
// written by hand.
//
//   reluva-tests common.unit.refpack

#include "common/refpack.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

using Bytes = std::vector<uint8_t>;

void testRefPack() {
    // The three copy forms and both literal forms, by hand (docs/common/refpack.md).
    const Bytes small = {0x10, 0xfb, 0x00, 0x00, 0x0c, 0xe0, 'a', 'b', 'c', 'd', // 4 literals
                         0x01, 0x04, 'e',                                        // 1 literal, then 3 from 5 back
                         0x80, 0x00, 0x00,                                       // 4 from 1 back (overlapping)
                         0xfc};                                                  // end, no literals
    Bytes out;
    CHECK(isRefPack(small));
    CHECK(!decompressRefPack(small, out));
    CHECK(std::string(out.begin(), out.end()) == "abcdeabccccc");

    // The 4-byte form, from further back than the 3-byte form reaches.
    Bytes big = {0x10, 0xfb, 0x00, 0x43, 0x94}; // 17300 bytes
    Bytes expect;
    for (int i = 0; i < 17000; ++i)
        expect.push_back(uint8_t(i * 7));
    size_t done = 0;
    while (17000 - done >= 112) { // 0xfb: 112 literals
        big.push_back(0xfb);
        big.insert(big.end(), expect.begin() + done, expect.begin() + done + 112);
        done += 112;
    }
    while (17000 - done >= 4) { // 0xe0: 4 literals
        big.push_back(0xe0);
        big.insert(big.end(), expect.begin() + done, expect.begin() + done + 4);
        done += 4;
    }
    CHECK(done == 17000);
    const uint32_t distance = 17000 - 1;
    const uint32_t length = 300 - 5;
    big.push_back(uint8_t(0xc0 | (distance >> 16) << 4 | (length >> 8) << 2));
    big.push_back(uint8_t(distance >> 8));
    big.push_back(uint8_t(distance));
    big.push_back(uint8_t(length));
    big.push_back(0xfc);
    expect.insert(expect.end(), expect.begin(), expect.begin() + 300);
    CHECK(!decompressRefPack(big, out));
    CHECK(out == expect);

    // The flags: 0x01 adds a compressed size before the size.
    Bytes sized = {0x11, 0xfb, 0x00, 0x00, 0x09, 0x00, 0x00, 0x02, 0xfe, 'h', 'i'};
    CHECK(!decompressRefPack(sized, out));
    CHECK(std::string(out.begin(), out.end()) == "hi");

    // Damage is caught.
    Bytes cut(small.begin(), small.end() - 1);
    CHECK(decompressRefPack(cut, out)); // no end code
    Bytes wrongSize = small;
    wrongSize[4] = 0x0d;
    CHECK(decompressRefPack(wrongSize, out));
    Bytes tooFar = {0x10, 0xfb, 0x00, 0x00, 0x05, 0xe0, 'a', 'b', 'c', 'd', 0x00, 0x09, 0xfc};
    CHECK(decompressRefPack(tooFar, out)); // copy from 10 back with 4 written
    CHECK(!isRefPack(Bytes{'S', 'H', 'P', 'I', 0}));
    // EA Huffman (docs/common/refpack.md), built by hand: A coded 0, B 10, the escape
    // (C) 11; A, B, then the escape repeating B twice, then the end.
    const Bytes huffman = {0x30, 0xfb, 0x00, 0x00, 0x04, 0x43, 0xb8, 0x22, 0xc8, 0xbd, 0xc8};
    CHECK(isEaHuffman(huffman) && !isRefPack(huffman) && !isOtherEaCompression(huffman));
    CHECK(!decompressEaHuffman(huffman, out));
    CHECK(out == (Bytes{'A', 'B', 'B', 'B'}));
    // As differences (32 FB): each byte the sum of those so far.
    Bytes summed = huffman;
    summed[0] = 0x32;
    CHECK(!decompressEaHuffman(summed, out));
    CHECK(out == (Bytes{'A', 0x83, 0xc5, 0x07}));
    Bytes wrong = huffman;
    wrong[4] = 0x05;
    CHECK(decompressEaHuffman(wrong, out)); // not the size it says
    Bytes cutShort(huffman.begin(), huffman.end() - 2);
    CHECK(decompressEaHuffman(cutShort, out));
    const Bytes other = {0x46, 0xfb, 0x00, 0x00, 0x04, 0x00};
    CHECK(isOtherEaCompression(other));
    CHECK(!isOtherEaCompression(small));
    Bytes copy = other;
    CHECK(unpackIfRefPack(copy)); // fails rather than passing it on

    Bytes plain = {'S', 'H', 'P', 'I', 0};
    CHECK(!unpackIfRefPack(plain));
    CHECK(plain.size() == 5);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.refpack",
                                        {
                                            {"ref pack", [](const std::filesystem::path &) { testRefPack(); }},
                                        });
