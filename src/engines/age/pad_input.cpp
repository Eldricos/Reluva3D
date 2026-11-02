// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/pad_input.h"

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
    case pad::LeftX:
        steer = outside(value, kDeadZone);
        break;
    case pad::RightTrigger:
        throttle = outside(value, kTriggerZone);
        break;
    case pad::LeftTrigger:
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
        case pad::DpadUp:
            return key::Up;
        case pad::DpadDown:
            return key::Down;
        case pad::DpadLeft:
            return key::Left;
        case pad::DpadRight:
            return key::Right;
        case pad::South:
        case pad::Start:
            return key::Return;
        case pad::East:
        case pad::Back:
            return key::Escape;
        default:
            return 0;
        }
    }
    switch (button) {
    case pad::South:
        return keyOf(Control::Handbrake, options);
    case pad::East:
        return keyOf(Control::Horn, options);
    case pad::West:
        return keyOf(Control::Reverse, options);
    case pad::North:
        return keyOf(Control::ChangeCamera, options);
    case pad::LeftShoulder:
        return keyOf(Control::ShiftDown, options);
    case pad::RightShoulder:
        return keyOf(Control::ShiftUp, options);
    case pad::Back:
        return keyOf(Control::MapToggle, options);
    case pad::Start:
        return key::Escape;
    case pad::LeftStick:
        return keyOf(Control::ThrillCam, options);
    case pad::RightStick:
        return keyOf(Control::LookBack, options);
    case pad::DpadUp:
        return keyOf(Control::NextCheckpoint, options);
    case pad::DpadDown:
        return keyOf(Control::RearViewMirror, options);
    case pad::DpadLeft:
        return keyOf(Control::LookLeft, options);
    case pad::DpadRight:
        return keyOf(Control::LookRight, options);
    default:
        return 0;
    }
}

bool padKeyEvent(const Event& ev, bool driving, const GameOptions& options, Event& out) {
    if (ev.type != EventType::PadDown && ev.type != EventType::PadUp)
        return false;
    const uint32_t k = padKey(ev.pad, driving, options);
    if (!k)
        return false;
    out = Event{};
    out.type = ev.type == EventType::PadDown ? EventType::KeyDown : EventType::KeyUp;
    out.key = k;
    return true;
}

float padRumble(float impact) {
    // A knock from 2 m/s up, full by 15.
    return impact < 2 ? 0 : std::min(1.0f, (impact - 2) / 13);
}

} // namespace reluva::age
