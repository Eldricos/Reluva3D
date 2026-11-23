// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "emulation/x86/win32.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace reluva::x86 {

/**
 * WINMM's joystick functions (joyGetNumDevs, joyGetDevCapsA, joyGetPos,
 * joyGetPosEx), written from Microsoft's documentation: the joysticks a
 * program of the 1990s reads, here up to two, whose state the engine sets
 * from the player's controllers. Any thread may set it.
 */
class Joysticks {
public:
    /** How many a program can have: as many as Windows 95's driver lists. */
    static constexpr int kSupported = 16;
    /** How many may be connected here. */
    static constexpr int kConnectable = 2;
    /** A centred axis's position: positions run from 0 to 65535. */
    static constexpr uint16_t kCentre = 32767;
    /** The hat's position when it isn't pushed (JOY_POVCENTERED). */
    static constexpr uint32_t kPovCentred = 0xffff;

    /** What a joystick has, as joyGetDevCapsA tells it. */
    struct Device {
        std::string name;
        /** Its axes, in WINMM's order X, Y, Z, R, U, V: 2 to 6. */
        int axes = 2;
        /** Its buttons, up to 32. */
        int buttons = 4;
        bool hat = false;
    };

    /** Where a joystick's axes, buttons and hat are. */
    struct State {
        /** X, Y, Z, R, U, V: 0 to 65535; X increasing to the right and Y down. */
        std::array<uint16_t, 6> axes{kCentre, kCentre, kCentre, kCentre, kCentre, kCentre};
        /** A bit for each button held, the first the lowest. */
        uint32_t buttons = 0;
        /** The hat's direction in hundredths of a degree clockwise from ahead, or kPovCentred. */
        uint32_t pov = kPovCentred;
    };

    /** Provides WINMM's joystick functions to `process`; it has to outlive their use. */
    explicit Joysticks(Win32Process &process);
    ~Joysticks();

    /** Joystick `id` (0 or 1) is plugged in, or out (nullptr). */
    void connect(int id, const Device *device);
    /** Joystick `id`'s state now. */
    void set(int id, const State &state);

    /**
     * A game controller as WINMM's joystick: X and Y the left stick, Z the
     * triggers (the right's less the left's), R and U the right stick; ten
     * buttons (A, B, X, Y, the shoulders, Back, Start, the sticks' clicks);
     * the D-pad as the hat (docs/common/x86/joystick.md).
     */
    ///@{
    static Device fromGamepad(const GamepadState &pad);
    static State stateOf(const GamepadState &pad);
    ///@}

    struct Impl;

private:
    std::unique_ptr<Impl> _i;
};

} // namespace reluva::x86
