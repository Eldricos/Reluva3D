// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Electronic Arts' audio codecs (audio/ea_audio.h): EA-XA frames written by
// hand, and MicroTalk against a reference decoder's values.
//
//   reluva-tests common.unit.ea_audio

#include "audio/ea_audio.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace {

void testEaXa() {
    using namespace reluva::audio;
    // Filter 0, shift 12: each residual as it is (x 2^0); +1, -1, +7, -8.
    uint8_t frame[15] = {0x0c, 0x1f, 0x78};
    int16_t out[28] = {};
    EaXaChannel ch;
    decodeEaXaFrame(frame, ch, out, 28);
    CHECK(out[0] == 1 && out[1] == -1 && out[2] == 7 && out[3] == -8 && out[4] == 0);
    // Shift 0: x 4096; 0x7 -> 28672, then filter 1 (240/256 of the
    // previous) with residual 0.
    uint8_t loud[15] = {0x00, 0x70};
    decodeEaXaFrame(loud, ch, out, 2);
    CHECK(out[0] == 28672 && out[1] == 0);
    uint8_t predict[15] = {0x1c, 0x00};
    decodeEaXaFrame(predict, ch, out, 1);
    CHECK(out[0] == (240 * 0 + 128) >> 8);
    EaXaChannel fresh;
    fresh.previous = 1000;
    decodeEaXaFrame(predict, fresh, out, 1);
    CHECK(out[0] == (240 * 1000 + 128) >> 8);
    // Clamped, and a stride for interleaving.
    EaXaChannel c2;
    c2.previous = 30000;
    int16_t wide[4] = {};
    uint8_t clip1[15] = {0x10, 0x77};
    decodeEaXaFrame(clip1, c2, wide, 2, 2);
    CHECK(wide[0] == 32767 && wide[1] == 0 && wide[2] == 32767);
    // A stream: 30 samples need two frames.
    std::vector<uint8_t> two(30, 0);
    two[0] = 0x0c;
    two[1] = 0x10;
    std::vector<int16_t> samples;
    CHECK(!decodeEaXa(two.data(), two.size(), 30, samples));
    CHECK(samples.size() == 30 && samples[0] == 1);
    CHECK(decodeEaXa(two.data(), 29, 30, samples)); // short
}
void testMicroTalk() {
    using namespace reluva::audio;
    std::vector<uint8_t> stream;
    uint32_t x = 12345;
    for (int i = 0; i < 400; ++i) {
        x = (x * 1103515245u + 12345u) & 0x7fffffffu;
        stream.push_back(uint8_t(x >> 16));
    }
    std::vector<int16_t> out;
    CHECK(!decodeMicroTalk(stream.data(), stream.size(), 1296, out));
    CHECK(out.size() == 1296);
    if (out.size() == 1296) {
        CHECK(out[0] == -253 && out[1] == -9 && out[100] == -2462 && out[431] == 1464);
        CHECK(out[432] == 659 && out[700] == -3404 && out[1000] == 6988 && out[1295] == -1254);
        uint32_t hash = 0;
        for (int16_t v : out)
            hash = hash * 31 + uint16_t(v);
        CHECK(hash == 68820045u);
    }
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.ea_audio", {
    {"ea-xa", [](const std::filesystem::path&) { testEaXa(); }},
    {"microtalk", [](const std::filesystem::path&) { testMicroTalk(); }},
});
