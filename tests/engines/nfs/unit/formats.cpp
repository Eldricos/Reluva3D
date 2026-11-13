// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

//   reluva-tests nfs.unit.formats

#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {

void testRefPack() {
    // The three copy forms and both literal forms, by hand (refpack.md).
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
    const uint32_t distance = 17000 - 1, length = 300 - 5;
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
    // EA Huffman (refpack.md), built by hand: A coded 0, B 10, the escape
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

void testViv() {
    VivArchive viv;
    CHECK(!viv.open(makeViv({{"carp.txt", "Serial Number"}, {"car.fce", "abc"}}), "test.viv"));
    CHECK(viv.entries().size() == 2);
    CHECK(viv.list() == (std::vector<std::string>{"carp.txt", "car.fce"}));
    CHECK(viv.has("CAR.FCE"));
    CHECK(!viv.has("car.tga"));
    Bytes data;
    CHECK(!viv.read("Carp.TXT", data));
    CHECK(std::string(data.begin(), data.end()) == "Serial Number");
    CHECK(!viv.read("car.fce", data) && data.size() == 3);
    CHECK(viv.read("car.tga", data));
    CHECK(viv.describe() == "test.viv");

    // An empty archive is fine; damage isn't.
    CHECK(!viv.open(makeViv({}), "empty.viv"));
    CHECK(viv.entries().empty());
    Bytes bad = makeViv({{"a", "1234"}});
    bad[0] = 'X';
    CHECK(viv.open(bad, "bad.viv"));
    Bytes past = makeViv({{"a", "1234"}});
    past[19] = 0x40; // the file's offset puts it past the end
    CHECK(viv.open(past, "past.viv"));
    Bytes unterminated = makeViv({{"abc", "1234"}});
    unterminated.resize(27); // cut inside the name
    unterminated[15] = 27;
    CHECK(viv.open(unterminated, "cut.viv"));

    // Read from disk too.
    const stdfs::path file = stdfs::temp_directory_path() / "reluva_nfs_test.viv";
    const Bytes disk = makeViv({{"x.bin", "xyz"}});
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char *>(disk.data()), std::streamsize(disk.size()));
    CHECK(!viv.open(file));
    CHECK(viv.has("x.bin"));
    stdfs::remove(file);
    CHECK(viv.open(file));
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

    CHECK(bank.shapes[1].unknown.size() == 1 && bank.shapes[1].unknown[0].type == 0x7c);
    CHECK(!decodeShape(bank.shapes[1], img));
    const uint8_t blue[] = {0, 0, 255, 255};
    CHECK(std::memcmp(img.rgba.data(), blue, 4) == 0);
    CHECK(!decodeShape(bank.shapes[2], img));
    const uint8_t fromArgb[] = {0x30, 0x20, 0x10, 0x40};
    CHECK(std::memcmp(img.rgba.data(), fromArgb, 4) == 0);
    // As nfs3.exe gives it to the card (0x4d37a0): an alpha of 0x40 makes it
    // 4:4:4:4, each channel's top nibble x 17 (0x4d27e0); the 24-bit one
    // (no alpha) stays; asked for 4:4:4:4, a 32-bit one goes so anyway.
    CHECK(!decodeForGlide(bank.shapes[2], img));
    const uint8_t argb4444[] = {0x33, 0x22, 0x11, 0x44};
    CHECK(std::memcmp(img.rgba.data(), argb4444, 4) == 0);
    CHECK(!decodeForGlide(bank.shapes[3], img, true));
    const uint8_t rgbKept[] = {0x30, 0x20, 0x10, 0xff};
    CHECK(std::memcmp(img.rgba.data(), rgbKept, 4) == 0);
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

// A cockpit's gauges block (fsh.md, Gauges) and its needles.
void testCockpit() {
    Shape dash;
    Cockpit c;
    CHECK(!readCockpit(dash, c));
    // Colour texel (5, 6), width 4; a speedometer about (100, 100), hub 0,
    // km/h, 0-200, from the left (90, 100) to the right (110, 100); no
    // tachometer.
    const int32_t v[24] = {5, 6, 4, 0, 100, 100, 0, 1, 0, 200, 90, 100, 110, 100};
    Shape::Block b{kGauges, {}};
    put32(b.data, 12);
    for (int32_t x : v)
        put32(b.data, uint32_t(x));
    dash.unknown.push_back(b);
    CHECK(readCockpit(dash, c));
    CHECK(c.colorX == 5 && c.colorY == 6 && c.width == 4);
    CHECK(c.speedometer.shown() && c.speedometer.kmh && c.speedometer.high == 200 && !c.tachometer.shown());
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // Half way it points up: the sweep turns clockwise, over the top.
    Needle n = needle(c.speedometer, c.width, 100);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 90));
    CHECK(near(n.x[0] - n.x[1], 4) && near(n.x[3] - n.x[2], 2)); // base 4 wide, tip 2
    // At the ends, toward p0 and p1; past them it turns on, as the game's.
    n = needle(c.speedometer, c.width, 0);
    CHECK(near((n.x[2] + n.x[3]) / 2, 90) && near((n.y[2] + n.y[3]) / 2, 100));
    n = needle(c.speedometer, c.width, 200);
    CHECK(near((n.x[2] + n.x[3]) / 2, 110) && near((n.y[2] + n.y[3]) / 2, 100));
    n = needle(c.speedometer, c.width, 300);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 110));
    // A range from high to low sweeps the other way, under the bottom.
    Gauge reversed = c.speedometer;
    std::swap(reversed.low, reversed.high);
    n = needle(reversed, c.width, 100);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 110));
    // The wheel: a quarter turn at full lock standing still, none at 800/2.2369 m/s.
    CHECK(near(wheelTurns(128, 0), 0.25f) && near(wheelTurns(-64, 0), -0.125f) &&
          near(wheelTurns(128, 800 / 2.2369f / 2), 0));
    CHECK(near(metresPerSecond(c.speedometer, 36), 10));
    // Fewer than 12 pairs: not read.
    dash.unknown[0].data.resize(40);
    CHECK(!readCockpit(dash, c));
}

void testTextTable() {
    // Three offsets, then the strings; the last without its 0.
    Bytes b;
    put32(b, 12);
    put32(b, 13);
    put32(b, 25);
    b.push_back(0);
    putText(b, "Skill Level", 12);
    putText(b, "Abilit\xe0 \x85", 9);
    TextTable table;
    CHECK(!parseTextTable(b, table));
    CHECK(table.strings.size() == 3);
    if (table.strings.size() == 3) {
        CHECK(table.strings[0].empty());
        CHECK(table.strings[1] == "Skill Level");
        CHECK(table.strings[2] == "Abilit\xe0 \x85");
        CHECK(cp1252ToUtf8(table.strings[2]) == "Abilit\xc3\xa0 \xe2\x80\xa6"); // à, …
    }
    CHECK(cp1252ToUtf8("\x80\x81\x99") == "\xe2\x82\xac\xef\xbf\xbd\xe2\x84\xa2"); // €, unassigned, ™

    Bytes outside = b;
    outside[4] = 200;
    CHECK(parseTextTable(outside, table));
    Bytes odd = b;
    odd[0] = 13;
    CHECK(parseTextTable(odd, table));
    CHECK(parseTextTable(Bytes{1, 0}, table));
}

