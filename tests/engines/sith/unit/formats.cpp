// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: colormaps, pictures, materials, fonts and string
// tables, each read from a small file the test builds.
//
//   reluva-tests sith.unit.formats

#include "engines/sith/formats/bitmap.h"
#include "engines/sith/formats/colormap.h"
#include "engines/sith/formats/font.h"
#include "engines/sith/formats/material.h"
#include "engines/sith/formats/strings.h"

#include "tests/support/test.h"

#include <cstring>
#include <string>

using namespace reluva;
using namespace reluva::sith;

namespace {

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

void putFloat(std::vector<uint8_t> &out, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put32(out, bits);
}

void putTag(std::vector<uint8_t> &out, const char *tag) {
    out.insert(out.end(), tag, tag + 4);
}

// A header of `words` 32-bit words after the tag, the rest of `size` zero.
std::vector<uint8_t> header(const char *tag, const std::vector<uint32_t> &words, size_t size) {
    std::vector<uint8_t> out;
    putTag(out, tag);
    for (uint32_t w : words)
        put32(out, w);
    out.resize(size);
    return out;
}

void testColormap() {
    std::vector<uint8_t> data;
    putTag(data, "CMP ");
    put32(data, 30);
    put32(data, 1);
    putFloat(data, 1.0f);
    putFloat(data, 0.25f);
    putFloat(data, 0.5f);
    data.resize(64);
    for (int i = 0; i < 256; ++i)
        data.insert(data.end(), {uint8_t(i), uint8_t(255 - i), 7});
    for (int level = 0; level < Colormap::kLightLevels; ++level)
        for (int i = 0; i < 256; ++i)
            data.push_back(uint8_t(level == 63 ? i : i / 2));
    data.resize(data.size() + 256 * 256, 9);

    Colormap cmp;
    CHECK(!parseColormap(data, cmp));
    CHECK(cmp.tint[1] == 0.25f);
    CHECK(cmp.palette[10].r == 10 && cmp.palette[10].g == 245 && cmp.palette[10].b == 7);
    CHECK(cmp.lit(63, 200) == 200);
    CHECK(cmp.lit(0, 200) == 100);
    CHECK(cmp.blend.size() == 256 * 256 && cmp.blend[0] == 9);

    // Without the blend table the file ends after the light levels.
    data[8] = 0;
    data.resize(data.size() - 256 * 256);
    CHECK(!parseColormap(data, cmp));
    CHECK(cmp.blend.empty());
    data.pop_back();
    CHECK(parseColormap(data, cmp));
    CHECK(parseColormap({'C', 'M', 'P'}, cmp));
}

void testBitmap8() {
    // Two cels, then a palette of its own.
    std::vector<uint8_t> data = header("BM  ", {0x46, 0, 2, 2, 5, 6, 3, 0, 8}, 128);
    put32(data, 2);
    put32(data, 1);
    data.insert(data.end(), {3, 4});
    put32(data, 1);
    put32(data, 2);
    data.insert(data.end(), {5, 6});
    for (int i = 0; i < 256; ++i)
        data.insert(data.end(), {uint8_t(i), 0, uint8_t(i)});

    Bitmap bm;
    CHECK(!parseBitmap(data, bm));
    CHECK(bm.cels.size() == 2 && bm.paletteMode == 2 && bm.x == 5 && bm.y == 6);
    CHECK(bm.cels.size() == 2 && bm.cels[1].w == 1 && bm.cels[1].h == 2 && bm.cels[1].pixels[1] == 6);
    CHECK(bm.palette && (*bm.palette)[4].r == 4);

    // Its own palette wins; the transparent colour (3) comes out clear.
    Palette other{};
    const image::Image img = bitmapToImage(bm, bm.cels[0], other, true);
    CHECK(img.w == 2 && img.pixel(1, 0)[0] == 4 && img.pixel(1, 0)[3] == 255);
    CHECK(img.pixel(0, 0)[3] == 0);

    // Cut short in its palette, or in a cel.
    data.resize(data.size() - 1);
    CHECK(parseBitmap(data, bm));
    data.resize(128 + 9);
    CHECK(parseBitmap(data, bm));
}

void testBitmap16() {
    // RGB565, magenta transparent, as the game's 16-bit pictures are.
    std::vector<uint8_t> data = header("BM  ", {0x46, 0, 1, 1, 0, 0, 0xf81f, 1, 16, 5, 6, 5, 11, 5, 0, 3, 2, 2}, 128);
    put32(data, 2);
    put32(data, 1);
    data.insert(data.end(), {0x1f, 0xf8, 0xe0, 0x07});
    data.push_back(0xaa); // after the picture, as in a font's file

    Bitmap bm;
    size_t used = 0;
    CHECK(!parseBitmap(data.data(), data.size(), bm, &used));
    CHECK(used == 128 + 8 + 4);
    CHECK(bm.bitsPerPixel == 16 && !bm.palette);
    const image::Image img = bitmapToImage(bm, bm.cels[0], Palette{}, true);
    CHECK(img.pixel(0, 0)[3] == 0);
    CHECK(img.pixel(1, 0)[0] == 0 && img.pixel(1, 0)[1] == 255 && img.pixel(1, 0)[2] == 0);
    const image::Image opaque = bitmapToImage(bm, bm.cels[0], Palette{}, false);
    CHECK(opaque.pixel(0, 0)[0] == 255 && opaque.pixel(0, 0)[2] == 255 && opaque.pixel(0, 0)[3] == 255);

    data[36] = 24;
    CHECK(parseBitmap(data, bm));
}

void testMaterial() {
    // A colour cel and two texture cels, sharing one 4x2 texture with two mips.
    std::vector<uint8_t> data = header("MAT ", {0x32, 2, 3, 1, 0, 8}, 76);
    put32(data, 0);
    put32(data, 42);
    for (int i = 0; i < 4; ++i)
        putFloat(data, 1.0f);
    for (int cel = 0; cel < 2; ++cel) {
        put32(data, 8);
        put32(data, 7);
        for (int i = 0; i < 4; ++i)
            putFloat(data, 1.0f);
        data.resize(data.size() + 12);
        put32(data, 0);
    }
    for (uint32_t v : {4u, 2u, 1u, 0u, 0u, 2u})
        put32(data, v);
    data.insert(data.end(), {0, 1, 2, 3, 4, 5, 6, 7});
    data.insert(data.end(), {8, 9});

    Material mat;
    CHECK(!parseMaterial(data, mat));
    CHECK(mat.type == 2 && mat.cels.size() == 3);
    CHECK(mat.cels.size() == 3 && mat.cels[0].texture == -1 && mat.cels[0].color == 42 && mat.cels[2].texture == 0);
    CHECK(mat.textures.size() == 1 && mat.textures[0].transparent && mat.textures[0].mips.size() == 2);
    CHECK(mat.textures[0].mips[1] == std::vector<uint8_t>({8, 9}));

    Palette pal{};
    pal[5] = {50, 60, 70};
    const image::Image img = materialToImage(mat.textures[0], 0, pal, true);
    CHECK(img.w == 4 && img.h == 2 && img.pixel(1, 1)[1] == 60);
    CHECK(img.pixel(0, 0)[3] == 0 && img.pixel(1, 0)[3] == 255);
    CHECK(materialToImage(mat.textures[0], 1, pal, true).w == 2);

    // A cel naming a texture it hasn't, and a mip cut short.
    std::vector<uint8_t> bad = data;
    bad[76 + 24 + 36] = 3;
    CHECK(parseMaterial(bad, mat));
    data.pop_back();
    CHECK(parseMaterial(data, mat));
}

void testFont() {
    // Two ranges, '0'-'1' and 'A', over a 5x2 strip.
    std::vector<uint8_t> data = header("SFNT", {10, 1, 2, 32, 2}, 40);
    data.insert(data.end(), {'0', 0, '1', 0});
    for (uint32_t v : {0u, 2u, 2u, 1u})
        put32(data, v);
    data.insert(data.end(), {'A', 0, 'A', 0});
    put32(data, 3);
    put32(data, 2);
    std::vector<uint8_t> strip = header("BM  ", {0x46, 0, 1, 1, 0, 0, 0, 0, 8}, 128);
    put32(strip, 5);
    put32(strip, 2);
    strip.resize(strip.size() + 10, 1);
    data.insert(data.end(), strip.begin(), strip.end());

    Font font;
    CHECK(!parseFont(data, font));
    CHECK(font.fields[2] == 32 && font.glyphs.size() == 3);
    CHECK(font.glyph('1') && font.glyph('1')->x == 2 && font.glyph('1')->width == 1);
    CHECK(font.glyph('A') && font.glyph('A')->x == 3);
    CHECK(!font.glyph('B'));

    // A glyph past the strip's right edge.
    data[40 + 4 + 16 + 4] = 4;
    CHECK(parseFont(data, font));
}

void testStrings() {
    const std::string text = "MSGS 9\t  # <--- a count\r\n"
                             "\r\n"
                             "#\t\"<key>\"\t<unused number>\t\"<string>\"\r\n"
                             "   \"GUI_OK\"   0  \"OK\"\r\n"
                             "\"NAME\"    0 \"%s's Game\"   # i.e. \"Bob's Game\"\r\n"
                             "\"DIK_BACKSLASH\"  0 \"\\\"\r\n"
                             "\"ERR\" 0 \"\\n\\nSee:\\n\"\r\n"
                             "\"CREDITS_big_446\" \t0 Paul Zabierek\r\n"
                             "END   # !!! the count\r\n"
                             "\"AFTER\" 0 \"not read\"\r\n";
    StringTable table;
    CHECK(!parseStrings(std::vector<uint8_t>(text.begin(), text.end()), table));
    CHECK(table.declaredCount == 9);
    CHECK(table.entries.size() == 5);
    CHECK(table.find("gui_ok") && *table.find("gui_ok") == "OK");
    CHECK(table.find("NAME") && *table.find("NAME") == "%s's Game");
    CHECK(table.find("DIK_BACKSLASH") && *table.find("DIK_BACKSLASH") == "\\");
    CHECK(table.find("ERR") && *table.find("ERR") == "\n\nSee:\n");
    CHECK(table.find("CREDITS_big_446") && *table.find("CREDITS_big_446") == "Paul Zabierek");
    CHECK(!table.find("AFTER"));

    const std::string noCount = "\"A\" 0 \"B\"\r\n";
    CHECK(parseStrings(std::vector<uint8_t>(noCount.begin(), noCount.end()), table));
    const std::string noKey = "MSGS 1\r\nA 0 \"B\"\r\n";
    CHECK(parseStrings(std::vector<uint8_t>(noKey.begin(), noKey.end()), table));
}

} // namespace

static const reluva::test::Suite kSuite("sith.unit.formats",
                                        {
                                            {"colormap", [](const std::filesystem::path &) { testColormap(); }},
                                            {"8-bit bitmap", [](const std::filesystem::path &) { testBitmap8(); }},
                                            {"16-bit bitmap", [](const std::filesystem::path &) { testBitmap16(); }},
                                            {"material", [](const std::filesystem::path &) { testMaterial(); }},
                                            {"font", [](const std::filesystem::path &) { testFont(); }},
                                            {"strings", [](const std::filesystem::path &) { testStrings(); }},
                                        });
