// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/ui/pad_input.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kDeadZone = 0.15f;    // of a stick's travel, round the middle
constexpr float kTriggerZone = 0.05f; // of a trigger's
constexpr int kAxisLeftX = 0;         // GamepadState::axes' order
constexpr int kAxisLeftTrigger = 4;
constexpr int kAxisRightTrigger = 5;
constexpr uint32_t kShakeMs = 200;
constexpr uint32_t kShakePeriodMs = 40;

// Past the dead zone, rescaled to the rest of the travel.
float outside(float v, float zone) {
    const float a = std::fabs(v);
    if (a <= zone)
        return 0;
    return std::copysign(std::min(1.0f, (a - zone) / (1 - zone)), v);
}

uint32_t keyOf(Control c, const GameOptions &o) {
    const auto &keys = o.keys[size_t(c)];
    return keys[0] ? keys[0] : keys[1];
}

} // namespace

void PadDrive::axis(int axis, float value) {
    switch (axis) {
    case kAxisLeftX:
        steer = outside(value, kDeadZone);
        break;
    case kAxisRightTrigger:
        throttle = outside(value, kTriggerZone);
        break;
    case kAxisLeftTrigger:
        brake = outside(value, kTriggerZone);
        break;
    default:
        break;
    }
}

void PadDrive::apply(CarControls &c) const {
    if (std::fabs(steer) > std::fabs(c.steer))
        c.steer = steer;
    c.throttle = std::max(c.throttle, throttle);
    c.brake = std::max(c.brake, brake);
}

void pollPad(System &sys, PadDrive &pad, bool driving, bool axes, const GameOptions &options,
             std::vector<Event> &keys) {
    GamepadState state;
    if (!sys.readGamepad(state)) {
        state = {}; // unplugged: everything let go
    }
    if (axes)
        for (int a : {kAxisLeftX, kAxisLeftTrigger, kAxisRightTrigger})
            pad.axis(a, state.axes[a]);
    const uint32_t changed = state.buttons ^ pad.buttons;
    for (int b = 0; b < kPadButtons; ++b) {
        if (!(changed >> b & 1))
            continue;
        if (const uint32_t k = padKey(b, driving, options)) {
            Event ev;
            ev.type = state.buttons >> b & 1 ? EventType::kKeyDown : EventType::kKeyUp;
            ev.key = k;
            keys.push_back(ev);
        }
    }
    pad.buttons = state.buttons;
}

uint32_t padKey(int button, bool driving, const GameOptions &options) {
    if (!driving) {
        switch (button) {
        case kPadUp:
            return key::kUp;
        case kPadDown:
            return key::kDown;
        case kPadLeft:
            return key::kLeft;
        case kPadRight:
            return key::kRight;
        case kPadSouth:
        case kPadStart:
            return key::kReturn;
        case kPadEast:
        case kPadBack:
            return key::kEscape;
        default:
            return 0;
        }
    }
    switch (button) {
    case kPadSouth:
        return keyOf(Control::kHandbrake, options);
    case kPadEast:
        return keyOf(Control::kHorn, options);
    case kPadWest:
        return keyOf(Control::kReverse, options);
    case kPadNorth:
        return keyOf(Control::kChangeCamera, options);
    case kPadLeftShoulder:
        return keyOf(Control::kShiftDown, options);
    case kPadRightShoulder:
        return keyOf(Control::kShiftUp, options);
    case kPadBack:
        return keyOf(Control::kMapToggle, options);
    case kPadStart:
        return key::kEscape;
    case kPadLeftStick:
        return keyOf(Control::kThrillCam, options);
    case kPadRightStick:
        return keyOf(Control::kLookBack, options);
    case kPadUp:
        return keyOf(Control::kNextCheckpoint, options);
    case kPadDown:
        return keyOf(Control::kRearViewMirror, options);
    case kPadLeft:
        return keyOf(Control::kLookLeft, options);
    case kPadRight:
        return keyOf(Control::kLookRight, options);
    default:
        return 0;
    }
}

float padRumble(float impact) {
    // A knock from 2 m/s up, full by 15.
    return impact < 2 ? 0 : std::min(1.0f, (impact - 2) / 13);
}

void PadShaker::knock(System &sys, float strength, uint64_t nowMs) {
    GamepadForce force;
    force.vibration = std::clamp(strength, 0.0f, 1.0f);
    force.vibrationPeriodMs = kShakePeriodMs;
    sys.setGamepadForce(force);
    _until = nowMs + kShakeMs;
}

void PadShaker::update(System &sys, uint64_t nowMs) {
    if (_until && nowMs >= _until) {
        sys.setGamepadForce({});
        _until = 0;
    }
}

} // namespace reluva::age
