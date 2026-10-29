// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/hrz.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace reluva::nfs {

namespace {

// The file's numbers in order, its /* comments */ left out.
std::vector<int> numbers(std::string_view text) {
    std::vector<int> out;
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "/*") == 0) {
            const size_t end = text.find("*/", i + 2);
            i = end == std::string_view::npos ? text.size() : end + 2;
            continue;
        }
        const bool minus = text[i] == '-' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
        if (minus || std::isdigit(static_cast<unsigned char>(text[i]))) {
            size_t j = i + (minus ? 1 : 0);
            long v = 0;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])) && v < 1000000000)
                v = v * 10 + (text[j++] - '0');
            out.push_back(static_cast<int>(minus ? -v : v));
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

constexpr size_t kAfterRegions = 48;
constexpr int kMaxRegions = 64;

} // namespace

Error parseHorizon(std::string_view text, Horizon& out) {
    out = {};
    const std::vector<int> n = numbers(text);
    if (n.size() < 6 || n[5] < 0 || n[5] > kMaxRegions || n.size() != 6 + 15 * size_t(n[5]) + kAfterRegions)
        return Error::fail("Not a valid .hrz file: " + std::to_string(n.size()) + " numbers");
    size_t i = 0;
    auto next = [&] { return n[i++]; };
    auto rgb = [&] {
        Horizon::Rgb c;
        for (int& v : c)
            v = next();
        return c;
    };
    auto point = [&] {
        Horizon::FogPoint p;
        p.colour = rgb();
        p.density = next();
        return p;
    };
    out.fogColour = rgb();
    out.fogDensity = next();
    out.fogOnPixmap = next();
    out.fogRegions.resize(next());
    for (Horizon::FogRegion& r : out.fogRegions) {
        r.start = point();
        r.apex = point();
        r.end = point();
        r.beforeSlice = next();
        r.centreSlice = next();
        r.afterSlice = next();
    }
    out.cloudType = next();
    out.fogClouds = next();
    out.lightning = next();
    out.lightningChance = next();
    out.visualHeight = next();
    out.cloudDomeOffset = {next(), next()};
    out.cloudBright = next();
    out.cloudVariance = next();
    out.nimbus = next();
    out.windSpeed = next();
    out.windDirection = next();
    out.blackHorizon = next() != 0;
    out.mirror = next() != 0;
    out.radius = next();
    out.rotation = next();
    out.hasPixmap = next() != 0;
    out.shadedBase = next();
    out.shadedHeight = next();
    out.shadedMidpoint = next();
    out.pixmapTop = next();
    out.pixmapBottom = next();
    out.earthTop = rgb();
    out.earthBase = rgb();
    out.skyTop = rgb();
    out.skyBaseSun = rgb();
    out.skyBaseOpposite = rgb();
    Horizon::Weather& w = out.weather;
    w.type = next();
    w.startSlice = next();
    w.endSlice = next();
    w.fade = next();
    w.stayOn = next();
    w.fadeOff = next();
    w.stayOff = next();
    w.fadeOn = next();
    out.ambient = rgb();
    return Error::success();
}

// From nfs3.exe (0x470ad0 and 0x470b20, with the multiplier made as the
// .hrz is read, 0x47a88e): 5 x (100 - density), clamped to 5..500.
int sceneryFogValue(int density) {
    return density == 0 ? 0 : 5 * std::clamp(100 - density, 1, 100);
}

// The sky's: times (1 - pixmap effect) x radius / 100, rounded as the x87
// rounds (to the nearest, halves to even), from the float the game keeps.
int skyFogValue(const Horizon& h, int density) {
    const float effect = static_cast<float>(std::clamp(h.fogOnPixmap, 1, 100)) * 0.01f;
    const double times = std::nearbyint((1.0 - double(effect)) * double(h.radius) * 0.01);
    return 5 * std::clamp(100 - density, 1, 100) * static_cast<int>(times);
}

// The rotation as the game keeps it (0x47877e): degrees x 65536 / 360,
// in 16.16 turns, wrapped to 0..1.
float horizonTurn(const Horizon& h) {
    float turn = static_cast<float>(h.rotation * 65536 / 360) / 65536.0f;
    while (turn < 0)
        turn += 1;
    while (turn > 1)
        turn -= 1;
    return turn;
}

