// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The tracker-music player (audio/tracker.h) on MOD, S3M, XM and IT
// modules built here: their notes, effects and loops as sound.
//
//   reluva-tests common.unit.tracker

#include "audio/sound.h"
#include "audio/tracker.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::audio;

// Tracker modules built in memory: one square-wave sample, a few patterns.
namespace trackertest {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, size_t at, unsigned v) {
    if (b.size() < at + 2)
        b.resize(at + 2);
    b[at] = uint8_t(v), b[at + 1] = uint8_t(v >> 8);
}
void put32(Bytes &b, size_t at, uint32_t v) {
    put16(b, at, v & 0xffff);
    put16(b, at + 2, v >> 16);
}
void putText(Bytes &b, size_t at, const char *s) {
    for (size_t i = 0; s[i]; ++i) {
        if (b.size() <= at + i)
            b.resize(at + i + 1);
        b[at + i] = uint8_t(s[i]);
    }
}
void align16(Bytes &b) {
    b.resize((b.size() + 15) / 16 * 16);
}

constexpr int kFrames = 300; // the sample's length
int square(int i) {
    return i / 8 % 2 ? 48 : -48;
}

struct Note {
    // rows count on across patterns: row 64 is pattern 1's first
    int row;
    int channel;
    int note = 0;    // 61 = C-5 (MOD: a period); 254 cut, 255 off
    int volume = -1; // the volume column, -1 none
    // in the format's own numbering
    int fx = 0;
    int param = 0;
};

// 4-channel M.K.; notes are periods.
Bytes mod(const std::vector<int> &orders, int patterns, const std::vector<Note> &notes) {
    Bytes b(1084 + size_t(patterns) * 1024);
    putText(b, 0, "test");
    b[42] = kFrames / 2 >> 8, b[43] = kFrames / 2 & 255; // sample 1: length in words
    b[45] = 64;                                          // volume
    b[49] = 1;                                           // loop length 1 word: none
    b[950] = uint8_t(orders.size());
    b[951] = 127;
    for (size_t i = 0; i < orders.size(); ++i)
        b[952 + i] = uint8_t(orders[i]);
    putText(b, 1080, "M.K.");
    for (const Note &n : notes) {
        const size_t at = 1084 + size_t(n.row / 64) * 1024 + size_t(n.row % 64) * 16 + size_t(n.channel) * 4;
        const int sample = n.note ? 1 : 0;
        b[at] = uint8_t(n.note >> 8);
        b[at + 1] = uint8_t(n.note);
        b[at + 2] = uint8_t(sample << 4 | n.fx);
        b[at + 3] = uint8_t(n.param);
    }
    for (int i = 0; i < kFrames; ++i)
        b.push_back(uint8_t(square(i)));
    return b;
}

