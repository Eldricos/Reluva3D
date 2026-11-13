// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/joystick_input.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kJoy1 = 185; // JOY1; JOY32 is 216
// The key for each of the original's direction bits: an axis's way below
// and above the threshold (x, y, the third and fourth axes; the fifth and
// sixth never read), then the hat up, down, right, left.
constexpr std::array<int32_t, 16> kWayKeys = {134, 135, 132, 133, 200, 201, 202, 203,
                                              204, 205, 206, 207, 208, 209, 210, 211};

} // namespace

void JoystickInput::registerCvars(CvarStore &cvars) {
    // As the original registers them (in_joystick is also latched there,
    // taking effect on in_restart; here at once).
    cvars.create("in_joystick", "0", CvarStore::kArchive);
    cvars.create("joy_threshold", "0.15", CvarStore::kArchive);
    cvars.create("joy_xbutton", "1", CvarStore::kArchive);
    cvars.create("joy_ybutton", "0", CvarStore::kArchive);
}

void JoystickInput::event(const Event &ev) {
    switch (ev.type) {
    case EventType::kJoyButtonDown:
    case EventType::kJoyButtonUp:
        if (ev.joyButton >= 0 && ev.joyButton < 32) {
            const uint32_t bit = 1u << ev.joyButton;
            _buttons = ev.type == EventType::kJoyButtonDown ? _buttons | bit : _buttons & ~bit;
        }
        break;
    case EventType::kJoyAxis:
        if (ev.joyAxis >= 0 && ev.joyAxis < int(_axes.size()))
            _axes[size_t(ev.joyAxis)] = ev.joyValue;
        break;
    case EventType::kJoyHat:
        _hat = ev.joyHat;
        break;
    case EventType::kJoyRemoved:
        _buttons = 0;
        _axes = {};
        _hat = 0;
        break;
    default:
        break;
    }
}

JoystickInput::Frame JoystickInput::frame(const CvarStore &cvars) {
    Frame out;
    const bool on = cvars.find("in_joystick") && cvars.integer("in_joystick") != 0;
    // The buttons that changed.
    const uint32_t buttons = on ? _buttons : 0;
    for (int i = 0; i < 32; ++i)
        if ((buttons ^ _lastButtons) & (1u << i))
            out.keys.emplace_back(kJoy1 + i, (buttons >> i) & 1);
    _lastButtons = buttons;
    // The axes, each -1 to 1: x and y analog unless joy_xbutton or
    // joy_ybutton says to take them as keys, which the third and fourth
    // always are; inside the threshold, nothing.
    uint32_t ways = 0;
    if (on) {
        const float threshold = cvars.value("joy_threshold");
        for (size_t i = 0; i < _axes.size(); ++i) {
            const float v = std::clamp(float(_axes[i]) / 32768.0f, -1.0f, 1.0f);
            const bool analog =
                (i == 0 && cvars.integer("joy_xbutton") == 0) || (i == 1 && cvars.integer("joy_ybutton") == 0);
            if (analog) {
                const int32_t value = v < -threshold || v > threshold ? int32_t(-(v * 127)) : 0;
                (i == 0 ? out.x : out.y) = value;
            } else if (v < -threshold) {
                ways |= 1u << (2 * i);
            } else if (v > threshold) {
                ways |= 1u << (2 * i + 1);
            }
        }
        // The hat: one way at a time (the original's reading has no
        // diagonals).
        switch (_hat) {
        case joyhat::kUp:
            ways |= 0x1000;
            break;
        case joyhat::kDown:
            ways |= 0x2000;
            break;
        case joyhat::kRight:
            ways |= 0x4000;
            break;
        case joyhat::kLeft:
            ways |= 0x8000;
            break;
        default:
            break;
        }
    }
    for (size_t i = 0; i < kWayKeys.size(); ++i)
        if ((ways ^ _lastWays) & (1u << i))
            out.keys.emplace_back(kWayKeys[i], (ways >> i) & 1);
    _lastWays = ways;
    return out;
}

} // namespace reluva::idtech3
