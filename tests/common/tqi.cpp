// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// EA's TQI video (video/tqi.h) and its movies (video/ea_movie.h): frames
// built bit by bit, with MPEG-1's codes, and a movie of them with its
// sound, as docs/common/video/tqi.md describes them.
//
//   reluva-tests common.unit.tqi

#include "tests/support/ea_movie_writer.h"
#include "tests/support/test.h"
#include "audio/sound.h"
#include "common/headless_system.h"
#include "video/movie.h"
#include "video/ea_movie.h"
#include "video/tqi.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using reluva::test::kTqiMacroblock;
using reluva::test::makeEaMovie;
using reluva::test::makeTqiFrame;
using reluva::test::movieChunk;

bool grey(const reluva::Surface &s, int x, int y, int value) {
    const uint8_t *p = s.row(y) + x * 4;
    return std::abs(p[0] - value) <= 1 && std::abs(p[1] - value) <= 1 && std::abs(p[2] - value) <= 1 && p[3] == 255;
}

void testFrame() {
    reluva::tqi::Decoder decoder;
    CHECK(!decoder.decode(makeTqiFrame(16, 16, 99, kTqiMacroblock).data(),
                          makeTqiFrame(16, 16, 99, kTqiMacroblock).size()));
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
    const Bytes two = makeTqiFrame(32, 16, 99, kTqiMacroblock + second);
    CHECK(!decoder.decode(two.data(), two.size()) && decoder.width() == 32 && grey(decoder.surface(), 20, 4, 98));

    // A size not of whole macroblocks; cut short; a code that isn't one.
    const Bytes odd = makeTqiFrame(20, 16, 99, kTqiMacroblock);
    CHECK(decoder.decode(odd.data(), odd.size()));
    const Bytes cut = makeTqiFrame(32, 16, 99, kTqiMacroblock);
    CHECK(decoder.decode(cut.data(), cut.size()));
    const Bytes bad = makeTqiFrame(16, 16, 99, std::string(32, '0'));
    CHECK(decoder.decode(bad.data(), bad.size()));
    CHECK(decoder.decode(two.data(), 5));
}

void testMovie() {
    reluva::video::EaMovie movie;
    const Bytes data = makeEaMovie();
    CHECK(reluva::video::EaMovie::looksLike(data) && !movie.open(data));
    CHECK(movie.frameCount() == 2 && movie.frameSize(1) == makeTqiFrame(16, 16, 99, kTqiMacroblock).size());
    reluva::tqi::Decoder decoder;
    CHECK(!decoder.decode(movie.frame(1), movie.frameSize(1)) && decoder.width() == 16);
    reluva::audio::Sound sound;
    CHECK(movie.sound().segments().size() == 1 && !movie.sound().decode(0, sound));
    CHECK(sound.channels == 2 && (sound.samples == std::vector<int16_t>{1, -1, 2, -2}));

    // Not a movie; one without pictures; a chunk running past the end.
    CHECK(!reluva::video::EaMovie::looksLike(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0}));
    Bytes soundOnly;
    movieChunk(soundOnly, "SCHl", {'P', 'T', 0, 0, 0xff});
    CHECK(movie.open(soundOnly));
    Bytes cut = data;
    cut.resize(cut.size() - 3);
    CHECK(movie.open(cut));
}

// The movie played, with no mixer, by the clock: its 16x16 picture shown
// stretched to 32x16, in the middle of a 40x20 screen on black.
void testPlay(const std::filesystem::path &tmp) {
    const Bytes data = makeEaMovie();
    std::ofstream(tmp / "logo.tgq", std::ios::binary)
        .write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    struct Screen : reluva::HeadlessSystem {
        reluva::Surface last;
        void updateScreen(const reluva::Surface &s) override {
            HeadlessSystem::updateScreen(s);
            last = s;
        }
    } sys;
    reluva::video::MovieOptions options;
    options.screenWidth = 40;
    options.screenHeight = 20;
    options.pictureWidth = 32;
    options.pictureHeight = 16;
    bool quit = true;
    CHECK(!reluva::video::playMovie(sys, nullptr, tmp / "logo.tgq", quit, options) && !quit);
    CHECK(sys.width() == 40 && sys.height() == 20 && sys.last.w == 40);
    // The picture from (4, 2) to (35, 17): its right half, grey, at 20-35.
    auto black = [&](int x, int y) {
        const uint8_t *p = sys.last.row(y) + x * 4;
        return p[0] == 0 && p[1] == 0 && p[2] == 0;
    };
    CHECK(grey(sys.last, 20, 2, 98) && grey(sys.last, 35, 17, 98) && black(3, 2) && black(36, 2) && black(20, 1));
    CHECK(sys.getMillis() >= 66); // the second frame is due at 1/15 s
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.tqi",
                                        {
                                            {"frame", [](const std::filesystem::path &) { testFrame(); }},
                                            {"movie", [](const std::filesystem::path &) { testMovie(); }},
                                            {"play", [](const std::filesystem::path &tmp) { testPlay(tmp); }},
                                        });
