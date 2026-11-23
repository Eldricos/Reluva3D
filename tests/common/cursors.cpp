// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Windows' pointer files (graphics/cursor_file.h), made here: a cursor of
// 24 bits with its mask, one of 32 bits with its own alpha, an icon of 4
// bits, an animated cursor with its rates and sequence, and files that
// aren't right; and Reluva's own arrow.
//
//   reluva-tests common.unit.cursors

#include "graphics/cursor_file.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

void put16(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, x & 0xffff);
    put16(v, x >> 16);
}

void putId(std::vector<uint8_t> &v, const char *id) {
    v.insert(v.end(), id, id + 4);
}

// A picture of 4 by 4: `colour(x, y)` as 0xRRGGBB (or a palette index, for
// 4 bits; 0xAARRGGBB for 32), `masked(x, y)` its AND bit; rows bottom up.
template<typename Colour, typename Masked>
std::vector<uint8_t> picture(int bits, Colour colour, Masked masked) {
    std::vector<uint8_t> v;
    put32(v, 40);
    put32(v, 4);
    put32(v, 8); // both bitmaps
    put16(v, 1);
    put16(v, uint32_t(bits));
    for (int i = 0; i < 6; ++i)
        put32(v, 0);
    if (bits == 4) // a palette of 16: index i is grey i * 16
        for (int i = 0; i < 16; ++i) {
            const uint8_t g = uint8_t(i * 16);
            v.insert(v.end(), {g, g, g, 0});
        }
    for (int y = 3; y >= 0; --y) {
        if (bits == 4) {
            v.push_back(uint8_t(colour(0, y) << 4 | colour(1, y)));
            v.push_back(uint8_t(colour(2, y) << 4 | colour(3, y)));
            v.insert(v.end(), {0, 0});
        } else {
            for (int x = 0; x < 4; ++x) {
                const uint32_t c = uint32_t(colour(x, y));
                v.insert(v.end(), {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16)});
                if (bits == 32)
                    v.push_back(uint8_t(c >> 24));
            }
        }
    }
    for (int y = 3; y >= 0; --y) {
        uint8_t row = 0;
        for (int x = 0; x < 4; ++x)
            if (masked(x, y))
                row |= uint8_t(0x80 >> x);
        v.insert(v.end(), {row, 0, 0, 0});
    }
    return v;
}

// A file of one picture: a cursor (type 2) with its hotspot, or an icon.
std::vector<uint8_t> file(const std::vector<uint8_t> &picture, bool cursor, int hotX, int hotY) {
    std::vector<uint8_t> v;
    put16(v, 0);
    put16(v, cursor ? 2 : 1);
    put16(v, 1);
    v.insert(v.end(), {4, 4, 0, 0});
    put16(v, uint32_t(cursor ? hotX : 1));
    put16(v, uint32_t(cursor ? hotY : 24));
    put32(v, uint32_t(picture.size()));
    put32(v, 22);
    v.insert(v.end(), picture.begin(), picture.end());
    return v;
}