void testFont() {
    // Two glyphs over a 4x2 atlas of 4-bit pixels, high nibble first.
    Bytes b = {'F', 'N', 'T', 'F'};
    put32(b, 0);
    put16(b, 100);
    put16(b, 2); // glyphs
    put16(b, 1); // flags
    put16(b, 0);
    put16(b, 0);
    b.push_back(9); // line height
    b.push_back(0);
    put16(b, 32);
    b.resize(28, 0);
    const uint32_t atlas = 32 + 3 * 11; // a table with room for 3
    put32(b, atlas);
    // 'A': 2x2 at (0, 0), advance 3, offsets 1, -1. 'B': 1x1 at (3, 1).
    b.insert(b.end(), {'A', 0, 2, 2, 0, 0, 0, 0, 3, 1, 0xff});
    b.insert(b.end(), {'B', 0, 1, 1, 3, 0, 1, 0, 2, 0, 0});
    b.insert(b.end(), {0xad, 0xad, 9, 9, 9, 9, 9, 9, 9, 9, 9}); // filler past the count
    putShapeHeader(b, 0x7a, 0, 4, 2, 0, 0, 0, 0);
    b.insert(b.end(), {0xf8, 0x00, 0x0f, 0x0f});

    Font font;
    CHECK(!parseFont(b, font));
    CHECK(font.lineHeight == 9 && font.flags == 1);
    CHECK(font.atlasWidth == 4 && font.atlasHeight == 2);
    CHECK(font.coverage == (Bytes{255, 136, 0, 0, 0, 255, 0, 255}));
    CHECK(font.has('A') && font.has('B') && !font.has('C') && !font.has(0xad));
    CHECK(font.glyphs['A'].advance == 3 && font.glyphs['A'].offsetX == 1 && font.glyphs['A'].offsetY == -1);
    CHECK(font.glyphs['B'].x == 3 && font.glyphs['B'].y == 1);
    CHECK(font.measure("AB") == 5);

    // Drawn: 'A' at (0 + 1, 2 - 1), 'B' at (3, 2).
    Surface s;
    s.create(6, 4, PixelFormat::RGBA32);
    CHECK(font.draw(s, 0, 2, "AB", {200, 100, 50}) == 5);
    CHECK(s.row(1)[4] == 200 && s.row(1)[5] == 100 && s.row(1)[6] == 50); // full coverage
    CHECK(s.row(1)[8] == 200 * 136 / 255);                                // partial
    CHECK(s.row(2)[4 + 1] == 0 && s.row(2)[8] == 200);                    // (1, 2) clear, (2, 2) solid
    CHECK(s.row(2)[3 * 4] == 200);                                        // 'B'

    CHECK(font.penX == 0 && font.penY == 0 && font.lineStep() == 9);

    // A pen offset of (1, -1) moves everything; the gap adds to a line's step.
    Bytes moved = b;
    moved[16] = 1;
    moved[17] = 0xff;
    moved[19] = 2;
    CHECK(!parseFont(moved, font));
    CHECK(font.penX == 1 && font.penY == -1 && font.lineGap == 2 && font.lineStep() == 11);
    Surface m;
    m.create(6, 4, PixelFormat::RGBA32);
    CHECK(font.draw(m, 0, 3, "AB", {200, 100, 50}) == 5);
    CHECK(m.row(1)[2 * 4] == 200 && m.row(2)[4 * 4] == 200); // 'A' at (2, 1), 'B' at (4, 2)

    Bytes outside = b;
    outside[32 + 4] = 3; // 'A' at x 3 runs off the atlas
    CHECK(parseFont(outside, font));
    Bytes wrongType = b;
    wrongType[atlas] = 0x7b;
    CHECK(parseFont(wrongType, font));
    CHECK(parseFont(Bytes(b.begin(), b.end() - 1), font)); // atlas cut short
}

void testModel() {
    // The parts each detail draws (0x4bb4c0): a car of 13 parts.
    {
        const CarDetailParts near = carDetailParts(0, 13), mid = carDetailParts(1, 13), small = carDetailParts(2, 13),
                             tiny = carDetailParts(3, 13);
        CHECK(near.body == (std::vector<int>{0, 1, 2, 3, 4}) && near.headlights);
        CHECK(mid.body == (std::vector<int>{5, 6, 7, 8, 9}) && mid.headlights);
        CHECK(small.body == (std::vector<int>{10}) && !small.headlights && tiny.body == (std::vector<int>{11}) &&
              !tiny.headlights);
        // 12 parts: no headlights; traffic and police (6 parts): 0-4 then 5.
        CHECK(!carDetailParts(0, 12).headlights && carDetailParts(0, 12).body.size() == 5);
        CHECK(carDetailParts(2, 6).body == (std::vector<int>{0, 1, 2, 3, 4}) &&
              carDetailParts(3, 6).body == (std::vector<int>{5}));
        CHECK(carDetailParts(-1, 13).body.empty() && carDetailParts(4, 13).body.empty());
        // A two-sided triangle (flag 0x4) both ways round, the rest as stored.
        CHECK(carTriangleCorners(0) == (std::vector<int>{0, 1, 2}) &&
              carTriangleCorners(1) == (std::vector<int>{0, 1, 2}));
        CHECK(carTriangleCorners(5) == (std::vector<int>{0, 1, 2, 0, 2, 1}));
    }
    // Two parts: a triangle and a quad (two triangles), 7 vertices.
    constexpr size_t kData = 0x1f04;
    const uint32_t nV = 7, nT = 3;
    Bytes b(kData + 24 * nV + 56 * nT + 32 * nV + 12 * nV + 12 * nV, 0);
    putU32(b, 0x04, nT);
    putU32(b, 0x08, nV);
    putU32(b, 0x0c, 1);
    putU32(b, 0x10, 0);
    putU32(b, 0x14, 12 * nV);
    putU32(b, 0x18, 24 * nV);
    putFloat(b, 0x28, 1.0f);
    putU32(b, 0x34, 1); // a dummy
    putFloat(b, 0x38 + 8, 2.0f);
    std::memcpy(&b[0xa04], "HFLO", 4);
    putU32(b, 0xf8, 2);
    putFloat(b, 0xfc + 12 + 0, -0.8f); // part 1's position
    putU32(b, 0x3fc + 4, 3);           // first vertices 0, 3
    putU32(b, 0x4fc, 3);
    putU32(b, 0x4fc + 4, 4);
    putU32(b, 0x5fc + 4, 1); // first triangles 0, 1
    putU32(b, 0x6fc, 1);
    putU32(b, 0x6fc + 4, 2);
    putU32(b, 0x7fc, 1);
    for (int k = 0; k < 4; ++k)
        putU32(b, 0x800 + 4 * k, 10u + k);
    std::memcpy(&b[0xe04], "high body", 9);
    std::memcpy(&b[0xe44], "left front wheel", 16);
    for (uint32_t i = 0; i < nV; ++i) {
        putFloat(b, kData + 12 * i, float(i));
        putFloat(b, kData + 12 * nV + 12 * i + 4, 1.0f); // normals up
    }
    // Triangles: page, three part-local indices, 12 bytes, flags, u[3], v[3].
    const uint32_t tris[3][4] = {{0, 0, 1, 2}, {1, 0, 1, 2}, {1, 2, 3, 0}};
    for (uint32_t t = 0; t < nT; ++t) {
        const size_t at = kData + 24 * nV + 56 * t;
        for (int k = 0; k < 4; ++k)
            putU32(b, at + 4 * k, tris[t][k]);
        putU32(b, at + 28, t == 2 ? 0x80 : 1);
        putFloat(b, at + 32, 0.25f);
        putFloat(b, at + 44 + 8, 0.75f);
    }

    Model m;
    CHECK(!parseModel(b, m));
    CHECK(m.vertices.size() == nV && m.triangles.size() == nT && m.parts.size() == 2);
    if (m.parts.size() == 2 && m.triangles.size() == nT) {
        CHECK(m.parts[0].name == "high body" && m.parts[1].name == "left front wheel");
        CHECK(m.parts[1].position[0] == -0.8f);
        CHECK(m.parts[1].firstVertex == 3 && m.parts[1].vertexCount == 4 && m.parts[1].triangleCount == 2);
        // Part 1's triangles index the model's vertices: 3 + local.
        CHECK(m.triangles[0].vertices == (std::array<uint32_t, 3>{0, 1, 2}));
        CHECK(m.triangles[2].vertices == (std::array<uint32_t, 3>{5, 6, 3}));
        CHECK(m.triangles[1].texturePage == 1 && m.triangles[2].flags == 0x80);
        CHECK(m.triangles[1].u[0] == 0.25f && m.triangles[1].v[2] == 0.75f);
        CHECK(m.vertices[4][0] == 4.0f && m.normals[4][1] == 1.0f);
    }
    CHECK(m.halfSize[0] == 1.0f);
    CHECK(m.dummies.size() == 1 && m.dummies[0].name == "HFLO" && m.dummies[0].position[2] == 2.0f);
    CHECK(m.primaryColours.size() == 1 && m.primaryColours[0].hsbx[3] == 13);
    CHECK(m.secondaryColours.empty());

    Bytes outside = b;
    putU32(outside, kData + 24 * nV + 56 * 2 + 4, 4); // part 1 has 4 vertices: 0-3
    CHECK(parseModel(outside, m));
    Bytes tooMany = b;
    putU32(tooMany, 0x4fc + 4, 5);
    CHECK(parseModel(tooMany, m));
    CHECK(parseModel(Bytes(100, 0), m));
}