// Unsigned 8-bit sample, looping when `loop`.
Bytes s3m(const std::vector<int> &orders, int patterns, const std::vector<Note> &notes, int speed, int tempo,
          bool loop) {
    Bytes b(0x60);
    putText(b, 0, "test");
    b[0x1C] = 0x1A, b[0x1D] = 16;
    put16(b, 0x20, unsigned(orders.size()));
    put16(b, 0x22, 1);
    put16(b, 0x24, unsigned(patterns));
    put16(b, 0x28, 0x1320);
    put16(b, 0x2A, 2);
    putText(b, 0x2C, "SCRM");
    b[0x30] = 64, b[0x31] = uint8_t(speed), b[0x32] = uint8_t(tempo), b[0x33] = 0xB0;
    for (int c = 0; c < 32; ++c)
        b[0x40 + size_t(c)] = c < 4 ? uint8_t(c % 2 ? 8 + c : c) : 255;
    for (int o : orders)
        b.push_back(uint8_t(o));
    const size_t ptrs = b.size();
    b.resize(b.size() + 2 + 2 * size_t(patterns));
    align16(b);
    const size_t ins = b.size();
    put16(b, ptrs, unsigned(ins / 16));
    b.resize(ins + 0x50);
    b[ins] = 1;
    put32(b, ins + 0x10, kFrames);
    put32(b, ins + 0x18, kFrames);
    b[ins + 0x1C] = 64;
    b[ins + 0x1F] = loop ? 1 : 0;
    put32(b, ins + 0x20, 8363);
    putText(b, ins + 0x4C, "SCRS");
    align16(b);
    put16(b, ins + 14, unsigned(b.size() / 16));
    for (int i = 0; i < kFrames; ++i)
        b.push_back(uint8_t(square(i) + 128));
    for (int p = 0; p < patterns; ++p) {
        align16(b);
        put16(b, ptrs + 2 + 2 * size_t(p), unsigned(b.size() / 16));
        const size_t start = b.size();
        b.resize(start + 2);
        for (int row = 0; row < 64; ++row) {
            for (const Note &n : notes) {
                if (n.row != p * 64 + row)
                    continue;
                b.push_back(uint8_t(n.channel | (n.note ? 32 : 0) | (n.volume >= 0 ? 64 : 0) | (n.fx ? 128 : 0)));
                if (n.note) {
                    b.push_back(uint8_t(n.note >= 254 ? n.note : (n.note - 13) / 12 << 4 | (n.note - 13) % 12));
                    b.push_back(n.note >= 254 ? 0 : 1);
                }
                if (n.volume >= 0)
                    b.push_back(uint8_t(n.volume));
                if (n.fx) {
                    b.push_back(uint8_t(n.fx));
                    b.push_back(uint8_t(n.param));
                }
            }
            b.push_back(0);
        }
        put16(b, start, unsigned(b.size() - start));
    }
    return b;
}

// 2 channels, linear slides, one instrument with a looping 16-bit sample;
// `envelope`: a volume envelope falling from 64 to 0 over 10 ticks.
Bytes xm(const std::vector<int> &orders, int patterns, const std::vector<Note> &notes, bool envelope) {
    Bytes b(60 + 276);
    putText(b, 0, "Extended Module: test");
    b[37] = 0x1A;
    put16(b, 58, 0x0104);
    put32(b, 60, 276);
    put16(b, 64, unsigned(orders.size()));
    put16(b, 68, 2);
    put16(b, 70, unsigned(patterns));
    put16(b, 72, 1);
    put16(b, 74, 1);
    put16(b, 76, 6);
    put16(b, 78, 125);
    for (size_t i = 0; i < orders.size(); ++i)
        b[80 + i] = uint8_t(orders[i]);
    for (int p = 0; p < patterns; ++p) {
        const size_t h = b.size();
        b.resize(h + 9);
        put32(b, h, 9);
        put16(b, h + 5, 64);
        for (int row = 0; row < 64; ++row)
            for (int c = 0; c < 2; ++c) {
                const Note *found = nullptr;
                for (const Note &n : notes)
                    if (n.row == p * 64 + row && n.channel == c)
                        found = &n;
                if (!found) {
                    b.push_back(0x80);
                    continue;
                }
                b.push_back(uint8_t(found->note == 255 ? 97 : found->note ? found->note - 12 : 0));
                b.push_back(found->note && found->note != 255 ? 1 : 0);
                b.push_back(uint8_t(found->volume >= 0 ? 0x10 + found->volume : 0));
                b.push_back(uint8_t(found->fx));
                b.push_back(uint8_t(found->param));
            }
        put16(b, h + 7, unsigned(b.size() - h - 9));
    }
    const size_t ins = b.size();
    b.resize(ins + 263);
    put32(b, ins, 263);
    put16(b, ins + 27, 1);
    put32(b, ins + 29, 40);
    if (envelope) {
        put16(b, ins + 129, 0), put16(b, ins + 131, 64), put16(b, ins + 133, 10), put16(b, ins + 135, 0);
        b[ins + 225] = 2;
        b[ins + 233] = 1;
    }
    const size_t smp = b.size();
    b.resize(smp + 40);
    put32(b, smp, kFrames * 2);
    put32(b, smp + 8, kFrames * 2);
    b[smp + 12] = 64;
    b[smp + 14] = 0x10 | 1; // 16-bit, forward loop
    b[smp + 15] = 128;
    int old = 0;
    for (int i = 0; i < kFrames; ++i) {
        const int v = square(i) * 256;
        b.push_back(uint8_t((v - old) & 0xff));
        b.push_back(uint8_t((v - old) >> 8 & 0xff));
        old = v;
    }
    return b;
}

