// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's postcards: a card built here read back, damaged ones
// refused, and the clip art names a card's pictures stand for.

#include "engines/legoloco/formats/postcard.h"

#include "tests/support/test.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

void put(std::vector<uint8_t> &d, size_t at, const std::string &s) {
    std::memcpy(d.data() + at, s.c_str(), s.size() + 1);
}

// As the game saves one: its unused bytes the debug heap's 0xCD.
std::vector<uint8_t> card() {
    std::vector<uint8_t> d(kPostcardSize, 0xcd);
    d[0] = 0x66, d[1] = 0;
    d[4] = 0x5d, d[5] = d[6] = d[7] = 0;
    d[8] = 0x11, d[9] = 0x27, d[10] = d[11] = 0; // 10001
    put(d, 0x0c, "Bea");
    put(d, 0x21, "Player");
    d[0x36] = d[0x37] = 0;
    d[0x38] = 1, d[0x39] = d[0x3a] = d[0x3b] = 0;
    put(d, 0x3f, "Hello");
    d[0x8f] = 30;             // stamp S0030
    d[0x90] = 2, d[0x91] = 1; // background R0002
    for (size_t i = 0; i < 128; ++i)
        d[0x92 + 6 * i] = d[0x93 + 6 * i] = 0; // every slot free
    const uint8_t stickers[2][6] = {{0x69, 4, 0x86, 72, 0x53, 70}, {0x80, 10, 0x4d, 48, 0x11, 24}};
    for (size_t i = 0; i < 2; ++i)
        std::memcpy(d.data() + 0x92 + 6 * i, stickers[i], 6);
    return d;
}

void testRead() {
    Postcard c;
    CHECK(!parsePostcard(card(), c));
    CHECK(c.number == 10001 && c.to == "Bea" && c.from == "Player" && c.message == "Hello");
    CHECK(c.field04 == 0x5d && c.field38 == 1);
    CHECK(c.stamp == 30 && c.background == 2 && c.backgroundSet == 1);
    CHECK(c.stickers.size() == 2);
    const PostcardSticker &s = c.stickers[0];
    CHECK(s.set == 0x69 && s.number == 4 && s.x == 268 && s.y == 166 && s.width == 72 && s.height == 70);
    CHECK(c.stickers[1].x == 154 && c.stickers[1].y == 34);
    // Damaged: the wrong size, or not marked as a card.
    std::vector<uint8_t> d = card();
    d.pop_back();
    CHECK(bool(parsePostcard(d, c)));
    d = card();
    d[0] = 0x65;
    CHECK(bool(parsePostcard(d, c)));
}

// The game's names, by category: a hex digit, two runs of letters, the
// backgrounds' R and the stamps' S0.
void testClipArtNames() {
    CHECK(clipArtName(0x00, 6) == "00006.bmp");
    CHECK(clipArtName(0x69, 4) == "d1004.bmp");
    CHECK(clipArtName(clipArtSet(16, 0), 10) == "h0010.bmp");
    CHECK(clipArtName(clipArtSet(25, 0), 1) == "q0001.bmp");
    CHECK(clipArtName(clipArtSet(26, 2), 5) == "t2005.bmp");
    CHECK(clipArtName(clipArtSet(30, 1), 1) == "R1001.bmp");
    CHECK(clipArtName(clipArtSet(31, 0), 30) == "S0030.bmp");
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.postcards", {
                                          {"read", [](const std::filesystem::path &) { testRead(); }},
                                          {"clip art names", [](const std::filesystem::path &) { testClipArtNames(); }},
                                      });