void testHorizon() {
    // Comments (one inside a line, as the retail files have), one fog
    // region, then the 48 values after the regions.
    std::string text = "/*Fog Color (r,g,b) and Density (%)*/\n134,134,140,\t5,\t70,\n/*regions*/\n1,\n"
                       "10,11,12, 5,\n20,21,22, 55,\n30,31,32, 70,\n150,\n175,\n200,\n";
    const int after[48] = {1,   1,     0,    0,    500,  -100, 250, 235, 208, 80,  10,  100, 0,   1,   1500, 0,
                           1,   -1200, 1800, 1200, 1350, 1150, 117, 106, 15,  207, 153, 98,  120, 182, 239,  223,
                           210, 151,   120,  171,  204,  0,    0,   0,   0,   0,   0,   0,   0,   100, 90,   80};
    for (int i = 0; i < 48; ++i) {
        text += std::to_string(after[i]) + ",";
        if (i == 33)
            text += "\t\t/* 255, 176, 61, at position of sun */\n";
    }
    Horizon h;
    CHECK(!parseHorizon(text, h));
    CHECK(h.fogColour == (Horizon::Rgb{134, 134, 140}) && h.fogDensity == 5 && h.fogOnPixmap == 70);
    CHECK(h.fogRegions.size() == 1);
    if (h.fogRegions.size() == 1)
        CHECK(h.fogRegions[0].apex.colour[2] == 22 && h.fogRegions[0].end.density == 70 &&
              h.fogRegions[0].afterSlice == 200);
    {
        // The fog by the camera's road slice (0x478e10): Lost Canyons' day
        // region, 850-910-1000 from 50 % (44, 28, 38) to 10 % black.
        Horizon lc;
        lc.fogColour = {44, 28, 38};
        lc.fogDensity = 50;
        Horizon::FogRegion r;
        r.beforeSlice = 850;
        r.centreSlice = 910;
        r.afterSlice = 1000;
        r.start = {{44, 28, 38}, 50};
        r.apex = {{0, 0, 0}, 10};
        r.end = {{0, 0, 0}, 20};
        lc.fogRegions = {r};
        CHECK(fogAtSlice(lc, 120).density == 50 && fogAtSlice(lc, 849).colour == (Horizon::Rgb{44, 28, 38}));
        CHECK(fogAtSlice(lc, 850).density == 50 && fogAtSlice(lc, 910).density == 10);
        const Horizon::FogPoint mid = fogAtSlice(lc, 880);
        CHECK(mid.density == 30 && mid.colour == (Horizon::Rgb{22, 14, 19}));
        CHECK(fogAtSlice(lc, 955).density == 15 && fogAtSlice(lc, 1000).density == 20 &&
              fogAtSlice(lc, 1001).density == 50);
    }
    CHECK(h.cloudType == 1 && h.visualHeight == 500 && h.cloudDomeOffset == (std::array<int, 2>{-100, 250}));
    CHECK(h.mirror && !h.blackHorizon && h.radius == 1500 && h.hasPixmap);
    CHECK(h.shadedBase == -1200 && h.shadedHeight == 1800 && h.pixmapBottom == 1150);
    CHECK(h.skyBaseSun == (Horizon::Rgb{223, 210, 151}) && h.skyBaseOpposite == (Horizon::Rgb{120, 171, 204}));
    CHECK(h.ambient == (Horizon::Rgb{100, 90, 80}));
    CHECK(parseHorizon(text + "7,", h));     // a number too many
    CHECK(parseHorizon("1,2,3,4,5,99,", h)); // regions that aren't there
    CHECK(parseHorizon("/* only a comment */", h));

    // The fog values the game sent for Hometown's day (density 5, pixmap
    // effect 70, radius 1500), and with the radius made 2000.
    CHECK(sceneryFogValue(5) == 475 && sceneryFogValue(0) == 0 && sceneryFogValue(100) == 5);
    Horizon hometown;
    hometown.fogOnPixmap = 70;
    hometown.radius = 1500;
    CHECK(skyFogValue(hometown, 5) == 2375);
    hometown.radius = 2000;
    CHECK(skyFogValue(hometown, 5) == 2850);
    hometown.fogOnPixmap = 100; // none on the pixmap: no fog for the sky
    CHECK(skyFogValue(hometown, 5) == 0);

    // The sky's layout, as the game drew Hometown's: the sky's colour at
    // the first slot edges, the cloud dome, the drift.
    hometown.skyBaseSun = {223, 210, 151};
    hometown.skyBaseOpposite = {120, 171, 204};
    const auto base = skyBaseColours(hometown);
    CHECK(base[0] == (Horizon::Rgb{223, 210, 151}) && base[1] == (Horizon::Rgb{210, 205, 157}));
    CHECK(base[15] == base[0] && base[8] == base[7]);
    CHECK(horizonTurn(hometown) == 0);
    hometown.rotation = 90;
    CHECK(std::fabs(horizonTurn(hometown) - 0.25f) < 1e-4f);
    hometown.rotation = -90;
    CHECK(std::fabs(horizonTurn(hometown) - 0.75f) < 1e-4f);

    // The sky's flat rows (0x479160, 0x479500): eight quads in a plane at
    // the radius along the forward on the ground, a slot's arc wide, from
    // 4 slots and the heading's fraction of one to the left.
    {
        Horizon flat;
        flat.radius = 1500;
        const float step = 1500 * 6.2831853f / 16;
        // Along +z, looking up: heading 0, so 16 x frac(0 + 0.5) = 8, no fraction.
        const SkyRow row = skyRow(flat, {0, 0.3f, 1}, -10, 600);
        CHECK(row.firstSlot == 8);
        CHECK(std::fabs(row.bottom[4][0]) < 1e-3f && std::fabs(row.bottom[4][2] - 1500) < 1e-3f);
        CHECK(std::fabs(row.bottom[0][0] + 4 * step) < 1e-2f && std::fabs(row.bottom[8][0] - 4 * step) < 1e-2f);
        CHECK(row.bottom[3][1] == -10 && row.top[3][1] == 590 && std::fabs(row.top[8][2] - 1500) < 1e-3f);
        // A quarter slot to the right: the row a quarter step to the left
        // (the right of +z is +x), the view's centre a quarter into slot 12.
        const float a = 6.2831853f / 64;
        const SkyRow turned = skyRow(flat, {std::sin(a), 0, std::cos(a)}, 0, 1);
        const float across = turned.bottom[4][0] * std::cos(a) - turned.bottom[4][2] * std::sin(a);
        const float ahead = turned.bottom[4][0] * std::sin(a) + turned.bottom[4][2] * std::cos(a);
        CHECK(turned.firstSlot == 8 && std::fabs(across + step / 4) < 1e-2f && std::fabs(ahead - 1500) < 1e-2f);
        // Along -x: heading 0.75, slot 16 x 0.25 = 4; with the rotation
        // 90, 16 x frac(1.5) = 8; nearer the next slot, rounded up.
        CHECK(skyRow(flat, {-2, 0, 0}, 0, 1).firstSlot == 4);
        flat.rotation = 90;
        CHECK(skyRow(flat, {-2, 0, 0}, 0, 1).firstSlot == 8);
        flat.rotation = 0;
        const float b = 6.2831853f * 0.7f / 16;
        const SkyRow late = skyRow(flat, {std::sin(b), 0, std::cos(b)}, 0, 1);
        CHECK(late.firstSlot == 9);
        CHECK(std::fabs(late.bottom[4][0] * std::cos(b) - late.bottom[4][2] * std::sin(b) - 0.3f * step) < 1e-2f);
        CHECK(skyRow(flat, {0, 1, 0}, 0, 1).firstSlot == 8); // straight up: along +z
        // The horizon picture's tiles (0x478bb0): twice round, the second
        // time mirrored with the mirror flag.
        bool flipped = true;
        CHECK(horizonTile(flat, 3, &flipped) == 3 && !flipped);
        CHECK(horizonTile(flat, 11, &flipped) == 3 && !flipped);
        flat.mirror = true;
        CHECK(horizonTile(flat, 7, &flipped) == 7 && !flipped);
        CHECK(horizonTile(flat, 8, &flipped) == 7 && flipped);
        CHECK(horizonTile(flat, 15, &flipped) == 0 && flipped);
        // No sky, black (0x47a4c0): the flag, or Lost Canyons' slices
        // 906-1064.
        CHECK(!skyBlack(flat, 5, 905) && skyBlack(flat, 5, 906) && skyBlack(flat, 5, 1064));
        CHECK(!skyBlack(flat, 5, 1065) && !skyBlack(flat, 0, 1000));
        flat.blackHorizon = true;
        CHECK(skyBlack(flat, 0, 0));
    }
    hometown.cloudDomeOffset = {-100, 250};
    CHECK(std::fabs(cloudHeight(hometown, 0) - 650) < 0.01f);
    CHECK(std::fabs(cloudHeight(hometown, 3000) + 100) < 0.01f);
    hometown.nimbus = 80; // below 100: no effect
    CHECK(std::fabs(cloudHeight(hometown, 0, 255) - 650) < 0.01f);
    hometown.nimbus = 100; // the brightest hang half the arch (750) lower
    CHECK(std::fabs(cloudHeight(hometown, 0, 255) - 275) < 0.01f);
    hometown.windSpeed = 10;
    hometown.windDirection = 90;
    const auto drift = cloudDrift(hometown);
    CHECK(std::fabs(drift[0]) < 1e-7f && std::fabs(drift[1] - 3e-4f) < 1e-7f);
    hometown.cloudBright = 235;
    hometown.cloudVariance = 208;
    const auto clouds = cloudBrightness(hometown, 1);
    CHECK(clouds == cloudBrightness(hometown, 1));
    CHECK(*std::max_element(clouds.begin(), clouds.end()) <= 235);
    CHECK(*std::max_element(clouds.begin(), clouds.end()) > *std::min_element(clouds.begin(), clouds.end()));
    // The game's generator (0x4e0dee, 0x4e0d88): seeded with 5992 its state
    // is the one dumped at the sky's set-up in run n3-runs/cl-v2.
    {
        GameRandom r(5992);
        CHECK(r.state[0] == 0xf22d25beu && r.state[1] == 0x88313e51u && r.state[5] == 0x6fdf52ccu);
        const uint32_t first = r.next();
        CHECK(r.state[0] == first && r.state[5] == 0x6fdf52cdu);
    }
    // The game's layouts, from its own seeds: Hometown's weather (250, 235)
    // seeded 5992 (told of a Voodoo 2) and 5991 (told of d3da) in the same
    // deterministic run, as the game left the points (n3-runs/cl-v2-pts1,
    // cl-d3d-pts1): 88 to 200, mean 161.3, and 56 to 201, mean 120.0. The
    // second is darker by its random amounts alone.
    {
        Horizon weather = hometown;
        weather.cloudBright = 250;
        weather.cloudVariance = 235;
        for (const auto &[seed, low, high, sum] :
             {std::tuple{5992u, 88, 200, 27258}, std::tuple{5991u, 56, 201, 20281}}) {
            const auto b = cloudBrightness(weather, seed);
            int total = 0;
            for (uint8_t v : b)
                total += v;
            CHECK(*std::min_element(b.begin(), b.end()) == low && *std::max_element(b.begin(), b.end()) == high);
            CHECK(std::abs(total - sum) <= 3);
        }
    }
    // The alpha (0x47b2e5): each point's brightness on a 3dfx card; with
    // Direct3D the brightness less half the variance at every point.
    CHECK(cloudAlpha(hometown, 150, false) == 150);
    CHECK(cloudAlpha(hometown, 150, true) == 235 - 104 && cloudAlpha(hometown, 20, true) == 131);
    // Without variance every point is the brightness pushed up from the
    // middle, (1 + sqrt(107 / 128)) x 127.5, and stays so: the passes only
    // hold a point to the brightness when it moves.
    Horizon even = hometown;
    even.cloudVariance = 0;
    const auto flat = cloudBrightness(even, 7);
    CHECK(std::all_of(flat.begin(), flat.end(), [](uint8_t v) { return v == 244; }));
    // The camera's place moves the clouds a tile a visual height (0x47b88a).
    even.visualHeight = 1000;
    const auto offset = cloudOffset(even, 500, -250);
    CHECK(std::fabs(offset[0] - 0.5f) < 1e-6f && std::fabs(offset[1] + 0.25f) < 1e-6f);
    even.visualHeight = 0;
    CHECK(cloudOffset(even, 500, 20)[0] == 0);

    // Lightning: on ticks that are a multiple of the second value; a bolt
    // of points 40 apart on the cloud dome, starting within 750, forking.
    SkyRandom random;
    hometown.lightning = 100;
    hometown.lightningChance = 64;
    CHECK(!lightningStarts(hometown, 63, random) && lightningStarts(hometown, 128, random));
    hometown.lightning = 0;
    CHECK(!lightningStarts(hometown, 128, random));
    const LightningBolt bolt = makeBolt(hometown, random);
    CHECK(bolt.points.size() > 40 && bolt.points.size() <= 240 && bolt.life == 24);
    CHECK(std::fabs(bolt.points[0].at[0]) <= 750 && std::fabs(bolt.points[0].at[2]) <= 750);
    bool onDome = true, apart = true;
    for (size_t i = 1; i < bolt.points.size(); ++i) {
        const auto &p = bolt.points[i];
        const auto &q = bolt.points[size_t(p.parent)];
        onDome = onDome && std::fabs(p.at[1] - cloudHeight(hometown, std::hypot(p.at[0], p.at[2]))) < 0.5f;
        apart = apart && std::fabs(std::hypot(p.at[0] - q.at[0], p.at[2] - q.at[2]) - 40) < 0.01f;
    }
    CHECK(onDome && apart);
    LightningBolt ticking = bolt;
    CHECK(boltFlash(ticking) == 255);
    for (int i = 0; i < 18; ++i)
        tickBolt(ticking, random);
    CHECK(ticking.life == 6 && boltFlash(ticking) == 127);
}

