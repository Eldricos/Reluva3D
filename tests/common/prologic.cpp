// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Pro Logic decoder model (audio/prologic.h), on synthetic
// signals.

#include "audio/prologic.h"
#include "tests/support/test.h"

#include <filesystem>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using namespace reluva::audio;

namespace {

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

struct Levels {
    double db[4]; // L, C, R, S, dB relative to full scale
    double direction, dominance, steering;
};

// One second of a source at `angle` on the matrix circle (0 L, 45 C, 90 R,
// 135 S: Lt = cos, Rt = sin), a tone of `hz`, through the decoder; levels
// over the last half second.
Levels decode(double angle, double hz, bool active, double amplitude = 0.5) {
    ProLogicOptions o;
    o.active = active;
    ProLogicDecoder dec(kRate, o);
    const double a = angle * kPi / 180;
    std::vector<float> in(2 * kRate), out(4 * kRate);
    for (int i = 0; i < kRate; ++i) {
        const double v = amplitude * std::sin(2 * kPi * hz * i / kRate);
        in[2 * i] = float(std::cos(a) * v);
        in[2 * i + 1] = float(std::sin(a) * v);
    }
    dec.process(in.data(), out.data(), kRate);
    Levels lv{};
    for (int ch = 0; ch < 4; ++ch) {
        double e = 0;
        for (int i = kRate / 2; i < kRate; ++i)
            e += double(out[4 * i + ch]) * out[4 * i + ch];
        lv.db[ch] = 10 * std::log10(e / (kRate / 2) + 1e-20);
    }
    lv.direction = dec.direction();
    lv.dominance = dec.dominance();
    lv.steering = dec.steering();
    return lv;
}

void testCentre() {
    const Levels a = decode(45, 1000, true);
    CHECK(a.steering > 0.99);
    CHECK(std::fabs(a.direction - 45) < 1);
    CHECK(a.db[ProLogicDecoder::C] - a.db[ProLogicDecoder::L] > 40);
    CHECK(a.db[ProLogicDecoder::C] - a.db[ProLogicDecoder::R] > 40);
    CHECK(a.db[ProLogicDecoder::C] - a.db[ProLogicDecoder::S] > 40);
    // A 0.5 sine is -9 dB; the centre gets it all.
    CHECK(std::fabs(a.db[ProLogicDecoder::C] + 9.03) < 0.1);
    const Levels p = decode(45, 1000, false);
    CHECK(std::fabs(p.db[ProLogicDecoder::C] - p.db[ProLogicDecoder::L] - 3.01) < 0.1);
    CHECK(p.db[ProLogicDecoder::C] - p.db[ProLogicDecoder::S] > 100);
}

void testLeft() {
    const Levels a = decode(0, 1000, true);
    CHECK(std::fabs(a.direction) < 1 || std::fabs(a.direction - 180) < 1);
    CHECK(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::C] > 40);
    CHECK(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::S] > 40);
    CHECK(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::R] > 40);
    const Levels p = decode(0, 1000, false);
    CHECK(std::fabs(p.db[ProLogicDecoder::L] - p.db[ProLogicDecoder::C] - 3.01) < 0.1);
    CHECK(std::fabs(p.db[ProLogicDecoder::L] - p.db[ProLogicDecoder::S] - 3.01) < 0.2);
}

void testSurround() {
    // Lt = -Rt.
    const Levels a = decode(135, 1000, true);
    CHECK(std::fabs(a.direction - 135) < 1);
    CHECK(a.db[ProLogicDecoder::S] - a.db[ProLogicDecoder::L] > 40);
    CHECK(a.db[ProLogicDecoder::S] - a.db[ProLogicDecoder::C] > 40);
    CHECK(a.db[ProLogicDecoder::S] - a.db[ProLogicDecoder::R] > 40);
    CHECK(std::fabs(a.db[ProLogicDecoder::S] + 9.03) < 0.2);
    // Passive: the front gets it 3 dB down, the centre not at all.
    const Levels p = decode(135, 1000, false);
    CHECK(std::fabs(p.db[ProLogicDecoder::S] - p.db[ProLogicDecoder::L] - 3.01) < 0.2);
    CHECK(p.db[ProLogicDecoder::S] - p.db[ProLogicDecoder::C] > 100);
    // The 7 kHz low-pass: 3 dB down at 7 kHz, far more at 14.
    const Levels hi = decode(135, 7000, true), higher = decode(135, 14000, true);
    CHECK(std::fabs(a.db[ProLogicDecoder::S] - hi.db[ProLogicDecoder::S] - 3.01) < 0.5);
    CHECK(a.db[ProLogicDecoder::S] - higher.db[ProLogicDecoder::S] > 11);
}

