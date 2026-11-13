// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"

#include "graphics/palette.h"
#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/formats/bitmap.h"
#include "common/stream.h"

using namespace reluva;
using namespace reluva::stgenerations;

// True colour pixels are 0xAARRGGBB (STG-Engine's PixelFormat test, for the
// formats the game uses).
static void pixelformat_conversion() {
    CHECK_EQUAL(argb(0x12, 0x34, 0x56), 0xFF123456u);
    uint8_t r, g, b;
    argbToRgb(0xFF123456u, r, g, b);
    CHECK(r == 0x12 && g == 0x34 && b == 0x56);
    Canvas c;
    c.create(2, 1, Canvas::kArgb32);
    CHECK(!c.isClut8() && c.bytesPerPixel() == 4 && c.pitch == 8);
    c.setPixel(1, 0, argb(1, 2, 3));
    CHECK_EQUAL(c.getPixel(1, 0), 0xFF010203u);
    c.fillRect(Rect(0, 0, 1, 1), argb(9, 9, 9));
    CHECK_EQUAL(c.getPixel(0, 0), 0xFF090909u);
}

static void surface_drawing() {
    Canvas s;
    s.create(8, 4, Canvas::kClut8);
    s.fillRect(Rect(-2, -2, 3, 2), 5);
    CHECK_EQUAL(s.getPixel(0, 0), 5u);
    CHECK_EQUAL(s.getPixel(2, 1), 5u);
    CHECK_EQUAL(s.getPixel(3, 1), 0u);
    CHECK_EQUAL(s.getPixel(0, 2), 0u);

    s.drawLine(0, 3, 7, 3, 9);
    CHECK_EQUAL(s.getPixel(7, 3), 9u);
    s.setPixel(100, 100, 1); // clipped, must not crash

    const uint8_t block[4] = { 1, 2, 3, 4 };
    s.copyRectToSurface(block, 2, 7, -1, 2, 2); // mostly off-screen
    CHECK_EQUAL(s.getPixel(7, 0), 3u);

    Canvas moved(std::move(s));
    CHECK(s.empty());
    CHECK_EQUAL(moved.getPixel(7, 0), 3u);
}

static void palette_helpers() {
    Palette pal;
    pal.setColor(1, 255, 0, 0);
    pal.setColor(2, 0, 0, 250);
    CHECK_EQUAL(pal.findBestColor(0, 10, 240), 2);
    CHECK_EQUAL(pal.findBestColor(200, 20, 20), 1);

    uint8_t vga[768] = {};
    vga[9] = 63, vga[10] = 0, vga[11] = 32;
    pal.loadVga(vga);
    uint8_t r, g, b;
    pal.getColor(3, r, g, b);
    CHECK_EQUAL(r, 255);
    CHECK_EQUAL(g, 0);
    CHECK_EQUAL(b, 130);
}

static void bmp_uncompressed_8bit() {
    std::vector<uint8_t> pixels = { 0, 1, 1, 1, 0, 0 }; // 3x2, top row first
    std::vector<uint8_t> file = stgtest::makeBMP8(3, 2, pixels, false);
    MemoryReadStream s(file);
    BitmapDecoder decoder;
    CHECK(decoder.loadStream(s));
    const Canvas& surf = decoder.getSurface();
    CHECK_EQUAL(surf.w, 3);
    CHECK_EQUAL(surf.h, 2);
    CHECK(decoder.hasPalette());
    CHECK_EQUAL(decoder.getPaletteColorCount(), 2);
    CHECK_EQUAL(surf.getPixel(1, 0), 1u);
    CHECK_EQUAL(surf.getPixel(0, 1), 1u);
    CHECK_EQUAL(surf.getPixel(2, 1), 0u);
    uint8_t r, g, b;
    decoder.getPalette().getColor(1, r, g, b);
    CHECK_EQUAL(r, 255);
    CHECK_EQUAL(g, 128);
    CHECK_EQUAL(b, 0);
}

static void bmp_rle8() {
    // Row 0 (bottom): run of 4 x colour 1. Row 1: absolute run 1,0,1 + pad.
    std::vector<uint8_t> rle = { 4, 1, 0, 0, 0, 3, 1, 0, 1, 0, 0, 1 };
    std::vector<uint8_t> file = stgtest::makeBMP8(4, 2, rle, true);
    MemoryReadStream s(file);
    BitmapDecoder decoder;
    CHECK(decoder.loadStream(s));
    const Canvas& surf = decoder.getSurface();
    CHECK_EQUAL(surf.getPixel(3, 1), 1u);
    CHECK_EQUAL(surf.getPixel(0, 0), 1u);
    CHECK_EQUAL(surf.getPixel(1, 0), 0u);
    CHECK_EQUAL(surf.getPixel(2, 0), 1u);
    CHECK_EQUAL(surf.getPixel(3, 0), 0u);
}

static void bmp_rejects_garbage() {
    const uint8_t junk[] = "XX not a bitmap";
    MemoryReadStream s(std::vector<uint8_t>(junk, junk + sizeof(junk)));
    BitmapDecoder decoder;
    CHECK(!decoder.loadStream(s));
}

static void surface_fill_polygon() {
    Canvas s;
    s.create(10, 10, Canvas::kClut8);

    // Two triangles splitting a square share their diagonal without overlap.
    const Point lower[] = { Point(2, 2), Point(8, 2), Point(8, 8) };
    const Point upper[] = { Point(2, 2), Point(8, 8), Point(2, 8) };
    s.fillPolygon(lower, 3, 1);
    s.fillPolygon(upper, 3, 2);
    int counts[3] = { 0, 0, 0 };
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++)
            counts[s.getPixel(x, y)]++;
    }
    CHECK_EQUAL(counts[1] + counts[2], 36);
    CHECK_EQUAL(counts[1], 15);
    CHECK_EQUAL(s.getPixel(7, 2), 1u);
    CHECK_EQUAL(s.getPixel(2, 7), 2u);
    CHECK_EQUAL(s.getPixel(8, 8), 0u);

    // Clipped at the surface edges.
    const Point big[] = { Point(-5, -5), Point(20, -5), Point(20, 20),
                                  Point(-5, 20) };
    s.fillPolygon(big, 4, 3);
    CHECK_EQUAL(s.getPixel(0, 0), 3u);
    CHECK_EQUAL(s.getPixel(9, 9), 3u);
}

static const reluva::test::Suite suite("stgenerations.unit.canvas", {
    {"pixelformat conversion", [](const std::filesystem::path&) { pixelformat_conversion(); }},
    {"surface drawing", [](const std::filesystem::path&) { surface_drawing(); }},
    {"palette helpers", [](const std::filesystem::path&) { palette_helpers(); }},
    {"bmp uncompressed 8bit", [](const std::filesystem::path&) { bmp_uncompressed_8bit(); }},
    {"bmp rle8", [](const std::filesystem::path&) { bmp_rle8(); }},
    {"bmp rejects garbage", [](const std::filesystem::path&) { bmp_rejects_garbage(); }},
    {"surface fill polygon", [](const std::filesystem::path&) { surface_fill_polygon(); }},
});
