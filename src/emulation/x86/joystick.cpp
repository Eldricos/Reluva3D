// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's joystick API as mmsystem.h declares it: JOYCAPSA, JOYINFO and
// JOYINFOEX, and their error codes.

#include "emulation/x86/joystick.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace reluva::x86 {

namespace {

constexpr uint32_t kNoError = 0;
constexpr uint32_t kBadParameters = 165; // JOYERR_PARMS
constexpr uint32_t kUnplugged = 167;     // JOYERR_UNPLUGGED

constexpr uint32_t kCapsSize = 404; // sizeof(JOYCAPSA)
constexpr uint32_t kInfoExSize = 52;

// JOYCAPSA's wCaps: the axes past Y, and the hat, in degrees' hundredths.
constexpr uint32_t kHasZ = 0x1;
constexpr uint32_t kHasR = 0x2;
constexpr uint32_t kHasU = 0x4;
constexpr uint32_t kHasV = 0x8;
constexpr uint32_t kHasPov = 0x10;
constexpr uint32_t kPovContinuous = 0x40;

// A stick's -1 to 1 as a position.
uint16_t position(float v) {
    return uint16_t((std::clamp(v, -1.0f, 1.0f) + 1.0f) * 32767.5f); // centred 32767, as WINMM's
}

} // namespace

struct Joysticks::Impl {
    std::mutex lock;
    std::array<bool, kConnectable> connected{};
    std::array<Device, kConnectable> devices;
    std::array<State, kConnectable> states;
};

Joysticks::~Joysticks() = default;

Joysticks::Joysticks(Win32Process &process) : _i(std::make_unique<Impl>()) {
    Impl *i = _i.get();
    const std::string dll = "WINMM.dll";
    // Whether `id` is plugged in; the reason it can't be read if not.
    auto check = [i](uint32_t id) {
        if (id >= uint32_t(kSupported))
            return kBadParameters;
        return id < uint32_t(kConnectable) && i->connected[id] ? kNoError : kUnplugged;
    };
    process.provideStdcall(dll, "joyGetNumDevs", 0, [](Cpu &) { return uint32_t(kSupported); });
    // joyGetDevCapsA(id, JOYCAPSA *, size)
    process.provideStdcall(dll, "joyGetDevCapsA", 3, [i, check](Cpu &c) {
        const uint32_t id = c.arg(0);
        const uint32_t out = c.arg(1);
        if (c.arg(2) < kCapsSize || !c.valid(out, kCapsSize))
            return kBadParameters;
        std::lock_guard<std::mutex> guard(i->lock);
        if (const uint32_t r = check(id))
            return r;
        const Device &d = i->devices[id];
        for (uint32_t k = 0; k < kCapsSize; k += 4)
            c.write32(out + k, 0);
        // wMid and wPid stay 0: no maker's, no product's.
        c.writeString(out + 4, d.name.empty() ? "Game controller" : d.name, 32);
        const int axes = std::clamp(d.axes, 2, 6);
        for (int a = 0; a < 3; ++a) // wXmax, wYmax, wZmax: 0 to 65535
            c.write32(out + 40 + 8 * uint32_t(a), a < axes ? 65535 : 0);
        c.write32(out + 60, uint32_t(d.buttons)); // wNumButtons
        c.write32(out + 64, 10);                  // wPeriodMin, wPeriodMax: ms
        c.write32(out + 68, 1000);
        for (int a = 3; a < 6; ++a) // wRmax, wUmax, wVmax
            c.write32(out + 76 + 8 * uint32_t(a - 3), a < axes ? 65535 : 0);
        uint32_t caps = d.hat ? kHasPov | kPovContinuous : 0;
        for (int a = 2; a < axes; ++a)
            caps |= std::array<uint32_t, 4>{kHasZ, kHasR, kHasU, kHasV}[size_t(a - 2)];
        c.write32(out + 96, caps);
        c.write32(out + 100, 6);                   // wMaxAxes
        c.write32(out + 104, uint32_t(axes));      // wNumAxes
        c.write32(out + 108, uint32_t(d.buttons)); // wMaxButtons
        return kNoError;
    });
    // joyGetPos(id, JOYINFO *): X, Y, Z and the first four buttons.
    process.provideStdcall(dll, "joyGetPos", 2, [i, check](Cpu &c) {
        const uint32_t id = c.arg(0);
        const uint32_t out = c.arg(1);
        if (!c.valid(out, 16))
            return kBadParameters;
        std::lock_guard<std::mutex> guard(i->lock);
        if (const uint32_t r = check(id))
            return r;
        const State &s = i->states[id];
        for (uint32_t a = 0; a < 3; ++a)
            c.write32(out + 4 * a, s.axes[a]);
        c.write32(out + 12, s.buttons & 0xf);
        return kNoError;
    });
    // joyGetPosEx(id, JOYINFOEX *): every axis, button and the hat.
    process.provideStdcall(dll, "joyGetPosEx", 2, [i, check](Cpu &c) {
        const uint32_t id = c.arg(0);
        const uint32_t out = c.arg(1);
        if (!c.valid(out, kInfoExSize) || c.read32(out) < kInfoExSize)
            return kBadParameters;
        std::lock_guard<std::mutex> guard(i->lock);
        if (const uint32_t r = check(id))
            return r;
        const State &s = i->states[id];
        for (uint32_t a = 0; a < 6; ++a)
            c.write32(out + 8 + 4 * a, s.axes[a]);
        c.write32(out + 32, s.buttons);
        uint32_t held = 0; // dwButtonNumber: how many are held
        for (uint32_t b = s.buttons; b; b &= b - 1)
            ++held;
        c.write32(out + 36, held);
        c.write32(out + 40, i->devices[id].hat ? s.pov : kPovCentred);
        return kNoError;
    });
}