void testSurroundDelay() {
    ProLogicDecoder dec(kRate); // 20 ms
    std::vector<float> in(2 * 4000, 0.0f), out(4 * 4000);
    for (int i = 100; i < 4000; ++i) {
        in[2 * i] = 0.5f;
        in[2 * i + 1] = -0.5f;
    }
    dec.process(in.data(), out.data(), 4000);
    int first = -1;
    for (int i = 0; i < 4000 && first < 0; ++i)
        if (std::fabs(out[4 * i + ProLogicDecoder::S]) > 1e-4)
            first = i;
    CHECK(first == 100 + 882); // 20 ms at 44.1 kHz
}

void testPanned() {
    // Halfway between left and centre: both speakers equal, at -3 dB.
    const Levels a = decode(22.5, 1000, true);
    CHECK(std::fabs(a.direction - 22.5) < 1);
    CHECK(std::fabs(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::C]) < 0.5);
    CHECK(std::fabs(a.db[ProLogicDecoder::L] + 9.03 + 3.01) < 0.3);
    CHECK(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::R] > 40);
    CHECK(a.db[ProLogicDecoder::L] - a.db[ProLogicDecoder::S] > 40);
    // Between right and surround (Lt small and opposite, Rt large).
    const Levels b = decode(112.5, 1000, true);
    CHECK(std::fabs(b.direction - 112.5) < 1);
    CHECK(std::fabs(b.db[ProLogicDecoder::R] - b.db[ProLogicDecoder::S]) < 0.5);
    CHECK(b.db[ProLogicDecoder::R] - b.db[ProLogicDecoder::C] > 40);
    // A sound behind, slightly to the left as NFS places it (one level
    // negated, the other smaller): mostly surround, some left.
    const Levels c = decode(150, 1000, true);
    CHECK(c.db[ProLogicDecoder::S] > c.db[ProLogicDecoder::L]);
    CHECK(c.db[ProLogicDecoder::S] - c.db[ProLogicDecoder::C] > 40);
}

void testNoDominance() {
    // Independent noise in Lt and Rt: nothing to steer, passive levels.
    ProLogicDecoder dec(kRate);
    std::mt19937 rng(1);
    std::normal_distribution<float> n(0, 0.1f);
    std::vector<float> in(2 * kRate), out(4 * kRate);
    for (float &v : in)
        v = n(rng);
    dec.process(in.data(), out.data(), kRate);
    CHECK(dec.dominance() < 0.2);
    CHECK(dec.steering() == 0);
}

void testSwitch() {
    // A centre tone, then a surround one: the steering follows within
    // tens of milliseconds.
    ProLogicDecoder dec(kRate);
    const int n = kRate;
    std::vector<float> in(2 * n), out(4 * n);
    for (int i = 0; i < n; ++i) {
        const float v = 0.5f * float(std::sin(2 * kPi * 500 * i / kRate));
        in[2 * i] = v;
        in[2 * i + 1] = i < n / 2 ? v : -v;
    }
    std::vector<double> dirs;
    for (int i = 0; i < n; i += 441) {
        dec.process(in.data() + 2 * i, out.data() + 4 * i, 441);
        dirs.push_back(dec.direction());
    }
    CHECK(std::fabs(dirs[45] - 45) < 1);       // 0.45 s: centre
    CHECK(std::fabs(dirs[50 + 10] - 135) < 2); // 0.1 s after the switch: surround
}

void testLayout() {
    const float lcrs[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    int16_t o6[6], o4[4], o2[2];
    layOutChannels(lcrs, 1, 6, o6);
    CHECK(o6[0] == 3277 && o6[1] == 9830 && o6[2] == 6554 && o6[3] == 0);
    CHECK(o6[4] == 9268 && o6[5] == 9268);
    layOutChannels(lcrs, 1, 4, o4);
    CHECK(o4[0] == 3277 + 4634 && o4[2] == 9268 && o4[3] == 9268);
    layOutChannels(lcrs, 1, 2, o2);
    CHECK(std::abs(o2[0] - (3277 + 4634 + 6554)) <= 1);
}

} // namespace

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.prologic", {
                                       {"centre", [](const std::filesystem::path &) { testCentre(); }},
                                       {"left", [](const std::filesystem::path &) { testLeft(); }},
                                       {"surround", [](const std::filesystem::path &) { testSurround(); }},
                                       {"surround delay", [](const std::filesystem::path &) { testSurroundDelay(); }},
                                       {"panned", [](const std::filesystem::path &) { testPanned(); }},
                                       {"no dominance", [](const std::filesystem::path &) { testNoDominance(); }},
                                       {"switch", [](const std::filesystem::path &) { testSwitch(); }},
                                       {"layout", [](const std::filesystem::path &) { testLayout(); }},
                                   });
