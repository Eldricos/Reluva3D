// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// EA's TQI video (video/tqi.h) and its movies (video/ea_movie.h): frames
// built bit by bit, with MPEG-1's codes, and a movie of them with its
// sound, as docs/common/video/tqi.md describes them.
//
//   reluva-tests common.unit.tqi

#include "tests/support/test.h"
#include "audio/sound.h"
#include "video/ea_movie.h"
#include "video/tqi.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

// A frame of `width` x `height` at quantiser `q`, its codes given as a
// string of bits, packed into 32-bit little-endian words from their top.
Bytes makeFrame(int width, int height, int q, std::string bits) {
    Bytes f = {uint8_t(width), uint8_t(width >> 8), uint8_t(height), uint8_t(height >> 8), uint8_t(q), 0, 0, 0};
    while (bits.size() % 32)
        bits += '0';
    for (size_t at = 0; at < bits.size(); at += 32) {
        const uint32_t word = uint32_t(std::stoul(bits.substr(at, 32), nullptr, 2));
        for (int i = 0; i < 4; ++i)
            f.push_back(uint8_t(word >> (8 * i)));
    }
    return f;
}

// One macroblock: the first luma block's DC 100 (size 7) with an AC
// coefficient of 2 (code 0100) and an escaped one (run 3, level -5); the
// other luma blocks' DC unchanged; both chroma blocks' DC 128 (size 8).
const std::string kMacroblock = std::string("111110") + "1100100" + "0100" + "0" + "000001" + "000011" + "11111011" +
                                "10" +
                                "100"
                                "10" +
                                "100"
                                "10" +
                                "100"
                                "10" +
                                "11111110" + "10000000" + "10" + "11111110" + "10000000" + "10";

bool grey(const reluva::Surface &s, int x, int y, int value) {
    const uint8_t *p = s.row(y) + x * 4;
    return std::abs(p[0] - value) <= 1 && std::abs(p[1] - value) <= 1 && std::abs(p[2] - value) <= 1 && p[3] == 255;
}

void testFrame() {
    reluva::tqi::Decoder decoder;
    CHECK(!decoder.decode(makeFrame(16, 16, 99, kMacroblock).data(), makeFrame(16, 16, 99, kMacroblock).size()));
    CHECK(decoder.width() == 16 && decoder.height() == 16 && decoder.surface().w == 16);
    // Luma 100, chroma neutral: BT.601's grey, (100 - 16) x 1.164, in the
    // blocks without AC coefficients.
    const reluva::Surface &s = decoder.surface();
    CHECK(grey(s, 8, 0, 98) && grey(s, 0, 8, 98) && grey(s, 15, 15, 98));
    // The first block has its AC: not flat, but about 100 on average.
    int sum = 0;
    bool flat = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            sum += s.row(y)[x * 4];
            flat = flat && s.row(y)[x * 4] == s.row(0)[0];
        }
    CHECK(!flat && std::abs(sum / 64 - 98) <= 2);

    // Two macroblocks side by side: the second's DCs carry on from the
    // first's (luma 100 + 0), whatever its own codes.
    const std::string second = std::string("100") + "10" +
                               "100"
                               "10" +
                               "100"
                               "10" +
                               "100"
                               "10" +
                               "00"
                               "10" +
                               "00"
                               "10";
    const Bytes two = makeFrame(32, 16, 99, kMacroblock + second);
    CHECK(!decoder.decode(two.data(), two.size()) && decoder.width() == 32 && grey(decoder.surface(), 20, 4, 98));

    // A size not of whole macroblocks; cut short; a code that isn't one.
    const Bytes odd = makeFrame(20, 16, 99, kMacroblock);
    CHECK(decoder.decode(odd.data(), odd.size()));
    const Bytes cut = makeFrame(32, 16, 99, kMacroblock);
    CHECK(decoder.decode(cut.data(), cut.size()));
    const Bytes bad = makeFrame(16, 16, 99, std::string(32, '0'));
    CHECK(decoder.decode(bad.data(), bad.size()));
    CHECK(decoder.decode(two.data(), 5));
}

// A chunk of an EA movie: its tag, its size with these 8 bytes, its bytes.
void chunk(Bytes &out, const char *tag, const Bytes &body) {
    out.insert(out.end(), tag, tag + 4);
    const uint32_t size = uint32_t(8 + body.size());
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(size >> (8 * i)));
    out.insert(out.end(), body.begin(), body.end());
}

// A movie of two frames and a sound of 2 stereo PCM samples, the frames
// between the sound's chunks.
Bytes makeMovie() {
    const Bytes frame = makeFrame(16, 16, 99, kMacroblock);
    Bytes m;
    chunk(m, "SCHl", {'P', 'T', 0, 0, 0x82, 1, 2, 0x85, 1, 2, 0xff});
    chunk(m, "pIQT", frame);
    chunk(m, "SCCl", {1, 0, 0, 0});
    chunk(m, "SCDl", {2, 0, 0, 0, 1, 0, 0xff, 0xff, 2, 0, 0xfe, 0xff});
    chunk(m, "pIQT", frame);
    chunk(m, "SCEl", {});
    return m;
}

void testMovie() {
    reluva::video::EaMovie movie;
    const Bytes data = makeMovie();
    CHECK(reluva::video::EaMovie::looksLike(data) && !movie.open(data));
    CHECK(movie.frameCount() == 2 && movie.frameSize(1) == makeFrame(16, 16, 99, kMacroblock).size());
    reluva::tqi::Decoder decoder;
    CHECK(!decoder.decode(movie.frame(1), movie.frameSize(1)) && decoder.width() == 16);
    reluva::audio::Sound sound;
    CHECK(movie.sound().segments().size() == 1 && !movie.sound().decode(0, sound));
    CHECK(sound.channels == 2 && (sound.samples == std::vector<int16_t>{1, -1, 2, -2}));

    // Not a movie; one without pictures; a chunk running past the end.
    CHECK(!reluva::video::EaMovie::looksLike(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0}));
    Bytes soundOnly;
    chunk(soundOnly, "SCHl", {'P', 'T', 0, 0, 0xff});
    CHECK(movie.open(soundOnly));
    Bytes cut = data;
    cut.resize(cut.size() - 3);
    CHECK(movie.open(cut));
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.tqi",
                                        {
                                            {"frame", [](const std::filesystem::path &) { testFrame(); }},
                                            {"movie", [](const std::filesystem::path &) { testMovie(); }},
                                        });