// 0x478a34: the steps in 16.16, truncated.
std::array<Horizon::Rgb, 16> skyBaseColours(const Horizon& h) {
    std::array<Horizon::Rgb, 16> out{};
    for (int k = 0; k < 8; ++k)
        for (size_t c = 0; c < 3; ++c) {
            const int value = (h.skyBaseSun[c] * 65536 + k * (h.skyBaseOpposite[c] - h.skyBaseSun[c]) * 8192) / 65536;
            out[size_t(k)][c] = value;
            out[size_t(15 - k)][c] = value;
        }
    return out;
}

// 0x47adc0: off1 + 3 off2 cos(r / 3000 quarter turns); less, for the
// nimbus (0x47b2b6), brightness / 255 x 3 off2 / 2 x (nimbus / 100,
// rounded down: only 100 counts).
float cloudHeight(const Horizon& h, float r, int brightness) {
    constexpr float kTau = 6.28318530718f;
    const float arch = 3.0f * static_cast<float>(h.cloudDomeOffset[1]);
    const float nimbus = static_cast<float>(std::max(h.nimbus, 0) / 100);
    return static_cast<float>(h.cloudDomeOffset[0]) + arch * std::cos(r * 0.25f / 3000.0f * kTau) -
           static_cast<float>(brightness) / 255.0f * arch * 0.5f * nimbus;
}

// 0x47ae10: bright less a random amount below the variance; then pushed
// from the middle (a signed square root of (b - 128) / 128); then 20
// passes that move each towards its neighbours within two points, by half
// the mean of (neighbour - it) x |neighbour - 128| / 128, kept within
// 0..bright.
std::array<uint8_t, kCloudPoints * kCloudPoints> cloudBrightness(const Horizon& h, uint32_t seed) {
    constexpr int n = kCloudPoints;
    std::array<float, n * n> b{};
    for (float& v : b) {
        int value = h.cloudBright;
        if (h.cloudVariance > 0) {
            seed = seed * 1103515245u + 12345u;
            value -= static_cast<int>((seed >> 16) & 0x7fff) % h.cloudVariance;
        }
        v = static_cast<float>(value);
    }
    for (float& v : b) {
        const float x = (v - 128.0f) / 128.0f;
        const float signedRoot = x < 0 ? -std::sqrt(-x) : std::sqrt(x);
        v = (1 + signedRoot) * 0.5f * 255.0f;
    }
    const float bright = static_cast<float>(h.cloudBright);
    for (int pass = 0; pass < 20; ++pass)
        for (int row = 0; row < n; ++row)
            for (int col = 0; col < n; ++col) {
                const float here = b[size_t(row * n + col)];
                float sum = 0, count = 0;
                for (int dr = -2; dr <= 2; ++dr)
                    for (int dc = -2; dc <= 2; ++dc) {
                        const int r = row + dr, c = col + dc;
                        if ((dr == 0 && dc == 0) || r < 0 || r >= n || c < 0 || c >= n)
                            continue;
                        const float other = b[size_t(r * n + c)];
                        sum += (other - here) * std::fabs(other - 128.0f) / 128.0f;
                        count += 1;
                    }
                const float mean = count > 0 ? sum / count : 0;
                if (mean != 0)
                    b[size_t(row * n + col)] = std::clamp(here + mean * 0.5f, 0.0f, bright);
            }
    std::array<uint8_t, n * n> out{};
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>(std::clamp(b[i], 0.0f, 255.0f));
    return out;
}

// 0x478701: speed x 0.00003 tiles a tick, towards the direction (degrees,
// from x towards z).
std::array<float, 2> cloudDrift(const Horizon& h) {
    constexpr float kTau = 6.28318530718f;
    const float turn = static_cast<float>(h.windDirection) / 360.0f * kTau;
    const float speed = static_cast<float>(h.windSpeed) * 3e-5f;
    return {std::cos(turn) * speed, std::sin(turn) * speed};
}

} // namespace reluva::nfs
