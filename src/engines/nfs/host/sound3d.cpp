// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/host/sound3d.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr double kSpeedOfSound = 343.3; // metres a second, as DirectSound has it
constexpr double kPi = 3.14159265358979323846;

struct V {
    double x = 0, y = 0, z = 0;
};
V of(const float* f) { return {f[0], f[1], f[2]}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(V a) { return std::sqrt(dot(a, a)); }
V unit(V a, V otherwise) {
    const double l = length(a);
    return l > 1e-9 ? V{a.x / l, a.y / l, a.z / l} : otherwise;
}
double fromDecibels(double hundredths) { return std::pow(10.0, std::min(hundredths, 0.0) / 2000.0); }

} // namespace

Ds3dResult spatialize(const Ds3dListener& listener, const Ds3dSource& source) {
    Ds3dResult r;
    if (source.mode == 2) // DS3DMODE_DISABLE
        return r;
    // Where the source is from the listener, in the listener's own axes.
    V local;
    if (source.mode == 1) { // DS3DMODE_HEADRELATIVE
        local = of(source.position);
    } else {
        const V front = unit(of(listener.front), {0, 0, 1});
        const V right = unit(cross(of(listener.top), front), {1, 0, 0});
        const V up = cross(front, right);
        const V rel = of(source.position) - of(listener.position);
        local = {dot(rel, right), dot(rel, up), dot(rel, front)};
    }
    const double d = length(local);

    // Distance.
    double gain = 1;
    const double minD = std::max<double>(source.minDistance, 1e-6);
    const double maxD = std::max<double>(source.maxDistance, minD);
    const double at = std::min(d, maxD);
    if (at > minD)
        gain = minD / (minD + std::max<double>(listener.rolloffFactor, 0) * (at - minD));

    // The cone, by the angle between where it points and the listener.
    if (source.outsideCone < 360 && d > 1e-9) {
        const V toListener = source.mode == 1 ? V{-local.x, -local.y, -local.z}
                                              : of(listener.position) - of(source.position);
        const double c = dot(unit(of(source.coneOrientation), {0, 0, 1}), unit(toListener, {0, 0, 1}));
        const double angle = std::acos(std::clamp(c, -1.0, 1.0)) * 180 / kPi * 2; // the cone's full angle
        const double inside = std::min<double>(source.insideCone, source.outsideCone);
        const double outside = source.outsideCone;
        double hundredths = 0;
        if (angle >= outside)
            hundredths = source.coneOutsideVolume;
        else if (angle > inside)
            hundredths = source.coneOutsideVolume * (angle - inside) / (outside - inside);
        gain *= fromDecibels(hundredths);
    }

    // Pan: the farther speaker down by up to 20 dB.
    const double side = d > 1e-9 ? std::clamp(local.x / d, -1.0, 1.0) : 0.0;
    const double far = std::pow(10.0, -std::fabs(side));
    r.behind = d > 1e-9 ? std::clamp(-local.z / d, 0.0, 1.0) : 0.0;
    gain *= rearGain(r.behind);
    r.left = gain * (side > 0 ? far : 1.0);
    r.right = gain * (side < 0 ? far : 1.0);

    // Doppler.
    if (listener.dopplerFactor > 0 && d > 1e-9 && source.mode != 1) {
        const V u = unit(of(source.position) - of(listener.position), {0, 0, 1});
        const double scale = double(listener.dopplerFactor) * std::max<double>(listener.distanceFactor, 1e-6);
        const double vl = dot(of(listener.velocity), u) * scale, vs = dot(of(source.velocity), u) * scale;
        const double below = kSpeedOfSound + vs;
        if (below > 1e-6)
            r.doppler = std::clamp((kSpeedOfSound + vl) / below, 0.5, 2.0);
        else
            r.doppler = 2.0;
    }
    return r;
}

double rearGain(double behind) { return std::pow(10.0, -3.0 * std::clamp(behind, 0.0, 1.0) / 20); }

double rearCutoff(double behind) { return 20000 - 16000 * std::clamp(behind, 0.0, 1.0); }

double lowPassCoefficient(double cutoff, double rate) {
    return rate > 0 ? 1 - std::exp(-2 * kPi * cutoff / rate) : 1.0;
}

} // namespace reluva::nfs
