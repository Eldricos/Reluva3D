// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Indeo 5 video (video/indeo5.h), and an Indeo AVI read through
// avi::Decoder as the movie player does.
//
//   reluva-tests common.unit.indeo

#include "common/stream.h"
#include "video/avi.h"
#include "video/indeo5.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;

// Bits written as Indeo 5 reads them: a field's lowest bit first, each
// byte filled from its lowest bit.
struct LeBits {
    std::vector<uint8_t> bytes;
    size_t pos = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++pos) {
            if (pos / 8 >= bytes.size())
                bytes.push_back(0);
            bytes[pos / 8] |= uint8_t(((v >> i) & 1) << (pos % 8));
        }
    }
    void align() { pos = (pos + 7) & ~size_t(7); }
    void pad() { // up to the next byte, as bytes
        align();
        bytes.resize(pos / 8);
    }
};

// A 16x16 key frame by docs/common/video/indeo5.md, every block uncoded,
// so a flat grey picture: one luma band of 16x16 macroblocks of 8x8
// blocks, chroma bands of 4x4.
static std::vector<uint8_t> indeo5GreyFrame(int type) {
    LeBits b;
    b.put(0x1f, 5);
    b.put(uint32_t(type), 3);
    b.put(0, 8); // frame number
    if (type == 0) {
        b.put(0, 8);   // GOP flags
        b.put(0, 2);   // one luma band
        b.put(0, 1);   // one chroma band
        b.put(15, 4);  // size given:
        b.put(16, 13); // height
        b.put(16, 13); // width
        b.put(0, 1);   // luma: full-pel, macroblocks twice the blocks, 8x8
        b.put(0, 1);
        b.put(0, 1);
        b.put(0, 1);
        b.put(0, 2);
        b.put(0, 1); // chroma: full-pel, macroblocks of one block, 4x4
        b.put(1, 1);
        b.put(1, 1);
        b.put(0, 1);
        b.put(0, 2);
        b.align();
        b.put(0, 23);
        b.put(0, 1); // no extension
        b.align();
    }
    if (type == 4) {
        b.pad();
        return b.bytes;
    }
    b.put(0, 8); // frame flags
    b.put(0, 3);
    b.align();
    for (int band = 0; band < 3; ++band) {
        b.put(0, 8); // band flags: defaults
        b.put(0, 1); // no checksum
        b.put(0, 5); // quantiser
        b.align();
        b.put(0, 1); // the tile isn't empty
        b.put(1, 1); // its size follows
        b.put(3, 8);
        b.align();
        b.put(0, 1); // the macroblock isn't empty
        if (type != 0)
            b.put(0, 1);        // intra
        b.put(0, band ? 1 : 4); // no blocks coded
        b.align();
    }
    b.pad();
    return b.bytes;
}