void testCollision() {
    Bytes b = {'C', 'O', 'L', 'L'};
    put32(b, 11);
    put32(b, 0); // size, unchecked
    put32(b, 4);
    const size_t table = b.size();
    b.resize(table + 16, 0);
    auto block = [&](int i, uint16_t id, uint16_t count, const Bytes &body) {
        putU32(b, table + 4 * i, uint32_t(b.size() - table));
        put32(b, uint32_t(8 + body.size()));
        put16(b, id);
        put16(b, count);
        b.insert(b.end(), body.begin(), body.end());
    };
    block(0, 2, 1, Bytes(8, 7));
    Bytes structure;
    put32(structure, 8 + 16 * 3 + 6);
    put16(structure, 3);
    put16(structure, 1);
    for (int v = 0; v < 3; ++v) {
        putF(structure, float(v));
        putF(structure, 0);
        putF(structure, 0);
        put32(structure, 0);
    }
    put16(structure, 5);
    structure.insert(structure.end(), {0, 1, 2, 2});
    block(1, 8, 1, structure);
    Bytes objects;
    put16(objects, 8 + 20);
    objects.push_back(3);
    objects.push_back(0);
    put16(objects, 1);
    put16(objects, 25);
    put32(objects, 874u << 16);
    put32(objects, 52u << 16);
    put32(objects, uint32_t(-(515 << 16)));
    for (int q : {0, 0, 0, 16384})
        put16(objects, unsigned(q));
    block(2, 7, 1, objects);
    Bytes road;
    put32(road, uint32_t(829.5 * 65536));
    put32(road, 13u << 16);
    put32(road, uint32_t(136.25 * 65536));
    put32(road, 0x550ff8);
    road.insert(road.end(), {0, 127, 0, 34, 0x55, 0xff, 0xa3, 16, 0xa2, 0, 0xab, 16});
    put32(road, uint32_t(23.5 * 65536));
    put32(road, 28u << 16);
    block(3, 15, 1, road);

    TrackCollision c;
    CHECK(!parseCollision(b, c));
    CHECK(c.textures.size() == 1 && c.textures[0][7] == 7);
    CHECK(c.structures.size() == 1 && c.structures[0].vertices.size() == 3 && c.structures[0].polygons.size() == 1);
    if (!c.structures.empty() && !c.structures[0].polygons.empty())
        CHECK(c.structures[0].polygons[0].texture == 5 && c.structures[0].polygons[0].vertices[3] == 2);
    CHECK(c.objects.size() == 1);
    if (!c.objects.empty())
        CHECK(c.objects[0].type == 3 && c.objects[0].delay == 25 && c.objects[0].keys.size() == 1 &&
              c.objects[0].keys[0].position[2] == -(515 << 16) && c.objects[0].keys[0].rotation[3] == 16384);
    CHECK(c.road.size() == 1);
    if (!c.road.empty()) {
        const auto &r = c.road[0];
        // The vectors' bytes as every reader of them in the game takes them:
        // byte << 9 as 16.16 (e.g. 0x406a80, 0x420230, 0x4a0aa0), byte / 128.
        CHECK(r.position[0] == 829.5f && r.position[2] == 136.25f && r.normal[1] == 127 / 128.0f);
        CHECK(r.forward[0] == 85 / 128.0f && r.forward[2] == -93 / 128.0f && r.right[0] == -94 / 128.0f);
        CHECK(r.leftWall == 23.5f && r.rightWall == 28.0f && r.extra[0] == 34 && r.unknown == 0x550ff8);
        CHECK(r.leftLanes() == 2 && r.rightLanes() == 2 && r.leftEdge() == 16.0f && r.rightEdge() == 16.0f);
    }

    Bytes badVertex = b;
    const size_t poly = table + 16 + 16 + 8 + 8 + 16 * 3 + 2; // block 1's polygon's vertices
    badVertex[poly] = 3;
    CHECK(parseCollision(badVertex, c));
    Bytes badStructure = b;
    badStructure[table + 16 + 16 + 8 + 62 + 8 + 3] = 1; // the object's structure: 1 of 1
    CHECK(parseCollision(badStructure, c));
    CHECK(parseCollision(Bytes{'C', 'O', 'L', 'X'}, c));
}

