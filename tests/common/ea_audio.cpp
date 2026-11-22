// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Electronic Arts' audio (audio/ea_audio.h, audio/ea_stream.h): EA-XA
// frames written by hand, MicroTalk against a reference decoder's values,
// PT headers and sound streams built in the test.
//
//   reluva-tests common.unit.ea_audio

#include "audio/ea_audio.h"
#include "audio/ea_stream.h"
#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using reluva::audio::EaSoundStream;
using reluva::audio::parseSoundHeader;
using reluva::audio::SoundHeader;

void put16(Bytes &b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(Bytes &b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

// A PT header of the tags, after a patch tag and a section byte, as Need
// for Speed III's.
Bytes ptHeader(const std::vector<std::pair<uint8_t, uint32_t>> &tags) {
    Bytes b = {'P', 'T', 0, 0, 0x06, 1, 25, 0xfd};
    for (auto [tag, value] : tags) {
        const int n = value > 0xffffff ? 4 : value > 0xffff ? 3 : value > 0xff ? 2 : 1;
        b.push_back(tag);
        b.push_back(uint8_t(n));
        for (int i = n - 1; i >= 0; --i)
            b.push_back(uint8_t(value >> (8 * i)));
    }
    b.push_back(0xff);
    return b;
}

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

void testStreams() {
    // A stream of two segments: EA-XA stereo (one block of 30 samples:
    // two groups) and PCM stereo (2 samples); between them a chunk of
    // something else, as a movie's pictures are.
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
    chunk(mus, "pIQT", Bytes(12, 7));
    const uint32_t second = uint32_t(mus.size());
    chunk(mus, "SCHl", ptHeader({{0x82, 2}, {0x85, 2}}));
    chunk(mus, "SCCl", count);
    Bytes pcm;
    put32(pcm, 2);
    for (int v : {1, -1, 2, -2})
        put16(pcm, unsigned(v));
    chunk(mus, "SCDl", pcm);
    chunk(mus, "SCEl", {});

    EaSoundStream stream;
    CHECK(!stream.open(mus));
    CHECK(stream.segments().size() == 2 && stream.segmentAt(second) == 1 && stream.segmentAt(4) == -1);
    reluva::audio::Sound s;
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
}

// A PT header: section bytes passed over, values big-endian, defaults for
// what isn't given; where it ends.
void testSoundHeader() {
    const Bytes h = ptHeader({{0x82, 2}, {0x83, 7}, {0x84, 44100}, {0x85, 70000}, {0x86, 5}, {0x87, 9}});
    SoundHeader out;
    size_t end = 0;
    CHECK(!parseSoundHeader(h.data(), h.size(), 0, out, &end));
    CHECK(out.channels == 2 && out.compression == SoundHeader::kEaXa && out.rate == 44100 && out.samples == 70000);
    CHECK(out.loopStart == 5 && out.loopEnd == 9 && end == h.size() && out.tags.at(0x06) == 25);
    const Bytes plain = ptHeader({});
    CHECK(!parseSoundHeader(plain.data(), plain.size(), 0, out));
    CHECK(out.channels == 1 && out.compression == SoundHeader::kPcm16 && out.rate == 22050);
    // Cut short, three channels, not a PT header.
    CHECK(parseSoundHeader(h.data(), h.size() - 2, 0, out));
    const Bytes three = ptHeader({{0x82, 3}});
    CHECK(parseSoundHeader(three.data(), three.size(), 0, out));
    CHECK(parseSoundHeader(h.data(), h.size(), 1, out));
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.ea_audio",
                                        {
                                            {"ea-xa", [](const std::filesystem::path &) { testEaXa(); }},
                                            {"microtalk", [](const std::filesystem::path &) { testMicroTalk(); }},
                                            {"sound header", [](const std::filesystem::path &) { testSoundHeader(); }},
                                            {"streams", [](const std::filesystem::path &) { testStreams(); }},
                                        });