// Instrument mode, linear slides, one instrument mapping every note to a
// looping IT 2.14-compressed 8-bit sample.
Bytes it(const std::vector<int> &orders, int patterns, const std::vector<Note> &notes) {
    Bytes b(0xC0);
    putText(b, 0, "IMPMtest");
    put16(b, 0x20, unsigned(orders.size()));
    put16(b, 0x22, 1);
    put16(b, 0x24, 1);
    put16(b, 0x26, unsigned(patterns));
    put16(b, 0x28, 0x214);
    put16(b, 0x2A, 0x214);
    put16(b, 0x2C, 1 | 4 | 8);
    b[0x30] = 128, b[0x31] = 64, b[0x32] = 6, b[0x33] = 125, b[0x34] = 128;
    for (int c = 0; c < 64; ++c)
        b[0x40 + size_t(c)] = 32, b[0x80 + size_t(c)] = 64;
    for (int o : orders)
        b.push_back(uint8_t(o));
    const size_t ptrs = b.size();
    b.resize(ptrs + 4 * (2 + size_t(patterns)));
    const size_t ins = b.size();
    put32(b, ptrs, uint32_t(ins));
    b.resize(ins + 554);
    putText(b, ins, "IMPI");
    b[ins + 0x18] = 128, b[ins + 0x19] = 0x80 | 32;
    for (size_t n = 0; n < 120; ++n)
        b[ins + 0x40 + 2 * n] = uint8_t(n), b[ins + 0x41 + 2 * n] = 1;
    const size_t smp = b.size();
    put32(b, ptrs + 4, uint32_t(smp));
    b.resize(smp + 0x50);
    putText(b, smp, "IMPS");
    b[smp + 0x11] = 64, b[smp + 0x12] = 1 | 8 | 0x10, b[smp + 0x13] = 64, b[smp + 0x2E] = 1, b[smp + 0x2F] = 32;
    put32(b, smp + 0x30, kFrames);
    put32(b, smp + 0x38, kFrames);
    put32(b, smp + 0x3C, 8363);
    put32(b, smp + 0x48, uint32_t(b.size()));
    // One block of 9-bit deltas (bit 8 clear: a value, not a width change).
    Bytes bits((kFrames * 9 + 7) / 8);
    for (int i = 0, bit = 0; i < kFrames; ++i) {
        const unsigned v = uint8_t(square(i) - (i ? square(i - 1) : 0));
        for (int k = 0; k < 9; ++k, ++bit)
            bits[size_t(bit / 8)] |= uint8_t((v >> k & 1) << (bit % 8));
    }
    put16(b, b.size(), unsigned(bits.size()));
    b.insert(b.end(), bits.begin(), bits.end());
    for (int p = 0; p < patterns; ++p) {
        const size_t h = b.size();
        put32(b, ptrs + 8 + 4 * size_t(p), uint32_t(h));
        b.resize(h + 8);
        put16(b, h + 2, 64);
        for (int row = 0; row < 64; ++row) {
            for (const Note &n : notes) {
                if (n.row != p * 64 + row)
                    continue;
                const bool real = n.note && n.note < 254;
                b.push_back(uint8_t((n.channel + 1) | 0x80));
                b.push_back(uint8_t((n.note ? 1 : 0) | (real ? 2 : 0) | (n.volume >= 0 ? 4 : 0) | (n.fx ? 8 : 0)));
                if (n.note)
                    b.push_back(uint8_t(n.note >= 254 ? n.note : n.note - 1));
                if (real)
                    b.push_back(1);
                if (n.volume >= 0)
                    b.push_back(uint8_t(n.volume));
                if (n.fx) {
                    b.push_back(uint8_t(n.fx));
                    b.push_back(uint8_t(n.param));
                }
            }
            b.push_back(0);
        }
        put16(b, h, unsigned(b.size() - h - 8));
    }
    return b;
}

