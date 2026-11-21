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

void testFont() {
    Font font;
    CHECK(!font.load(makeFont(8)));
    CHECK(font.lineHeight() == 8 && font.glyphHeight() == 7 && font.spaceAdvance() == 4);
    // 'A' is glyph 10, the first of row 2: a pixel wider than the header's
    // advance - 1, and advancing a pixel further than the header says.
    const Font::Glyph *a = font.glyph(U'A');
    CHECK(a && a->x == 1 && a->y == 9 && a->w == 2 + 10 % 4 + 1 && a->h == 7 && a->advance == 2 + 10 % 4 + 2);
    const Font::Glyph *one = font.glyph(U'1');
    CHECK(one && one->x == 1 && one->y == 1 && one->w == 2 && one->advance == 3);
    const Font::Glyph *two = font.glyph(U'2');
    CHECK(two && two->x == 4 && two->w == 3);
    CHECK(font.glyph(U'é') == &font.glyphs()[5 + 62 + 32]);
    CHECK(font.glyph(U'–') == &font.glyphs()[119]);
    CHECK(font.glyph(U' ') == nullptr && font.glyph(U'€') == nullptr);
    CHECK(font.measure("1 2") == 3 + 4 + 4);

    // Drawing: the glyph's pixels, not its background pixel, nor its cell's
    // last row.
    Surface s;
    s.create(20, 10, PixelFormat::kRGBA32);
    CHECK(font.draw(s, 2, 1, "1") == 5);
    auto lit = [&](int x, int y) { return s.row(y)[size_t(x) * 4 + 3] != 0; };
    CHECK(!lit(2, 1) && lit(3, 1) && lit(2, 2) && lit(3, 6) && !lit(3, 7) && !lit(4, 2) && !lit(2, 8));
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
    // "12 12" is 18 pixels; a word fits only with the space after it (4),
    // but the last word has none.
    CHECK(font.wrap("12 12 12", 22) == std::vector<std::string>({"12 12", "12"}));
    CHECK(font.wrap("12 12 12", 21) == std::vector<std::string>({"12", "12 12"}));
    CHECK(font.wrap("12 12", 18) == std::vector<std::string>({"12 12"}));
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