void Joysticks::connect(int id, const Device *device) {
    if (id < 0 || id >= kConnectable)
        return;
    std::lock_guard<std::mutex> guard(_i->lock);
    _i->connected[size_t(id)] = device != nullptr;
    if (device)
        _i->devices[size_t(id)] = *device;
    _i->states[size_t(id)] = State();
}

void Joysticks::set(int id, const State &state) {
    if (id < 0 || id >= kConnectable)
        return;
    std::lock_guard<std::mutex> guard(_i->lock);
    _i->states[size_t(id)] = state;
}

Joysticks::Device Joysticks::fromGamepad(const GamepadState &pad) {
    Device d;
    d.name = pad.name.substr(0, 31);
    d.axes = 5;
    d.buttons = 10;
    d.hat = true;
    return d;
}

Joysticks::State Joysticks::stateOf(const GamepadState &pad) {
    State s;
    s.axes[0] = position(pad.axes[0]);
    s.axes[1] = position(pad.axes[1]);
    s.axes[2] = position(pad.axes[5] - pad.axes[4]);
    s.axes[3] = position(pad.axes[2]);
    s.axes[4] = position(pad.axes[3]);
    static constexpr GamepadButton kOrder[] = {kPadSouth,         kPadEast, kPadWest,  kPadNorth,     kPadLeftShoulder,
                                               kPadRightShoulder, kPadBack, kPadStart, kPadLeftStick, kPadRightStick};
    for (size_t b = 0; b < std::size(kOrder); ++b)
        if (pad.buttons & (1u << kOrder[b]))
            s.buttons |= 1u << b;
    const bool up = pad.buttons & (1u << kPadUp);
    const bool down = pad.buttons & (1u << kPadDown);
    const bool left = pad.buttons & (1u << kPadLeft);
    const bool right = pad.buttons & (1u << kPadRight);
    const int dx = (right ? 1 : 0) - (left ? 1 : 0);
    const int dy = (down ? 1 : 0) - (up ? 1 : 0);
    if (dx || dy) {
        // Clockwise from ahead, in hundredths of a degree.
        const double degrees = std::atan2(double(dx), double(-dy)) * 180.0 / 3.14159265358979;
        s.pov = uint32_t(std::lround(std::fmod(degrees + 360.0, 360.0) * 100)) % 36000;
    }
    return s;
}

} // namespace reluva::x86
