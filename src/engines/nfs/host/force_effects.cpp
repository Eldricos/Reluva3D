// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/host/force_effects.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr double kMax = 10000; // DI_FFNOMINALMAX
constexpr double kPi = 3.14159265358979;

// DIEFFECT's parts (SetParameters' DIEP_ flags) and its fields' offsets.
constexpr uint32_t kDuration = 0x1, kGain = 0x4, kAxes = 0x20, kDirection = 0x40, kEnvelope = 0x80,
                   kTypeSpecific = 0x100, kStartDelay = 0x200;

} // namespace

bool DiEffect::kindOf(uint32_t guidData1, Kind& kind) {
    if (guidData1 < 0x13541c20 || guidData1 > 0x13541c2b)
        return false;
    kind = Kind(guidData1 - 0x13541c20);
    return true;
}

void DiEffect::set(const std::function<uint32_t(uint32_t)>& read32, uint32_t at, uint32_t which) {
    const uint32_t size = read32(at), flags = read32(at + 4);
    if (which & kDuration)
        durationUs = read32(at + 8);
    if (which & kGain)
        gain = std::min<uint32_t>(read32(at + 16), 10000);
    if (which & kAxes) {
        // rgdwAxes: by offset in the data format (DIEFF_OBJECTOFFSETS, 2),
        // X's 0; or by object ID (DIEFF_OBJECTIDS, 1), X's instance 0 of
        // an axis.
        axisCount = std::clamp<uint32_t>(read32(at + 28), 0, 3);
        xAxis = -1;
        const uint32_t axes = read32(at + 32);
        for (uint32_t i = 0; axes && i < axisCount && xAxis < 0; ++i) {
            const uint32_t a = read32(axes + 4 * i);
            if ((flags & 2) ? a == 0 : (a & 3) && (a >> 8 & 0xffff) == 0)
                xAxis = int(i);
        }
    }
    if (which & kDirection) {
        coordinates = flags & 0x70;
        const uint32_t values = read32(at + 36), n = coordinates == 0x10 ? axisCount : axisCount - 1;
        for (uint32_t i = 0; i < 3; ++i)
            direction[i] = values && i < n ? int32_t(read32(values + 4 * i)) : 0;
    }
    if (which & kEnvelope) {
        const uint32_t e = read32(at + 40);
        envelope = e != 0;
        if (e) {
            attackLevel = read32(e + 4);
            attackUs = read32(e + 8);
            fadeLevel = read32(e + 12);
            fadeUs = read32(e + 16);
        }
    }
    if (which & kStartDelay && size >= 56)
        startDelayUs = read32(at + 52);
    const uint32_t bytes = read32(at + 44), p = read32(at + 48);
    if (!(which & kTypeSpecific) || !p || !bytes)
        return;
    switch (kind) {
    case Constant: // DICONSTANTFORCE
        magnitude = int32_t(read32(p));
        break;
    case Ramp: // DIRAMPFORCE
        magnitude = int32_t(read32(p));
        rampEnd = int32_t(read32(p + 4));
        break;
    case Square: case Sine: case Triangle: case SawUp: case SawDown: // DIPERIODIC
        magnitude = int32_t(std::min<uint32_t>(read32(p), 10000));
        offset = int32_t(read32(p + 4));
        periodUs = read32(p + 12);
        break;
    case Spring: case Damper: case Inertia: case Friction: { // DICONDITION, one for each axis or one for all
        const uint32_t c = p + (bytes >= 24 * uint32_t(std::max(xAxis, 0) + 1) ? 24 * uint32_t(std::max(xAxis, 0)) : 0);
        offset = int32_t(read32(c));
        positiveCoefficient = int32_t(read32(c + 4));
        negativeCoefficient = int32_t(read32(c + 8));
        positiveSaturation = read32(c + 12);
        negativeSaturation = read32(c + 16);
        break;
    }
    case Custom:
        break;
    }
}

float DiEffect::alongX() const {
    if (xAxis < 0)
        return 0;
    if (axisCount <= 1)
        return direction[0] < 0 ? -1.0f : 1.0f;
    if (coordinates == 0x20) // polar: hundredths of a degree clockwise from north (-Y), east +X
        return float(std::sin(direction[0] / 18000.0 * kPi));
    if (coordinates == 0x40) // spherical: from +X towards +Y
        return float(std::cos(direction[0] / 18000.0 * kPi));
    double norm = 0;
    for (uint32_t i = 0; i < axisCount; ++i)
        norm += double(direction[i]) * direction[i];
    return norm > 0 ? float(direction[xAxis] / std::sqrt(norm)) : 0.0f;
}

