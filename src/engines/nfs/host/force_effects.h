// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace reluva::nfs {

// A DirectInput force feedback effect (IDirectInputEffect) as the game
// sets it: DIEFFECT and its type-specific parameters, and whether it's
// playing. Times are in microseconds, as DirectInput has them; levels in
// its units, 10000 the most (DI_FFNOMINALMAX).
struct DiEffect {
    // Its kind, from the first part of its GUID (GUID_ConstantForce...,
    // 13541C2x-8E33-11D0-9AD0-00A0C9A06E35).
    enum Kind { Constant, Ramp, Square, Sine, Triangle, SawUp, SawDown, Spring, Damper, Inertia, Friction, Custom };
    static bool kindOf(uint32_t guidData1, Kind& kind);
    Kind kind = Constant;

    static constexpr uint32_t kInfinite = 0xffffffffu;
    uint32_t durationUs = kInfinite, gain = 10000, startDelayUs = 0;
    // Its axes: how many, and which of them is X (-1: none); its direction
    // (DIEFF_CARTESIAN 0x10, POLAR 0x20 or SPHERICAL 0x40, and the values).
    uint32_t axisCount = 1;
    int xAxis = 0;
    uint32_t coordinates = 0x10;
    int32_t direction[3] = {1, 0, 0};
    // The part of the push along X (-1..1) from its axes and direction,
    // the sign the side it comes from (+ from +X).
    float alongX() const;
    bool envelope = false;
    uint32_t attackLevel = 0, attackUs = 0, fadeLevel = 0, fadeUs = 0;
    // Constant: magnitude; ramp: start and end.
    int32_t magnitude = 0, rampEnd = 0;
    // Periodic: magnitude (in `magnitude`), offset and period.
    int32_t offset = 0;
    uint32_t periodUs = 0;
    // Condition, X's: offset (in `offset`), coefficients, saturation.
    int32_t positiveCoefficient = 0, negativeCoefficient = 0;
    uint32_t positiveSaturation = 10000, negativeSaturation = 10000;

    // Playing since startedMs, for `iterations` times its duration.
    bool playing = false;
    uint64_t startedMs = 0;
    uint32_t iterations = 1;

    // Reads what DIEFFECT at `at` gives, for the DIEP_ parts in `which`
    // (SetParameters' flags; CreateEffect sets all). `read32` reads the
    // game's memory.
    void set(const std::function<uint32_t(uint32_t)>& read32, uint32_t at, uint32_t which);
    // Whether it plays at `nowMs`: started, past its delay and not past
    // its iterations.
    bool active(uint64_t nowMs) const;
};

// What the effects playing add up to at `nowMs`, with the device's gain
// (DIPROP_FFGAIN, 0..10000).
GamepadForce sumForces(const std::vector<const DiEffect*>& effects, uint32_t deviceGain, uint64_t nowMs);

} // namespace reluva::nfs