// The loudest sample in frames [from, to).
int peak(const audio::Sound &s, size_t from, size_t to) {
    int p = 0;
    for (size_t i = from * 2; i < std::min(to * 2, s.samples.size()); ++i)
        p = std::max(p, std::abs(int(s.samples[i])));
    return p;
}

} // namespace trackertest

void testTracker() {
    using namespace reluva::audio;
    using namespace trackertest;
    constexpr size_t kTick = 882;
    constexpr size_t kRow = 6 * kTick;
    constexpr size_t kPattern = 64 * kRow; // speed 6, tempo 125, 44100 Hz
    TrackerModule m;
    Sound s;

    // MOD: a C-2 (period 428) at the start of a one-pattern song, which then
    // loops to its start. The 300-frame sample at 8363 Hz lasts ~1582 frames.
    const Bytes modBytes = mod({0}, 1, {{0, 0, 428}});
    CHECK(!loadTrackerModule(modBytes, m));
    CHECK(trackerFormat(m) == "MOD" && m.channels == 4 && m.samples.size() == 31);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[8] == 48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.rate == 44100 && s.channels == 2 && s.frames() == kPattern);
    CHECK(s.loopStart == 0 && s.loopEnd == kPattern);
    CHECK(peak(s, 0, 1000) > 1000 && peak(s, 1700, kPattern) == 0);
    CHECK(s.samples[0] != 0 && s.samples[1] == 0); // channel 1 is hard left
    // maxSeconds caps the render.
    CHECK(!renderTrackerModule(m, 44100, 0, 0.5, s) && s.frames() == 22050 && s.loopStart == 0);

    // Cxx scales the volume.
    Sound full;
    Sound half;
    CHECK(!loadTrackerModule(mod({0}, 1, {{0, 0, 428, -1, 0xC, 0x40}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, full));
    CHECK(!loadTrackerModule(mod({0}, 1, {{0, 0, 428, -1, 0xC, 0x20}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, half));
    const int pf = peak(full, 0, 1000);
    const int ph = peak(half, 0, 1000);
    CHECK(ph > 0 && std::abs(pf - 2 * ph) <= 2);

    // A position jump back to order 1 from order 2's first row: the loop
    // starts at order 1. Starting at order 1 plays from there.
    CHECK(!loadTrackerModule(mod({0, 1, 2}, 3, {{0, 0, 428}, {128, 0, 0, -1, 0xB, 1}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 2 * kPattern + kRow && s.loopStart == kPattern);
    CHECK(!renderTrackerModule(m, 44100, 1, 60, s));
    CHECK(s.frames() == kPattern + kRow && s.loopStart == 0 && peak(s, 0, s.frames()) == 0);
    // Speed 3 (F03) and a pattern break (D62) at row 1 to order 1's row 62.
    CHECK(!loadTrackerModule(mod({0, 1}, 2, {{0, 0, 0, -1, 0xF, 3}, {1, 0, 0, -1, 0xD, 0x62}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 4 * 3 * kTick && s.loopStart == 0);

    // S3M: speed 3, tempo 150 (735 frames a tick); a looping sample cut (^^) at row 4.
    const Bytes s3mBytes = s3m({0, 255}, 1, {{0, 0, 61}, {4, 0, 254}}, 3, 150, true);
    CHECK(!loadTrackerModule(s3mBytes, m));
    CHECK(trackerFormat(m) == "S3M" && m.channels == 4 && m.samples[0].loop);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[0] == -48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 64 * 3 * 735 && s.loopStart == 0);
    CHECK(peak(s, 0, 4 * 3 * 735) > 1000 && peak(s, 4 * 3 * 735, s.frames()) == 0);
    // Speed (A02) and tempo (T250) from the pattern; the volume column.
    CHECK(!loadTrackerModule(s3m({0}, 1, {{0, 0, 61, 32, 1, 2}, {1, 1, 0, -1, 20, 250}}, 6, 125, true), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 2 * kTick + 63 * 2 * 441);
    CHECK(peak(s, 0, 1000) > 0 && peak(s, 0, 1000) < 9000);

    // XM: a looping 16-bit sample keyed off at row 8 (no envelope: a cut).
    const Bytes xmBytes = xm({0}, 1, {{0, 0, 61}, {8, 0, 255}}, false);
    CHECK(!loadTrackerModule(xmBytes, m));
    CHECK(trackerFormat(m) == "XM" && m.instruments.size() == 1 && m.samples.size() == 1 && m.linearSlides);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[8] == 48 * 256 && m.samples[0].data[0] == -48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == kPattern && peak(s, 7 * kRow, 8 * kRow) > 1000 && peak(s, 8 * kRow + 1, kPattern) == 0);
    // A volume column of 32 halves it; the envelope silences it after 10 ticks.
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61, 32}}, false), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, half));
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61}}, false), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, full));
    CHECK(peak(half, 0, 1000) > 0 && std::abs(peak(full, 0, 1000) - 2 * peak(half, 0, 1000)) <= 2);
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61}}, true), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(peak(s, 0, kTick) > 1000 && peak(s, 4 * kTick, 5 * kTick) < peak(s, 0, kTick) &&
          peak(s, 11 * kTick, kPattern) == 0);

    // IT: instruments, a compressed sample, a note cut at row 4, and a jump
    // (B00) back to order 0 at order 1's row 2.
    const Bytes itBytes = it({0, 1, 255}, 2, {{0, 0, 61}, {4, 0, 254}, {66, 1, 0, -1, 2, 0}});
    CHECK(!loadTrackerModule(itBytes, m));
    CHECK(trackerFormat(m) == "IT" && m.instruments.size() == 1 && m.samples.size() == 1);
    bool same = m.samples[0].data.size() == kFrames;
    for (int i = 0; same && i < kFrames; ++i)
        same = m.samples[0].data[size_t(i)] == square(i) * 256;
    CHECK(same);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == kPattern + 3 * kRow && s.loopStart == 0);
    CHECK(peak(s, 0, 4 * kRow) > 1000 && peak(s, 4 * kRow, s.frames()) == 0);
    CHECK(s.samples[0] == s.samples[1]); // channel pan 32: the middle

    // Garbage and truncations of the modules: an error or a module, never a crash.
    Bytes junk(4000);
    uint32_t seed = 1;
    for (uint8_t &b : junk)
        b = uint8_t((seed = seed * 1103515245 + 12345) >> 16);
    CHECK(bool(loadTrackerModule(junk, m)));
    CHECK(bool(loadTrackerModule({}, m)));
    CHECK(bool(loadTrackerModule(Bytes(modBytes.begin(), modBytes.begin() + 1500), m))); // patterns cut
    CHECK(!loadTrackerModule(Bytes(modBytes.begin(), modBytes.end() - 100), m));         // a sample cut: kept
    for (const Bytes *whole : {&modBytes, &s3mBytes, &xmBytes, &itBytes})
        for (size_t n = 0; n < whole->size(); n += 7) {
            Bytes cut(whole->begin(), whole->begin() + long(n));
            for (size_t i = 0x40; i < cut.size(); i += 61) // and some bytes changed
                cut[i] ^= 0x5A;
            if (!loadTrackerModule(cut, m))
                renderTrackerModule(m, 8000, 0, 0.2, s);
        }
    CHECK(!loadTrackerModule(modBytes, m) && bool(renderTrackerModule(m, 44100, 99, 1, s))); // no such order
}

static const reluva::test::Suite kSuite("common.unit.tracker",
                                        {
                                            {"modules", [](const std::filesystem::path &) { testTracker(); }},
                                        });
