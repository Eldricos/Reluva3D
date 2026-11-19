// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: the .fnt fonts and wrapping text.

#include "engines/botf/formats/font.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

// A .fnt as docs/formats/botf/fnt.md describes: the header (line height,
// space advance, 120 advances) and a 16-bit TGA sheet of framed cells, in
// rows of 10, 26, 26, 32 and 26. Glyph i is a cell of width 2 + i % 4 filled
// with colour 0x0400 + i, except one pixel left as background; the first
// cell of rows 2-5 is a pixel wider than its advance says, as in the game.
Bytes makeFont(int lineHeight, int rowsToDraw = 5) {
    const int rows[5] = {10, 26, 26, 32, 26};
    std::vector<uint16_t> advances;
    int sheetW = 0;
    for (int r = 0, i = 0; r < 5; ++r) {
        int x = 1;
        for (int k = 0; k < rows[r]; ++k, ++i) {
            const int cell = 2 + i % 4 + (r > 0 && k == 0 ? 1 : 0);
            advances.push_back(uint16_t(2 + i % 4 + 1));
            x += cell + 1;
        }
        sheetW = std::max(sheetW, x);
    }
    const int sheetH = 5 * lineHeight + 1;
    std::vector<uint16_t> px(size_t(sheetW) * sheetH, 0); // top row first
    auto at = [&](int x, int y) -> uint16_t & { return px[size_t(y) * sheetW + x]; };
    const uint16_t frame = 0x03f1;
    for (int r = 0, i = 0; r < 5; ++r) {
        for (int x = 0; x < sheetW; ++x)
            at(x, r * lineHeight) = at(x, (r + 1) * lineHeight) = frame;
        int x = 1;
        for (int k = 0; k < rows[r]; ++k, ++i) {
            const int cell = 2 + i % 4 + (r > 0 && k == 0 ? 1 : 0);
            for (int y = r * lineHeight; y <= (r + 1) * lineHeight; ++y)
                at(x - 1, y) = at(x + cell, y) = frame;
            if (r < rowsToDraw)
                for (int y = r * lineHeight + 1; y < (r + 1) * lineHeight; ++y)
                    for (int cx = 0; cx < cell; ++cx)
                        if (!(cx == 0 && y == r * lineHeight + 1))
                            at(x + cx, y) = uint16_t(0x0400 + i);
            x += cell + 1;
        }
    }
    if (rowsToDraw < 5) // a full-height frame-coloured column splits a cell
        for (int y = 4 * lineHeight + 1; y < 5 * lineHeight; ++y)
            at(2, y) = frame;
    Bytes f(260, 0);
    f[0] = uint8_t(lineHeight);
    f[1] = 1;
    f[2] = 4;
    f[3] = 1;
    set32(f, 4, 10 | 120u << 16);
    set32(f, 8, uint32_t(sheetW) | uint32_t(sheetH) << 16);
    set32(f, 12, uint32_t(px.size() * 2));
    f[16] = 1;
    f[17] = 6;
    f[18] = 86;
    for (int i = 0; i < 120; ++i) {
        f[20 + 2 * size_t(i)] = uint8_t(advances[size_t(i)]);
        f[21 + 2 * size_t(i)] = uint8_t(advances[size_t(i)] >> 8);
    }
    Bytes tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    put16(tga, uint32_t(sheetW));
    put16(tga, uint32_t(sheetH));
    tga.push_back(16);
    tga.push_back(1); // rows from the bottom up
    for (int y = sheetH - 1; y >= 0; --y)
        for (int x = 0; x < sheetW; ++x)
            put16(tga, at(x, y));
    f.insert(f.end(), tga.begin(), tga.end());
    return f;
}

void testFont() {
    Font font;
    CHECK(!font.load(makeFont(8)));
    CHECK(font.lineHeight() == 8 && font.glyphHeight() == 7 && font.spaceAdvance() == 4);
    // 'A' is glyph 10, the first of row 2: a pixel wider than its advance - 1.
    const Font::Glyph *a = font.glyph(U'A');
    CHECK(a && a->x == 1 && a->y == 9 && a->w == 2 + 10 % 4 + 1 && a->h == 7 && a->advance == 2 + 10 % 4 + 1);
    const Font::Glyph *one = font.glyph(U'1');
    CHECK(one && one->x == 1 && one->y == 1 && one->w == 2 && one->advance == 3);
    const Font::Glyph *two = font.glyph(U'2');
    CHECK(two && two->x == 4 && two->w == 3);
    CHECK(font.glyph(U'é') == &font.glyphs()[5 + 62 + 32]);
    CHECK(font.glyph(U'–') == &font.glyphs()[119]);
    CHECK(font.glyph(U' ') == nullptr && font.glyph(U'€') == nullptr);
    CHECK(font.measure("1 2") == 3 + 4 + 4);

    // Drawing: the glyph's pixels, not its background pixel.
    Surface s;
    s.create(20, 10, PixelFormat::kRGBA32);
    CHECK(font.draw(s, 2, 1, "1") == 5);
    auto lit = [&](int x, int y) { return s.row(y)[size_t(x) * 4 + 3] != 0; };
    CHECK(!lit(2, 1) && lit(3, 1) && lit(2, 2) && lit(3, 7) && !lit(4, 2) && !lit(2, 8));
    s.create(20, 10, PixelFormat::kRGBA32);
    font.draw(s, 0, 0, "1", Color{0, 0, 0});
    CHECK(s.row(1)[3] == 255 && s.row(1)[0] == 0 && s.row(1)[1] == 0); // tinted black, still drawn

    // A row whose cells can't be told apart fails.
    CHECK(font.load(makeFont(8, 4)));
    CHECK(font.load(Bytes(100, 0)));
}

void testWrap() {
    Font font;
    CHECK(!font.load(makeFont(8)));
    // Every glyph advances 3 to 6 pixels, a space 4: '1' 3, '2' 4, '3' 5.
    CHECK(font.measure("12") == 7);
    CHECK(font.wrap("12 12 12", 18) == std::vector<std::string>({"12 12", "12"}));
    CHECK(font.wrap("12 12 12", 100) == std::vector<std::string>({"12 12 12"}));
    CHECK(font.wrap("123123 1", 6) == std::vector<std::string>({"123123", "1"})); // too wide: on its own
    CHECK(font.wrap("1\n\n2", 100) == std::vector<std::string>({"1", "", "2"}));
    CHECK(font.wrap("", 100) == std::vector<std::string>({""}));
}

} // namespace

static const reluva::test::Suite kSuite("botf.unit.fonts",
                                        {
                                            {"fonts", [](const std::filesystem::path &) { testFont(); }},
                                            {"wrap", [](const std::filesystem::path &) { testWrap(); }},
                                        });