bool DiEffect::active(uint64_t nowMs) const {
    const uint64_t from = startedMs + startDelayUs / 1000;
    if (!playing || nowMs < from)
        return false;
    if (durationUs == kInfinite || iterations == kInfinite)
        return true;
    return (nowMs - from) * 1000 < uint64_t(durationUs) * iterations;
}

GamepadForce sumForces(const std::vector<const DiEffect*>& effects, uint32_t deviceGain, uint64_t nowMs) {
    GamepadForce f;
    float strongestVibration = 0, strongestSpring = 0;
    for (const DiEffect* e : effects) {
        if (!e || !e->active(nowMs))
            continue;
        const double g = e->gain / kMax * std::min<uint32_t>(deviceGain, 10000) / kMax;
        // Where it is in its iteration, for ramps and envelopes.
        const uint64_t elapsedUs = (nowMs - e->startedMs) * 1000 - std::min<uint64_t>(e->startDelayUs, (nowMs - e->startedMs) * 1000);
        const bool finite = e->durationUs != DiEffect::kInfinite && e->durationUs > 0;
        const double t = finite ? double(elapsedUs % e->durationUs) : double(elapsedUs);
        // A level through its envelope: from the attack level to the
        // effect's, and on to the fade level at the end.
        auto shaped = [&](double level) {
            if (!e->envelope)
                return level;
            double size = std::fabs(level);
            if (e->attackUs && t < e->attackUs)
                size = e->attackLevel + (size - e->attackLevel) * t / e->attackUs;
            else if (finite && e->fadeUs && t > double(e->durationUs) - e->fadeUs)
                size += (e->fadeLevel - size) * (t - (double(e->durationUs) - e->fadeUs)) / e->fadeUs;
            return std::copysign(size, level);
        };
        const float x = e->alongX();
        switch (e->kind) {
        case DiEffect::Constant:
            f.constant += float(shaped(e->magnitude) / kMax * g * x);
            break;
        case DiEffect::Ramp: {
            const double level = finite ? e->magnitude + (e->rampEnd - e->magnitude) * t / e->durationUs : e->magnitude;
            f.constant += float(shaped(level) / kMax * g * x);
            break;
        }
        case DiEffect::Square: case DiEffect::Sine: case DiEffect::Triangle: case DiEffect::SawUp: case DiEffect::SawDown: {
            f.constant += float(e->offset / kMax * g * x);
            const float size = float(std::fabs(shaped(e->magnitude)) / kMax * g * std::fabs(x));
            f.vibration += size;
            if (size > strongestVibration) {
                strongestVibration = size;
                f.vibrationPeriodMs = std::max<uint32_t>(e->periodUs / 1000, 1);
                f.vibrationSquare = e->kind == DiEffect::Square;
            }
            break;
        }
        case DiEffect::Spring: {
            if (e->xAxis < 0)
                break;
            const float stiffness = float((std::abs(e->positiveCoefficient) + std::abs(e->negativeCoefficient)) / 2.0 / kMax * g);
            f.spring += stiffness;
            if (stiffness > strongestSpring) {
                strongestSpring = stiffness;
                f.springCentre = float(std::clamp(e->offset / kMax, -1.0, 1.0));
                f.springSaturation = float(std::min<uint32_t>(std::max(e->positiveSaturation, e->negativeSaturation), 10000) / kMax);
            }
            break;
        }
        case DiEffect::Damper: case DiEffect::Inertia: case DiEffect::Friction:
            // All taken as a damper: each resists the wheel's turning.
            if (e->xAxis >= 0)
                f.damper += float((std::abs(e->positiveCoefficient) + std::abs(e->negativeCoefficient)) / 2.0 / kMax * g);
            break;
        case DiEffect::Custom:
            break;
        }
    }
    f.constant = std::clamp(f.constant, -1.0f, 1.0f);
    f.spring = std::min(f.spring, 1.0f);
    f.damper = std::min(f.damper, 1.0f);
    f.vibration = std::min(f.vibration, 1.0f);
    if (f.vibration <= 0)
        f.vibrationPeriodMs = 0, f.vibrationSquare = false;
    if (f.spring <= 0)
        f.springCentre = 0, f.springSaturation = 1;
    return f;
}

} // namespace reluva::nfs