static void testVideo() {
    using namespace reluva::video;
    // A key frame: grey; a null frame keeps it.
    Indeo5Decoder dec;
    std::vector<uint8_t> frame = indeo5GreyFrame(0);
    CHECK(!dec.decode(frame.data(), frame.size()));
    CHECK(dec.width() == 16 && dec.height() == 16);
    std::vector<uint8_t> y;
    std::vector<uint8_t> cb;
    std::vector<uint8_t> cr;
    dec.yuv(y, cb, cr);
    CHECK(y.size() == 256 && cb.size() == 16 && cr.size() == 16);
    CHECK(std::all_of(y.begin(), y.end(), [](uint8_t v) { return v == 128; }));
    CHECK(std::all_of(cb.begin(), cb.end(), [](uint8_t v) { return v == 128; }));
    std::vector<uint8_t> rgba(16 * 16 * 4);
    dec.rgba(rgba.data(), 16 * 4);
    CHECK(rgba[0] == 130 && rgba[1] == 130 && rgba[2] == 130 && rgba[3] == 255);
    frame = indeo5GreyFrame(4);
    CHECK(!dec.decode(frame.data(), frame.size()));
    frame = indeo5GreyFrame(1); // an inter frame of intra macroblocks
    CHECK(!dec.decode(frame.data(), frame.size()));
    CHECK(!dec.decode(nullptr, 0)); // a dropped frame
    // Damage: no start code; an inter frame before any key frame.
    frame[0] ^= 1;
    CHECK(dec.decode(frame.data(), frame.size()));
    Indeo5Decoder fresh;
    frame = indeo5GreyFrame(1);
    CHECK(fresh.decode(frame.data(), frame.size()));

    // An AVI: a video stream (IV50) and a PCM one, their chunks in a
    // "rec " list inside "movi".
    std::vector<uint8_t> a;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            a.push_back(uint8_t(v >> (8 * i)));
    };
    auto id = [&](const char *s) { a.insert(a.end(), s, s + 4); };
    std::vector<size_t> sizes;
    auto open = [&](const char *list, const char *type) {
        id(list);
        sizes.push_back(a.size());
        u32(0);
        if (type)
            id(type);
    };
    auto close = [&] {
        const size_t at = sizes.back();
        sizes.pop_back();
        const uint32_t n = uint32_t(a.size() - at - 4);
        for (int i = 0; i < 4; ++i)
            a[at + size_t(i)] = uint8_t(n >> (8 * i));
        if (n & 1)
            a.push_back(0);
    };
    open("RIFF", "AVI ");
    open("LIST", "hdrl");
    open("avih", nullptr); // microseconds a frame: 15 a second
    u32(66667);
    for (int i = 0; i < 13; ++i)
        u32(0);
    close();
    open("LIST", "strl");
    open("strh", nullptr);
    id("vids");
    id("IV50");
    for (int i = 0; i < 3; ++i)
        u32(0);
    u32(1);  // scale
    u32(15); // rate
    for (int i = 0; i < 9; ++i)
        u32(0);
    close();
    open("strf", nullptr);
    u32(40);
    u32(16);
    u32(16);
    for (int i = 0; i < 7; ++i)
        u32(0);
    close();
    close();
    open("LIST", "strl");
    open("strh", nullptr);
    id("auds");
    for (int i = 0; i < 13; ++i)
        u32(0);
    close();
    open("strf", nullptr);
    for (uint32_t v : {0x00020001u, 22050u, 88200u, 0x00100004u})
        u32(v);
    close();
    close();
    close(); // hdrl
    open("LIST", "movi");
    open("LIST", "rec ");
    const std::vector<uint8_t> key = indeo5GreyFrame(0);
    open("00dc", nullptr);
    a.insert(a.end(), key.begin(), key.end());
    close();
    open("01wb", nullptr);
    for (int i = 0; i < 4; ++i)
        a.insert(a.end(), {uint8_t(i), 0});
    close();
    close();
    open("00dc", nullptr); // a dropped frame
    close();
    close(); // movi
    close(); // RIFF
    avi::Decoder avi;
    CHECK(avi.load(std::make_unique<MemoryReadStream>(a)));
    CHECK(avi.width() == 16 && avi.height() == 16 && avi.videoCodec() == avi::fourcc("IV50"));
    CHECK(avi.frameMicros() > 66000 && avi.frameMicros() < 67000); // 15 a second
    std::vector<uint8_t> first;
    std::vector<uint8_t> second;
    CHECK(avi.frameCount() == 2 && avi.readFrame(0, first) && first.size() == key.size());
    CHECK(avi.readFrame(1, second) && second.empty());
    CHECK(avi.audioChannels() == 2 && avi.audioRate() == 22050 && avi.audioBits() == 16);
    CHECK(avi.audio().size() == 8 && avi.audio()[6] == 3);
    Indeo5Decoder fromAvi;
    CHECK(!fromAvi.decode(first.data(), first.size()) && fromAvi.width() == 16);
    a[0] = 'X';
    avi::Decoder damaged;
    CHECK(!damaged.load(std::make_unique<MemoryReadStream>(a)));
}

static const test::Suite kSuite("common.unit.indeo",
                                {
                                    {"indeo 5 and avi", [](const std::filesystem::path &) { testVideo(); }},
                                });
