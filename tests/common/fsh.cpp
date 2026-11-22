// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// EA's picture banks (image/fsh.h): banks written by hand, with every kind
// of picture, palettes and text, compressed whole, and damaged.
//
//   reluva-tests common.unit.fsh

#include "image/fsh.h"
#include "tests/support/fsh_writer.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace reluva;
using namespace reluva::image;
using reluva::test::makeShapeBank;
using reluva::test::putShapeHeader;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(Bytes &b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

void putText(Bytes &b, const char *s, size_t n) {
    b.insert(b.end(), s, s + n);
}

void testShapes() {
    // 8-bit, 2x1, with a 16-bit palette of 2 colours and a text block.
    Bytes indexed;
    putShapeHeader(indexed, 0x7b, 16 + 4, 2, 1, -256, 3, 10, -20); // pixels padded to 4
    indexed.insert(indexed.end(), {1, 0, 0, 0});
    putShapeHeader(indexed, 0x2d, 16 + 4, 2, 1, 0, 0, 0, 0);
    put16(indexed, 0x7c00);          // red, alpha bit clear
    put16(indexed, 0x8000 | 0x03e0); // green, opaque
    indexed.push_back(0x6f);
    indexed.insert(indexed.end(), {0, 0, 0});
    put32(indexed, 12);
    putText(indexed, "<additive>\r\n", 12);

    // 16-bit 5:6:5, 1x1, with an unknown block.
    Bytes rgb565;
    putShapeHeader(rgb565, 0x78, 18, 1, 1, 0, 0, 0, 0);
    put16(rgb565, 0x001f); // blue
    rgb565.insert(rgb565.end(), {0x7c, 0, 0, 0, 2, 0, 0, 0});

    // 32-bit (B, G, R, A) and 24-bit (B, G, R), 1x1.
    Bytes argb;
    putShapeHeader(argb, 0x7d, 0, 1, 1, 0, 0, 0, 0);
    argb.insert(argb.end(), {0x10, 0x20, 0x30, 0x40});
    Bytes rgb;
    putShapeHeader(rgb, 0x7f, 0, 1, 1, 0, 0, 0, 0);
    rgb.insert(rgb.end(), {0x10, 0x20, 0x30});

    const Bytes file = makeShapeBank({{"pal8", indexed}, {"r565", rgb565}, {"argb", argb}, {"rgb", rgb}});
    ShapeBank bank;
    CHECK(!parseShapeBank(file, bank));
    CHECK(bank.id == "GIMX");
    CHECK(bank.shapes.size() == 4);
    if (bank.shapes.size() != 4)
        return;

    const Shape &s = bank.shapes[0];
    CHECK(s.name == "pal8" && s.type == kShape8Bit && s.width == 2 && s.height == 1);
    CHECK(s.centerX == -256 && s.centerY == 3 && s.x == 10 && s.y == -20);
    CHECK(s.pixels.size() == 2);
    CHECK(s.palette.size() == 8);
    CHECK(s.texts.size() == 1 && s.texts[0] == "<additive>");
    image::Image img;
    CHECK(!decodeShape(s, img));
    CHECK(img.w == 2 && img.h == 1);
    const uint8_t pal8[] = {0, 255, 0, 255, 255, 0, 0, 0};
    CHECK(img.rgba.size() == 8 && std::memcmp(img.rgba.data(), pal8, 8) == 0);

    CHECK(bank.shapes[1].unknown.size() == 1 && bank.shapes[1].unknown[0].type == 0x7c); // not read here
    CHECK(!decodeShape(bank.shapes[1], img));
    const uint8_t blue[] = {0, 0, 255, 255};
    CHECK(std::memcmp(img.rgba.data(), blue, 4) == 0);
    CHECK(!decodeShape(bank.shapes[2], img));
    const uint8_t fromArgb[] = {0x30, 0x20, 0x10, 0x40};
    CHECK(std::memcmp(img.rgba.data(), fromArgb, 4) == 0);
    CHECK(!decodeShape(bank.shapes[3], img));
    const uint8_t fromRgb[] = {0x30, 0x20, 0x10, 0xff};
    CHECK(std::memcmp(img.rgba.data(), fromRgb, 4) == 0);

    // Compressed: a RefPack stream of only literals holding the file.
    Bytes packed = {0x10, 0xfb, uint8_t(file.size() >> 16), uint8_t(file.size() >> 8), uint8_t(file.size())};
    size_t done = 0;
    while (file.size() - done >= 4) {
        const size_t n = std::min<size_t>((file.size() - done) & ~size_t(3), 112);
        packed.push_back(uint8_t(0xe0 | ((n - 4) >> 2)));
        packed.insert(packed.end(), file.begin() + done, file.begin() + done + n);
        done += n;
    }
    packed.push_back(uint8_t(0xfc | (file.size() - done)));
    packed.insert(packed.end(), file.begin() + done, file.end());
    ShapeBank fromQfs;
    CHECK(!parseShapeBank(packed, fromQfs));
    CHECK(fromQfs.shapes.size() == 4);

    // A picture larger than its block fails; an 8-bit one without a
    // palette reads but doesn't decode.
    Bytes tooBig;
    putShapeHeader(tooBig, 0x7d, 0, 2, 2, 0, 0, 0, 0);
    tooBig.insert(tooBig.end(), 4, 0);
    CHECK(parseShapeBank(makeShapeBank({{"big", tooBig}}), bank));
    Bytes noPalette;
    putShapeHeader(noPalette, 0x7b, 0, 1, 1, 0, 0, 0, 0);
    noPalette.push_back(0);
    CHECK(!parseShapeBank(makeShapeBank({{"nopl", noPalette}}), bank));
    CHECK(decodeShape(bank.shapes[0], img));
    CHECK(parseShapeBank(Bytes{'S', 'H', 'P', 'X'}, bank));
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.fsh",
                                        {
                                            {"shapes", [](const std::filesystem::path &) { testShapes(); }},
                                        });
