// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/hrz.h"

#include <cctype>
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

} // namespace reluva::nfs