void testSoundBank() {
    // Four slots: PCM, empty (0), EA-XA, and a stale one pointing into the
    // audio; the headers' area ends at 128, the audio follows.
    Bytes b = {'B', 'N', 'K', 'l', 2, 0, 4, 0};
    put32(b, 128);
    b.resize(12 + 16, 0);
    const Bytes pcm = ptHeader({{0x85, 3}, {0x88, 128}, {0x84, 16000}});
    const Bytes xa = ptHeader({{0x83, 7}, {0x85, 30}, {0x88, 134}, {0x86, 2}, {0x87, 20}, {0x92, 1}});
    putU32(b, 12, uint32_t(b.size() - 12));
    b.insert(b.end(), pcm.begin(), pcm.end());
    putU32(b, 20, uint32_t(b.size() - 20));
    b.insert(b.end(), xa.begin(), xa.end());
    putU32(b, 24, uint32_t(140 - 24)); // into the audio: no header there
    b.resize(128, 0);
    for (int16_t v : {100, -200, 300})
        put16(b, uint16_t(v));
    Bytes frames(30, 0);
    frames[0] = 0x0c;
    frames[1] = 0x70; // 7, then zeros
    frames[15] = 0x0c;
    b.insert(b.end(), frames.begin(), frames.end());

    SoundBank bank;
    CHECK(!parseSoundBank(b, bank));
    CHECK(bank.version == 2 && bank.slots.size() == 4);
    if (bank.slots.size() != 4)
        return;
    CHECK(bank.slots[0] && !bank.slots[1] && bank.slots[2] && !bank.slots[3]);
    if (!bank.slots[0] || !bank.slots[2])
        return;
    const SoundHeader &p = *bank.slots[0];
    CHECK(p.compression == SoundHeader::kPcm16 && p.channels == 1 && p.rate == 16000 && p.samples == 3);
    const SoundHeader &x = *bank.slots[2];
    CHECK(x.compression == SoundHeader::kEaXa && x.rate == 22050 && x.samples == 30 && x.dataOffset == 134);
    CHECK(x.loopStart == 2 && x.loopEnd == 20 && x.tags.at(0x92) == 1 && x.tags.at(0x06) == 25);

    audio::Sound sound;
    CHECK(!decodeBankSound(b, p, sound));
    CHECK(sound.rate == 16000 && sound.samples == (std::vector<int16_t>{100, -200, 300}));
    CHECK(!decodeBankSound(b, x, sound));
    CHECK(sound.format == audio::kFormatEaXa && sound.samples.size() == 30 && sound.samples[0] == 7);

    // Damage: an unknown compression, data past the end, a cut header.
    SoundHeader odd = x;
    odd.compression = 3;
    CHECK(decodeBankSound(b, odd, sound));
    odd = x;
    odd.dataOffset = uint32_t(b.size() + 1);
    CHECK(decodeBankSound(b, odd, sound));
    Bytes cut(b.begin(), b.begin() + 40);
    putU32(cut, 8, 40);
    CHECK(parseSoundBank(cut, bank));
    CHECK(parseSoundBank(Bytes{'B', 'N', 'K', 'x', 2, 0, 0, 0, 0, 0, 0, 0}, bank));

    // Version 4 has its slots from 20, after the audio's size and -1: the
    // same bank so laid out keeps its slot numbers.
    Bytes v4 = {'B', 'N', 'K', 'l', 4, 0, 4, 0};
    put32(v4, 128 + 8);
    put32(v4, uint32_t(b.size() - 128));
    put32(v4, 0xffffffffu);
    v4.insert(v4.end(), b.begin() + 12, b.end()); // offsets count from each slot, so they still hold
    CHECK(!parseSoundBank(v4, bank));
    CHECK(bank.version == 4 && bank.slots.size() == 4 && bank.slots[0] && !bank.slots[1] && bank.slots[2] &&
          !bank.slots[3]);

    // Engines as the game pitches them, against its voices: the Corvette's
    // engine (root key 74, fine tune 28, range 15) idling at 900 of a
    // 6000 redline was at bend 16, -2497 cents; a range-6 sound at bend 77,
    // 121 cents.
    SoundHeader corvette;
    corvette.tags = {{0x07, 74}, {0x0A, 15}, {0x10, 28}};
    CHECK(engineBend(900, 6000) == 16);
    CHECK(patchCents(corvette, 60, 16) == -2497);
    CHECK(engineBend(6000, 6000) == 100 && engineBend(3000, 6000) == 54);
    SoundHeader plane;
    plane.tags = {{0x0A, 6}};
    CHECK(patchCents(plane, 60, 77) == 121 && patchCents(plane, 60, 64) == 0);
    SoundHeader flat;
    flat.tags = {{0x10, 249}}; // a signed byte
    CHECK(patchCents(flat, 60, 64) == -7);
}

