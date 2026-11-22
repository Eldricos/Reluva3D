// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's joystick, as Microsoft documents it: joystick 0, its capabilities
// (JOYCAPSA) and its position (JOYINFOEX), from Win32Process::joystick.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstring>

namespace reluva::x86 {

namespace {

constexpr uint32_t kJoyNoError = 0;
constexpr uint32_t kJoyErrParms = 165;     // JOYERR_PARMS: no such joystick
constexpr uint32_t kJoyErrUnplugged = 167; // JOYERR_UNPLUGGED
// JOYCAPS's wCaps: a z, r, u and v axis, and a point of view hat that
// gives any angle, its diagonals too (JOYCAPS_HASZ | HASR | HASU | HASV |
// HASPOV | POVCTS).
constexpr uint32_t kCaps = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x40;
constexpr uint32_t kButtons = 32;

} // namespace

void Win32Process::joystickAxis(int axis, int value) {
    if (axis >= 0 && axis < 6)
        joystick.axes[axis] = uint32_t(std::clamp(value, -32768, 32767) + 32768);
}

void Win32Process::joystickButton(int button, bool down) {
    if (button < 0 || button >= int(kButtons))
        return;
    if (down)
        joystick.buttons |= 1u << button;
    else
        joystick.buttons &= ~(1u << button);
}

void Win32Process::joystickHat(uint8_t directions) {
    // A diagonal is between its two directions.
    const bool up = (directions & 1) != 0;
    const bool right = (directions & 2) != 0;
    const bool down = (directions & 4) != 0;
    const bool left = (directions & 8) != 0;
    if (up && right)
        joystick.pov = 4500;
    else if (right && down)
        joystick.pov = 13500;
    else if (down && left)
        joystick.pov = 22500;
    else if (left && up)
        joystick.pov = 31500;
    else
        joystick.pov = up ? 0 : right ? 9000 : down ? 18000 : left ? 27000 : 0xffff;
}

void Win32Process::addJoystickStandIns() {
    const char *w = "WINMM.dll";
    stdcall(w, "joyGetNumDevs", 0, [](Cpu &) { return 16u; });
    // joyGetDevCapsA(id, JOYCAPSA*, its size: 404).
    stdcall(w, "joyGetDevCapsA", 3, [this](Cpu &c) {
        if (c.arg(0) != 0)
            return kJoyErrParms;
        if (!joystick.present)
            return kJoyErrUnplugged;
        const uint32_t p = c.arg(1);
        if (c.arg(2) < 404 || !c.valid(p, 404))
            return kJoyErrParms;
        std::memset(c.memory().data() + p, 0, 404);
        c.write16(p, 1);     // wMid
        c.write16(p + 2, 1); // wPid
        c.writeString(p + 4, "Joystick", 32);
        for (uint32_t i = 0; i < 3; ++i) // x, y, z: min 0, max 65535
            c.write32(p + 40 + 8 * i, 65535);
        c.write32(p + 60, kButtons);     // wNumButtons
        c.write32(p + 64, 10);           // wPeriodMin, ms
        c.write32(p + 68, 1000);         // wPeriodMax
        for (uint32_t i = 0; i < 3; ++i) // r, u, v
            c.write32(p + 76 + 8 * i, 65535);
        c.write32(p + 96, kCaps);
        c.write32(p + 100, 6);        // wMaxAxes
        c.write32(p + 104, 6);        // wNumAxes
        c.write32(p + 108, kButtons); // wMaxButtons
        return kJoyNoError;
    });
    // joyGetPosEx(id, JOYINFOEX*): every part, whatever dwFlags asks for.
    stdcall(w, "joyGetPosEx", 2, [this](Cpu &c) {
        if (c.arg(0) != 0)
            return kJoyErrParms;
        if (!joystick.present)
            return kJoyErrUnplugged;
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 52) || c.read32(p) < 52)
            return kJoyErrParms;
        for (uint32_t i = 0; i < 6; ++i)
            c.write32(p + 8 + 4 * i, joystick.axes[i]);
        c.write32(p + 32, joystick.buttons);
        uint32_t held = 0;
        for (uint32_t b = joystick.buttons; b; b &= b - 1)
            ++held;
        c.write32(p + 36, held); // dwButtonNumber
        c.write32(p + 40, joystick.pov);
        return kJoyNoError;
    });
}

} // namespace reluva::x86
