// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/ui/pad_input.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kDeadZone = 0.15f;    // of a stick's travel, round the middle
constexpr float kTriggerZone = 0.05f; // of a trigger's

// Past the dead zone, rescaled to the rest of the travel.
float outside(float v, float zone) {
    const float a = std::fabs(v);
    if (a <= zone)
        return 0;
    return std::copysign(std::min(1.0f, (a - zone) / (1 - zone)), v);
}

uint32_t keyOf(Control c, const GameOptions& o) {
    const auto& keys = o.keys[size_t(c)];
    return keys[0] ? keys[0] : keys[1];
}

} // namespace

void PadDrive::axis(int axis, float value) {
    switch (axis) {
    case pad::kLeftX:
        steer = outside(value, kDeadZone);
        break;
    case pad::kRightTrigger:
        throttle = outside(value, kTriggerZone);
        break;
    case pad::kLeftTrigger:
        brake = outside(value, kTriggerZone);
        break;
    default:
        break;
    }
}

void PadDrive::apply(CarControls& c) const {
    if (std::fabs(steer) > std::fabs(c.steer))
        c.steer = steer;
    c.throttle = std::max(c.throttle, throttle);
    c.brake = std::max(c.brake, brake);
}

uint32_t padKey(int button, bool driving, const GameOptions& options) {
    if (!driving) {
        switch (button) {
        case pad::kDpadUp:
            return key::kUp;
        case pad::kDpadDown:
            return key::kDown;
        case pad::kDpadLeft:
            return key::kLeft;
        case pad::kDpadRight:
            return key::kRight;
        case pad::kSouth:
        case pad::kStart:
            return key::kReturn;
        case pad::kEast:
        case pad::kBack:
            return key::kEscape;
        default:
            return 0;
        }
    }
    switch (button) {
    case pad::kSouth:
        return keyOf(Control::Handbrake, options);
    case pad::kEast:
        return keyOf(Control::Horn, options);
    case pad::kWest:
        return keyOf(Control::Reverse, options);
    case pad::kNorth:
        return keyOf(Control::ChangeCamera, options);
    case pad::kLeftShoulder:
        return keyOf(Control::ShiftDown, options);
    case pad::kRightShoulder:
        return keyOf(Control::ShiftUp, options);
    case pad::kBack:
        return keyOf(Control::MapToggle, options);
    case pad::kStart:
        return key::kEscape;
    case pad::kLeftStick:
        return keyOf(Control::ThrillCam, options);
    case pad::kRightStick:
        return keyOf(Control::LookBack, options);
    case pad::kDpadUp:
        return keyOf(Control::NextCheckpoint, options);
    case pad::kDpadDown:
        return keyOf(Control::RearViewMirror, options);
    case pad::kDpadLeft:
        return keyOf(Control::LookLeft, options);
    case pad::kDpadRight:
        return keyOf(Control::LookRight, options);
    default:
        return 0;
    }
}

bool padKeyEvent(const Event& ev, bool driving, const GameOptions& options, Event& out) {
    if (ev.type != EventType::kPadDown && ev.type != EventType::kPadUp)
        return false;
    const uint32_t k = padKey(ev.pad, driving, options);
    if (!k)
        return false;
    out = Event{};
    out.type = ev.type == EventType::kPadDown ? EventType::kKeyDown : EventType::kKeyUp;
    out.key = k;
    return true;
}

float padRumble(float impact) {
    // A knock from 2 m/s up, full by 15.
    return impact < 2 ? 0 : std::min(1.0f, (impact - 2) / 13);
}

} // namespace reluva::age
