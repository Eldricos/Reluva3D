// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's joystick API as mmsystem.h declares it: JOYCAPSA and JOYCAPSW,
// JOYINFO and JOYINFOEX, the MM_JOY messages and their error codes; the
// registry's joystick settings as regstr.h names them.

#include "emulation/x86/joystick.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace reluva::x86 {

namespace {

constexpr uint32_t kNoError = 0;
constexpr uint32_t kInvalidParameter = 11; // MMSYSERR_INVALPARAM
constexpr uint32_t kBadParameters = 165;   // JOYERR_PARMS
constexpr uint32_t kCantDo = 166;          // JOYERR_NOCANDO
constexpr uint32_t kUnplugged = 167;       // JOYERR_UNPLUGGED

constexpr uint32_t kCapsSize = 404;     // sizeof(JOYCAPSA)
constexpr uint32_t kWideCapsSize = 728; // sizeof(JOYCAPSW): its strings of 16-bit characters
constexpr uint32_t kInfoExSize = 52;

// JOYCAPS's wCaps: the axes past Y, and the hat, in degrees' hundredths.
constexpr uint32_t kHasZ = 0x1;
constexpr uint32_t kHasR = 0x2;
constexpr uint32_t kHasU = 0x4;
constexpr uint32_t kHasV = 0x8;
constexpr uint32_t kHasPov = 0x10;
constexpr uint32_t kPovContinuous = 0x40;

// wPeriodMin and wPeriodMax: how often a captured joystick may be polled, in ms.
constexpr uint32_t kPeriodMin = 10;
constexpr uint32_t kPeriodMax = 1000;

// The capture's messages, for joystick 1; joystick 2's are each one more.
constexpr uint32_t kMove = 0x3a0;       // MM_JOY1MOVE
constexpr uint32_t kZMove = 0x3a2;      // MM_JOY1ZMOVE
constexpr uint32_t kButtonDown = 0x3b5; // MM_JOY1BUTTONDOWN
constexpr uint32_t kButtonUp = 0x3b7;   // MM_JOY1BUTTONUP

// Where Windows 95 keeps the joysticks' settings (regstr.h's
// REGSTR_PATH_JOYCONFIG, REGSTR_KEY_JOYCURR, REGSTR_PATH_JOYOEM).
constexpr const char *kConfigPath =
    "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\";
constexpr const char *kOemPath =
    "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\";
constexpr uint32_t kRegBinary = 3;

// JOYREGHWSETTINGS's flags: the axes past Y, and the hat polled.
constexpr uint32_t kHwsHasZ = 0x1;
constexpr uint32_t kHwsHasPov = 0x2;
constexpr uint32_t kHwsPovIsPoll = 0x8;
constexpr uint32_t kHwsHasR = 0x80000;
constexpr uint32_t kHwsHasU = 0x800000;
constexpr uint32_t kHwsHasV = 0x1000000;

// A stick's -1 to 1 as a position.
uint16_t position(float v) {
    return uint16_t((std::clamp(v, -1.0f, 1.0f) + 1.0f) * 32767.5f); // centred 32767, as WINMM's
}

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t flagsOf(const Joysticks::Device &d) {
    const int axes = std::clamp(d.axes, 2, 6);
    uint32_t flags = d.hat ? kHwsHasPov | kHwsPovIsPoll : 0;
    for (int a = 2; a < axes; ++a)
        flags |= std::array<uint32_t, 4>{kHwsHasZ, kHwsHasR, kHwsHasU, kHwsHasV}[size_t(a - 2)];
    return flags;
}

} // namespace

struct Joysticks::Impl {
    Win32Process *process = nullptr;
    std::mutex lock;
    std::array<bool, kConnectable> connected{};
    std::array<Device, kConnectable> devices;
    std::array<State, kConnectable> states;
    // Settings the program changes: on its thread only.
    std::array<uint32_t, kConnectable> thresholds{};
    struct Capture {
        uint32_t window = 0;
        uint32_t period = 0;
        bool changedOnly = false;
        uint32_t nextMs = 0;
        bool started = false;
        State last;
    };
    std::array<Capture, kConnectable> captures;
    // Connected since update() last wrote the registry.
    std::array<bool, kConnectable> unwritten{};

    void writeRegistry(int id);
};

Joysticks::~Joysticks() = default;