void testMusic() {
    // A stream of two segments: EA-XA stereo (one block of 30 samples:
    // two groups) and PCM stereo (2 samples).
    auto chunk = [](Bytes &out, const char *tag, const Bytes &body) {
        out.insert(out.end(), tag, tag + 4);
        put32(out, uint32_t(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };
    Bytes mus;
    chunk(mus, "SCHl", ptHeader({{0x82, 2}, {0x83, 7}, {0x85, 30}}));
    Bytes count;
    put32(count, 1);
    chunk(mus, "SCCl", count);
    Bytes xa;
    put32(xa, 30);
    for (int v : {100, 0, -100, 0}) // left previous, before; right previous, before
        put16(xa, unsigned(v));
    xa.push_back(0x10); // filters: left 1, right 0
    xa.push_back(0xcc); // shifts 12, 12: residuals as they are
    xa.push_back(0x1f); // left +1, right -1
    xa.insert(xa.end(), 27, 0);
    Bytes group2(30, 0);
    group2[1] = 0xcc;
    xa.insert(xa.end(), group2.begin(), group2.end());
    chunk(mus, "SCDl", xa);
    chunk(mus, "SCEl", {});
    const uint32_t second = uint32_t(mus.size());
    chunk(mus, "SCHl", ptHeader({{0x82, 2}, {0x85, 2}}));
    chunk(mus, "SCCl", count);
    Bytes pcm;
    put32(pcm, 2);
    for (int v : {1, -1, 2, -2})
        put16(pcm, unsigned(v));
    chunk(mus, "SCDl", pcm);
    chunk(mus, "SCEl", {});

    MusicStream stream;
    CHECK(!stream.open(mus));
    CHECK(stream.segments().size() == 2 && stream.segmentAt(second) == 1 && stream.segmentAt(4) == -1);
    audio::Sound s;
    CHECK(!stream.decode(0, s));
    CHECK(s.channels == 2 && s.samples.size() == 60);
    if (s.samples.size() == 60) {
        // Left: 1 + 240/256 x 100 = 1 + 94; right: -1 + 0.
        CHECK(s.samples[0] == 1 + ((240 * 100 + 128) >> 8) && s.samples[1] == -1);
        CHECK(s.samples[56] == 0 && s.samples[57] == 0); // the second group, filter 0
    }
    CHECK(!stream.decode(1, s));
    CHECK(s.samples == (std::vector<int16_t>{1, -1, 2, -2}));
    Bytes cut(mus.begin(), mus.end() - 3);
    CHECK(stream.open(cut));

    // A map of three sections: 0 goes to 1 (values 0-49) or 2 (50-100),
    // 1 to 0, 2 nowhere; one event; then the offsets, big-endian.
    Bytes map = {'P', 'F', 'D', 'x', 0, 0, 3, 16, 0, 0, 0, 2};
    auto section = [&](uint8_t part, std::vector<std::array<uint8_t, 3>> routes) {
        Bytes r(28, 0);
        r[0] = part;
        r[1] = uint8_t(routes.size());
        r[2] = r[3] = 0xff;
        for (size_t k = 0; k < routes.size(); ++k)
            for (int i = 0; i < 3; ++i)
                r[4 + 3 * k + i] = routes[k][i];
        map.insert(map.end(), r.begin(), r.end());
    };
    section(0, {{0, 49, 1}, {60, 100, 2}});
    section(1, {{0, 100, 0}});
    section(1, {});
    // Two rows of events, for parts 0 and 1: event 4 goes to 2, then 0.
    for (uint8_t to : {uint8_t(2), uint8_t(0)}) {
        Bytes row(16, 0);
        row[4] = to;
        row[9] = 77; // a section that isn't there
        map.insert(map.end(), row.begin(), row.end());
    }
    for (uint32_t o : {0u, second, 0x01020304u})
        put32be(map, o);
    MusicMap m;
    CHECK(!parseMusicMap(map, m));
    CHECK(m.sections.size() == 3 && m.events.size() == 2 && m.offsets.size() == 3);
    CHECK(m.offsets[1] == second && m.offsets[2] == 0x01020304u);
    CHECK(m.next(0, 10) == 1 && m.next(0, 60) == 2 && m.next(0, 100) == 2 && m.next(1, 70) == 0);
    CHECK(m.next(0, 55) == 0); // no route for 55: the section again
    CHECK(m.next(2, 0) == -1 && m.next(5, 0) == -1 && m.sections[2].part == 1);
    CHECK(m.event(0, 4) == 2 && m.event(1, 4) == 0 && m.event(2, 4) == 0);
    CHECK(m.event(0, 9) == -1 && m.event(0, 16) == -1 && m.event(7, 4) == -1);
    Bytes badRoute = map;
    badRoute[12 + 6] = 3; // section 0's first route to section 3
    CHECK(parseMusicMap(badRoute, m));
    CHECK(parseMusicMap(Bytes(map.begin(), map.end() - 1), m));
}

void testMovie() {
    // A 16x16 intra frame: luma blocks of 138, 100 and 128, then one whose
    // first horizontal coefficient is escaped (level 100); chroma 128.
    MadBits k;
    for (int dc : {10, -28, 0}) {
        k.put(uint32_t(dc) & 0xff, 8);
        k.code("10"); // end of block
    }
    k.put(0, 8);
    k.code("000001"); // escape
    k.put(100, 10);
    k.put(0, 6); // run 0: the coefficient after the DC term
    k.code("10");
    for (int i = 0; i < 2; ++i) {
        k.put(0, 8);
        k.code("10");
    }
    const Bytes intra = madFrame('k', k);
    MadDecoder d;
    d.setArithmetic(MadDecoder::Arithmetic::Reference);
    CHECK(!d.decode(intra.data(), intra.size()));
    CHECK(d.width() == 16 && d.height() == 16);
    auto y = [&](int x, int row) { return d.plane(0)[row * d.stride(0) + x]; };
    CHECK(y(0, 0) == 138 && y(7, 7) == 138 && y(8, 0) == 100 && y(0, 8) == 128);
    // Quantiser 2953 x 16 / 1024 = 46: (100 x 46 >> 4) - 1, made odd, is
    // 287; the transform spreads it from +552 to -552 (16ths), across.
    CHECK(y(8, 8) == (2052 + 552) >> 4 && y(15, 15) == (2052 - 552) >> 4);
    CHECK(d.plane(1)[0] == 128 && d.plane(2)[d.stride(2) * 7 + 7] == 128);
    Surface rgb;
    d.toSurface(rgb);
    CHECK(rgb.w == 16 && rgb.row(0)[0] == 138 && rgb.row(0)[1] == 138 && rgb.row(0)[3] == 255);

    // As the game shows it: at twice the size. Y 138 is the 7-bit 69 and
    // chroma 128 the 7-bit 64, half a step above the centre 63.5; the
    // colour by mad.md's formula, cut to 5:6:5 and widened again.
    Surface shown;
    d.toGameSurface(shown, MadDecoder::Display::Interpolate);
    CHECK(shown.w == 32 && shown.h == 32);
    {
        const int l = 2 * (138 >> 1) + 1;
        const int r = l + 2 * int(std::nearbyint(1.402 * 0.5));
        const int g = l + 2 * (int(std::nearbyint(-0.3441 * 0.5)) + int(std::nearbyint(-0.7141 * 0.5)));
        const int b = l + 2 * int(std::nearbyint(1.772 * 0.5));
        auto widen5 = [](int q) { return q << 3 | q >> 2; };
        auto widen6 = [](int q) { return q << 2 | q >> 4; };
        // Column 0 rounded, column 1 truncated.
        CHECK(shown.row(0)[0] == widen5(std::min((r + 4) >> 3, 31)) &&
              shown.row(0)[1] == widen6(std::min((g + 2) >> 2, 63)) &&
              shown.row(0)[2] == widen5(std::min((b + 4) >> 3, 31)));
        CHECK(shown.row(0)[4] == widen5(r >> 3) && shown.row(0)[6] == widen5(b >> 3));
    }
    // Mode 3 leaves each line's last 4 pixels and the last line black;
    // mode 1 leaves the odd lines black.
    CHECK(shown.row(0)[28 * 4] == 0 && shown.row(0)[27 * 4] != 0 && shown.row(31)[0] == 0 && shown.row(1)[0] != 0);
    d.toGameSurface(shown, MadDecoder::Display::Repeat);
    CHECK(shown.row(1)[0] == 0 && shown.row(2)[0] != 0 && shown.row(0)[31 * 4] != 0);

    // An inter frame copying block 0 only ("01", map 000001), vector 0,0,
    // 6 brighter (motion code +3, doubled); the rest intra: 128, and block 3
    // as before.
    MadBits m;
    m.code("01");
    m.put(1, 6);
    m.code("00");     // vector x 0, y 0
    m.code("100010"); // +3
    for (int i = 1; i < 6; ++i) {
        m.put(0, 8);
        if (i == 3) {
            m.code("000001");
            m.put(100, 10);
            m.put(0, 6);
        }
        m.code("10");
    }
    const Bytes inter = madFrame('m', m);
    CHECK(!d.decode(inter.data(), inter.size()));
    CHECK(y(0, 0) == 144 && y(8, 0) == 128 && y(8, 8) == 162 && y(15, 15) == 93);

    // All blocks copied ("1"), one pixel left (x +1); the last luma block's
    // source would read past the picture, so it stays.
    MadBits all;
    all.code("1");
    all.code("1"
             "0"
             "0000"); // x +1
    all.code("0");
    for (int i = 0; i < 6; ++i)
        all.code("0");
    const Bytes shifted = madFrame('e', all);
    CHECK(!d.decode(shifted.data(), shifted.size()));
    CHECK(y(0, 0) == 144 && y(6, 0) == 144 && y(7, 0) == 128 && y(8, 8) == 162);
    CHECK(y(0, 8) == 128 && y(7, 8) == 162); // block 2 takes block 3's first column
    // An 'e' frame isn't kept: the next refers to the one before it.
    CHECK(!d.decode(shifted.data(), shifted.size()));
    CHECK(y(7, 0) == 128 && y(6, 0) == 144);

    // The game's arithmetic: 7-bit samples about 64, so 10 above the middle
    // is 5; the escaped block's coefficient is 100 x Q, Q = 16 x 2^12 x 5906
    // / 2^16 = 5906, spread from about +17 to -18 across.
    MadDecoder g;
    g.setArithmetic(MadDecoder::Arithmetic::Game);
    CHECK(!g.decode(intra.data(), intra.size()));
    auto gy = [&](int x, int row) { return g.plane(0)[row * g.stride(0) + x]; };
    CHECK(gy(0, 0) == 69 && gy(8, 0) == 50 && gy(0, 8) == 64);
    CHECK(gy(8, 8) == 81 && gy(15, 15) == 46);
    Surface grgb;
    g.toSurface(grgb);
    CHECK(grgb.row(0)[0] == 138);
    // Its copies add d itself.
    CHECK(!g.decode(inter.data(), inter.size()));
    CHECK(gy(0, 0) == 72);

    // Its quantiser matrix applied transposed (its scan is the zigzag
    // transposed): the escaped level 5 at scan position 14, row 0 column 4,
    // takes W[4][0] = 22, not W[0][4] = 26. Q = 22 x 2^12 x 8192 / 2^16 =
    // 11264, and 5 Q = 56320, across as + - - + + - - +: bits 16-23 give 0
    // and -1 (with 26, 66560: 1 and -2).
    MadBits t;
    for (int j = 0; j < 6; ++j) {
        t.put(0, 8);
        if (j == 0) {
            t.code("000001");
            t.put(5, 10);
            t.put(13, 6);
        }
        t.code("10");
    }
    const Bytes transposed = madFrame('k', t);
    CHECK(!g.decode(transposed.data(), transposed.size()));
    CHECK(gy(0, 0) == 64 && gy(1, 0) == 63 && gy(2, 0) == 63 && gy(3, 0) == 64 && gy(5, 7) == 63 && gy(7, 7) == 64);

    // Its copies clamp to 0-127, as its output table does: 124 (DC 120)
    // copied 16 brighter ("1", "0", "1111") is 127.
    MadBits bright;
    bright.put(120, 8);
    bright.code("10");
    for (int j = 1; j < 6; ++j) {
        bright.put(0, 8);
        bright.code("10");
    }
    const Bytes brightFrame = madFrame('k', bright);
    MadBits brighter;
    brighter.code("1");  // all copied
    brighter.code("00"); // vector 0, 0
    brighter.code("101111");
    for (int j = 1; j < 6; ++j)
        brighter.code("0");
    const Bytes brighterFrame = madFrame('m', brighter);
    CHECK(!g.decode(brightFrame.data(), brightFrame.size()) && gy(0, 0) == 124);
    CHECK(!g.decode(brighterFrame.data(), brighterFrame.size()) && gy(0, 0) == 127 && gy(8, 0) == 64);

    // Its chroma vector is the luma one shifted right, rounding down (-1:
    // -1), where the reference halves it toward zero (0). 32x16: the first
    // macroblock's U is 10 above the middle; the second copies its U one
    // pixel left, the rest copied as they are.
    MadBits two;
    for (int j = 0; j < 12; ++j) {
        two.put(j == 4 ? 20 : 0, 8);
        two.code("10");
    }
    MadBits left;
    left.code("1"
              "00"); // the first macroblock copied, vector 0, 0
    for (int j = 0; j < 6; ++j)
        left.code("0");
    left.code("01");
    left.put(16, 6);     // U alone copied
    left.code("111111"); // x -1
    left.code("0");      // y 0
    for (int j = 0; j < 6; ++j) {
        if (j == 4) {
            left.code("0");
            continue;
        }
        left.put(0, 8);
        left.code("10");
    }
    const Bytes twoFrame = madFrame('k', two, 1, 32), leftFrame = madFrame('m', left, 1, 32);
    auto gu = [&](MadDecoder &dec, int x) { return dec.plane(1)[x]; };
    CHECK(!g.decode(twoFrame.data(), twoFrame.size()) && gu(g, 7) == 74 && gu(g, 8) == 64);
    CHECK(!g.decode(leftFrame.data(), leftFrame.size()) && gu(g, 7) == 74 && gu(g, 8) == 74 && gu(g, 9) == 64);
    MadDecoder r;
    r.setArithmetic(MadDecoder::Arithmetic::Reference);
    CHECK(!r.decode(twoFrame.data(), twoFrame.size()) && gu(r, 7) > 128 && gu(r, 8) == 128);
    CHECK(!r.decode(leftFrame.data(), leftFrame.size()) && gu(r, 8) == 128);

    Bytes bad = intra;
    bad.resize(30);
    CHECK(d.decode(bad.data(), bad.size()));

    // A movie: frames interleaved with stereo EA-XA audio.
    auto chunk = [](Bytes &out, const char *tag, const Bytes &body) {
        out.insert(out.end(), tag, tag + 4);
        put32(out, uint32_t(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };
    Bytes mad = intra;
    chunk(mad, "SCHl", ptHeader({{0x82, 2}, {0x83, 7}, {0x85, 28}}));
    Bytes count;
    put32(count, 1);
    chunk(mad, "SCCl", count);
    mad.insert(mad.end(), inter.begin(), inter.end());
    Bytes xa;
    put32(xa, 28);
    xa.insert(xa.end(), 8, 0);
    Bytes group(30, 0);
    group[1] = 0xcc;
    xa.insert(xa.end(), group.begin(), group.end());
    chunk(mad, "SCDl", xa);
    chunk(mad, "SCEl", {});
    MadMovie movie;
    CHECK(!movie.open(mad));
    CHECK(movie.frames().size() == 2 && movie.frames()[0].kind == 'k' && movie.frames()[1].kind == 'm');
    CHECK(movie.width() == 16 && movie.height() == 16 && movie.frameDuration() == 4369067);
    CHECK(movie.hasAudio() && movie.audioHeader().samples == 28);
    audio::Sound sound;
    CHECK(!movie.decodeAudio(sound));
    CHECK(sound.channels == 2 && sound.rate == 22050 && sound.samples.size() == 56);
    MadDecoder fresh;
    fresh.setArithmetic(MadDecoder::Arithmetic::Reference);
    CHECK(!movie.decodeFrame(fresh, 0) && !movie.decodeFrame(fresh, 1));
    CHECK(fresh.plane(0)[0] == 144);
    CHECK(movie.decodeFrame(fresh, 2));
    Bytes cut(mad.begin(), mad.end() - 3);
    CHECK(movie.open(cut));
}

} // namespace

static const reluva::test::Suite kSuite("nfs.unit.formats",
                                        {
                                            {"ref pack", [](const stdfs::path &) { testRefPack(); }},
                                            {"viv", [](const stdfs::path &) { testViv(); }},
                                            {"shapes", [](const stdfs::path &) { testShapes(); }},
                                            {"cockpit", [](const stdfs::path &) { testCockpit(); }},
                                            {"text table", [](const stdfs::path &) { testTextTable(); }},
                                            {"font", [](const stdfs::path &) { testFont(); }},
                                            {"model", [](const stdfs::path &) { testModel(); }},
                                            {"horizon", [](const stdfs::path &) { testHorizon(); }},
                                            {"collision", [](const stdfs::path &) { testCollision(); }},
                                            {"sound bank", [](const stdfs::path &) { testSoundBank(); }},
                                            {"music", [](const stdfs::path &) { testMusic(); }},
                                            {"movie", [](const stdfs::path &) { testMovie(); }},
                                        });