std::vector<uint8_t> chunk(const char *id, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> v;
    putId(v, id);
    put32(v, uint32_t(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    if (data.size() & 1)
        v.push_back(0);
    return v;
}

// Three frames, red, green and blue, shown blue, red, blue, green for 6,
// 12, 6 and 3 sixtieths of a second.
std::vector<uint8_t> animated(bool withRate) {
    std::vector<uint8_t> frames;
    putId(frames, "fram");
    for (uint32_t c : {0xff0000u, 0x00ff00u, 0x0000ffu}) {
        const auto colour = [c](int, int) { return c; };
        const std::vector<uint8_t> f = file(picture(24, colour, [](int, int) { return false; }), true, 2, 3);
        const std::vector<uint8_t> ch = chunk("icon", f);
        frames.insert(frames.end(), ch.begin(), ch.end());
    }
    std::vector<uint8_t> header;
    for (uint32_t x : {36u, 3u, 4u, 0u, 0u, 0u, 0u, 6u, 3u}) // AF_ICON | AF_SEQUENCE, 6 jiffies
        put32(header, x);
    std::vector<uint8_t> rate;
    for (uint32_t x : {6u, 12u, 6u, 3u})
        put32(rate, x);
    std::vector<uint8_t> seq;
    for (uint32_t x : {2u, 0u, 2u, 1u})
        put32(seq, x);
    std::vector<uint8_t> body;
    putId(body, "ACON");
    for (const std::vector<uint8_t> &c :
         {chunk("anih", header), withRate ? chunk("rate", rate) : std::vector<uint8_t>(), chunk("seq ", seq),
          chunk("LIST", frames)})
        body.insert(body.end(), c.begin(), c.end());
    return chunk("RIFF", body);
}

uint32_t rgba(const Cursor &c, int x, int y) {
    const uint8_t *p = c.pixel(x, y);
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

void testStill() {
    // 24 bits: the masked column see-through where it's black, black where
    // it isn't (it would invert the screen); the rest as coloured.
    const auto colour = [](int x, int y) { return x == 3 && y == 0 ? 0xffffffu : x == 3 ? 0u : 0x102030u; };
    const auto masked = [](int x, int) { return x == 3; };
    CursorFile f;
    CHECK(!readCursorFile(file(picture(24, colour, masked), true, 1, 2), f));
    CHECK(f.frames.size() == 1 && f.steps.size() == 1);
    const Cursor *c = f.at(12345);
    CHECK(c && c->w == 4 && c->h == 4 && c->hotX == 1 && c->hotY == 2);
    CHECK(rgba(*c, 0, 0) == 0x102030ff && rgba(*c, 2, 3) == 0x102030ff);
    CHECK(rgba(*c, 3, 1) == 0 && rgba(*c, 3, 0) == 0x000000ff);

    // 32 bits with alpha: the alpha's used and the mask isn't.
    const auto alpha = [](int x, int) { return x < 2 ? 0x80ff0000u : 0u; };
    CHECK(!readCursorFile(file(picture(32, alpha, [](int, int) { return true; }), true, 0, 0), f));
    CHECK(rgba(f.frames[0], 0, 0) == 0xff000080 && (rgba(f.frames[0], 3, 3) & 0xff) == 0);

    // An icon of 4 bits from its palette, its hotspot its middle.
    const auto grey = [](int x, int y) { return uint32_t(x + y * 4); };
    CHECK(!readCursorFile(file(picture(4, grey, [](int, int) { return false; }), false, 0, 0), f));
    CHECK(f.frames[0].hotX == 2 && f.frames[0].hotY == 2);
    CHECK(rgba(f.frames[0], 1, 0) == 0x101010ff && rgba(f.frames[0], 3, 2) == 0xb0b0b0ff);
}

void testAnimated() {
    CursorFile f;
    CHECK(!readCursorFile(animated(true), f));
    CHECK(f.frames.size() == 3 && f.steps.size() == 4);
    CHECK(f.frames[0].hotX == 2 && f.frames[0].hotY == 3);
    // Blue for 100 ms, red for 200, blue for 100, green for 50; then again.
    const auto at = [&](uint64_t ms) { return rgba(*f.at(ms), 0, 0) >> 8; };
    CHECK(at(0) == 0x0000ff && at(99) == 0x0000ff && at(100) == 0xff0000 && at(299) == 0xff0000);
    CHECK(at(300) == 0x0000ff && at(400) == 0x00ff00 && at(449) == 0x00ff00 && at(450) == 0x0000ff);
    // Without rates, every step takes the header's 6 jiffies.
    CHECK(!readCursorFile(animated(false), f));
    CHECK(f.steps.size() == 4 && f.steps[1].ms == 100 && f.steps[3].ms == 100);
}

void testBroken() {
    CursorFile f;
    CHECK(readCursorFile({}, f));
    CHECK(readCursorFile({0, 0, 3, 0, 1, 0}, f)); // neither icon nor cursor
    std::vector<uint8_t> cut =
        file(picture(24, [](int, int) { return 0u; }, [](int, int) { return false; }), true, 0, 0);
    cut.resize(cut.size() - 10);
    CHECK(readCursorFile(cut, f));
    // A step naming a fourth frame, of three.
    std::vector<uint8_t> bad = animated(true);
    const auto seq = std::search(bad.begin(), bad.end(), std::begin("seq "), std::begin("seq ") + 4);
    CHECK(seq != bad.end());
    seq[8 + 8] = 3;
    CHECK(readCursorFile(bad, f));
}

void testArrow() {
    const Cursor c = standardArrow();
    CHECK(!c.empty() && c.hotX == 0 && c.hotY == 0);
    CHECK(rgba(c, 0, 0) == 0x000000ff && rgba(c, 1, 2) == 0xffffffff && (rgba(c, c.w - 1, 0) & 0xff) == 0);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.cursors",
                                        {
                                            {"still", [](const std::filesystem::path &) { testStill(); }},
                                            {"animated", [](const std::filesystem::path &) { testAnimated(); }},
                                            {"broken", [](const std::filesystem::path &) { testBroken(); }},
                                            {"arrow", [](const std::filesystem::path &) { testArrow(); }},
                                        });