Joysticks::Joysticks(Win32Process &process) : _i(std::make_unique<Impl>()) {
    Impl *i = _i.get();
    i->process = &process;
    const std::string dll = "WINMM.dll";
    // Whether `id` is plugged in; the reason it can't be read if not.
    auto check = [i](uint32_t id) {
        if (id >= uint32_t(kSupported))
            return kBadParameters;
        return id < uint32_t(kConnectable) && i->connected[id] ? kNoError : kUnplugged;
    };
    process.provideStdcall(dll, "joyGetNumDevs", 0, [](Cpu &) { return uint32_t(kSupported); });
    // joyGetDevCapsA and W (id, JOYCAPS *, size): the same, the W form's
    // strings of 16-bit characters, so its numbers 32 bytes further on.
    auto caps = [i, check](Cpu &c, bool wide) {
        const uint32_t id = c.arg(0);
        const uint32_t out = c.arg(1);
        const uint32_t size = wide ? kWideCapsSize : kCapsSize;
        if (c.arg(2) < size || !c.valid(out, size))
            return kBadParameters;
        std::lock_guard<std::mutex> guard(i->lock);
        if (const uint32_t r = check(id))
            return r;
        const Device &d = i->devices[id];
        for (uint32_t k = 0; k < size; k += 4)
            c.write32(out + k, 0);
        auto text = [&](uint32_t at, const std::string &s, uint32_t chars) {
            const std::string cut = s.substr(0, chars - 1);
            if (!wide) {
                c.writeString(at, cut, chars);
                return;
            }
            for (size_t k = 0; k < cut.size(); ++k)
                c.write16(at + 2 * uint32_t(k), uint8_t(cut[k]));
        };
        // wMid and wPid stay 0: no maker's, no product's.
        text(out + 4, d.name.empty() ? "Game controller" : d.name, 32);
        const uint32_t n = wide ? 68 : 36; // where the numbers start
        const int axes = std::clamp(d.axes, 2, 6);
        for (int a = 0; a < 3; ++a) // wXmax, wYmax, wZmax: 0 to 65535
            c.write32(n + out + 4 + 8 * uint32_t(a), a < axes ? 65535 : 0);
        c.write32(out + n + 24, uint32_t(d.buttons)); // wNumButtons
        c.write32(out + n + 28, kPeriodMin);
        c.write32(out + n + 32, kPeriodMax);
        for (int a = 3; a < 6; ++a) // wRmax, wUmax, wVmax
            c.write32(out + n + 40 + 8 * uint32_t(a - 3), a < axes ? 65535 : 0);
        uint32_t flags = d.hat ? kHasPov | kPovContinuous : 0;
        for (int a = 2; a < axes; ++a)
            flags |= std::array<uint32_t, 4>{kHasZ, kHasR, kHasU, kHasV}[size_t(a - 2)];
        c.write32(out + n + 60, flags);
        c.write32(out + n + 64, 6);                   // wMaxAxes
        c.write32(out + n + 68, uint32_t(axes));      // wNumAxes
        c.write32(out + n + 72, uint32_t(d.buttons)); // wMaxButtons
        text(out + n + 76, kRegistryKey, 32);         // szRegKey; szOEMVxD stays empty
        return kNoError;
    };
    process.provideStdcall(dll, "joyGetDevCapsA", 3, [caps](Cpu &c) { return caps(c, false); });
    process.provideStdcall(dll, "joyGetDevCapsW", 3, [caps](Cpu &c) { return caps(c, true); });
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
    // joyGetThreshold(id, UINT *): how far it moves before a captured
    // joystick's MM_JOYMOVE, in positions.
    process.provideStdcall(dll, "joyGetThreshold", 2, [i](Cpu &c) {
        const uint32_t id = c.arg(0);
        if (id >= uint32_t(kSupported) || !c.valid(c.arg(1), 4))
            return kInvalidParameter;
        c.write32(c.arg(1), id < uint32_t(kConnectable) ? i->thresholds[id] : 0);
        return kNoError;
    });
    process.provideStdcall(dll, "joySetThreshold", 2, [i](Cpu &c) {
        const uint32_t id = c.arg(0);
        if (id >= uint32_t(kConnectable))
            return kBadParameters;
        i->thresholds[id] = c.arg(1);
        return kNoError;
    });
    // joySetCapture(window, id, period, changed): the joystick's messages
    // to the window, polled each period (ms); with `changed`, its moves
    // only when it moved past the threshold.
    process.provideStdcall(dll, "joySetCapture", 4, [i, check](Cpu &c) {
        const uint32_t window = c.arg(0);
        const uint32_t id = c.arg(1);
        if (id >= uint32_t(kSupported) || !i->process->window(window))
            return kBadParameters;
        std::lock_guard<std::mutex> guard(i->lock);
        if (const uint32_t r = check(id))
            return r;
        Impl::Capture &cap = i->captures[id];
        if (cap.window)
            return kCantDo; // captured already
        cap = Impl::Capture();
        cap.window = window;
        cap.period = std::clamp(c.arg(2), kPeriodMin, kPeriodMax);
        cap.changedOnly = c.arg(3) != 0;
        cap.last = i->states[id];
        return kNoError;
    });
    process.provideStdcall(dll, "joyReleaseCapture", 1, [i](Cpu &c) {
        const uint32_t id = c.arg(0);
        if (id >= uint32_t(kConnectable))
            return kBadParameters;
        i->captures[id] = Impl::Capture();
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
    _i->unwritten[size_t(id)] = device != nullptr;
}

void Joysticks::set(int id, const State &state) {
    if (id < 0 || id >= kConnectable)
        return;
    std::lock_guard<std::mutex> guard(_i->lock);
    _i->states[size_t(id)] = state;
}

// Windows 95's settings for a joystick: its name, under the OEM key it
// gives (Joystick<n>OEMName, OEMName), and its JOYREGHWCONFIG: ranges 0 to
// 65535 centred, which need no calibrating.
void Joysticks::Impl::writeRegistry(int id) {
    const Device &d = devices[size_t(id)];
    std::string oem = d.name.empty() ? "Game controller" : d.name;
    std::replace(oem.begin(), oem.end(), '\\', '/');
    const std::string current = std::string(kConfigPath) + kRegistryKey + "\\CurrentJoystickSettings";
    const std::string n = std::to_string(id + 1);
    process->setRegistryString(current, "Joystick" + n + "OEMName", oem);
    std::vector<uint8_t> config;
    put32(config, flagsOf(d)); // JOYREGHWSETTINGS: dwFlags, dwNumButtons
    put32(config, uint32_t(d.buttons));
    put32(config, 0x2 | 0x4);                          // dwUsageSettings: JOY_US_PRESENT, JOY_US_ISOEM
    for (uint32_t v : {0u, 65535u, uint32_t(kCentre)}) // jrvHardware: jpMin, jpMax, jpCenter
        for (int a = 0; a < 6; ++a)
            put32(config, v);
    for (uint32_t v : {0u, 9000u, 18000u, 27000u}) // dwPOVValues: ahead, right, back, left
        put32(config, v);
    put32(config, 0x3f); // dwCalFlags: JOY_ISCAL_XY, Z, R, U, V and POV
    put32(config, 1);    // dwType: JOY_HW_CUSTOM
    put32(config, 0);
    process->setRegistryValue(current, "Joystick" + n + "Configuration", kRegBinary, config);
    process->setRegistryString(kOemPath + oem, "OEMName", d.name.empty() ? oem : d.name);
    std::vector<uint8_t> data;
    put32(data, flagsOf(d));
    put32(data, uint32_t(d.buttons));
    process->setRegistryValue(kOemPath + oem, "OEMData", kRegBinary, data);
}

void Joysticks::update(uint32_t nowMs) {
    std::lock_guard<std::mutex> guard(_i->lock);
    for (int id = 0; id < kConnectable; ++id) {
        if (_i->unwritten[size_t(id)]) {
            _i->unwritten[size_t(id)] = false;
            _i->writeRegistry(id);
        }
        Impl::Capture &cap = _i->captures[size_t(id)];
        if (!cap.window)
            continue;
        if (!_i->connected[size_t(id)]) { // unplugged: the capture ends
            cap = Impl::Capture();
            continue;
        }
        if (cap.started && int32_t(nowMs - cap.nextMs) < 0)
            continue;
        cap.started = true;
        cap.nextMs = nowMs + cap.period;
        const State &s = _i->states[size_t(id)];
        const uint32_t held = s.buttons & 0xf; // the messages tell of JOY_BUTTON1 to 4
        const uint32_t was = cap.last.buttons & 0xf;
        const uint32_t xy = uint32_t(s.axes[0]) | uint32_t(s.axes[1]) << 16;
        const uint32_t step = _i->thresholds[size_t(id)];
        auto moved = [&](int a) {
            return uint32_t(std::abs(int(s.axes[size_t(a)]) - int(cap.last.axes[size_t(a)]))) > step;
        };
        // A button's change: JOY_BUTTON<n>CHG, its bit 8 places up.
        if (const uint32_t down = held & ~was)
            _i->process->postMessage(cap.window, kButtonDown + uint32_t(id), held | down << 8, xy);
        if (const uint32_t up = was & ~held)
            _i->process->postMessage(cap.window, kButtonUp + uint32_t(id), held | up << 8, xy);
        State last = cap.last;
        last.buttons = s.buttons;
        if (!cap.changedOnly || moved(0) || moved(1)) {
            _i->process->postMessage(cap.window, kMove + uint32_t(id), held, xy);
            last.axes[0] = s.axes[0];
            last.axes[1] = s.axes[1];
        }
        if (moved(2)) {
            _i->process->postMessage(cap.window, kZMove + uint32_t(id), held, s.axes[2]);
            last.axes[2] = s.axes[2];
        }
        cap.last = last;
    }
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
