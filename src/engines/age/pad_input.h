// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "engines/age/car.h"
#include "engines/age/options.h"

#include <cstdint>

namespace reluva::age {

// A game controller, which the original had too (its Controller choice
// and Force Feedback). The left stick steers, the right trigger is the
// throttle and the left the brakes, by how far they're pushed; the
// buttons press the controls' keys (padKey). Reluva's layout.
struct PadDrive {
    float steer = 0, throttle = 0, brake = 0;

    // A stick or trigger moved (Event::pad, Event::value), with a dead
    // zone round the middle.
    void axis(int axis, float value);
    // Added to the keys' controls: a stick pushed further than the keys
    // steer wins, the pedals the more pressed of the two.
    void apply(CarControls& c) const;
    bool idle() const { return steer == 0 && throttle == 0 && brake == 0; }
};

// The key a button stands for while driving (a control's first key, as
// the options bind it), or in the menus (the arrows, Return and Escape);
// 0 for none. Start pauses (Escape) while driving.
uint32_t padKey(int button, bool driving, const GameOptions& options);

// A pad's event as the key event it stands for; false if it's no button
// or stands for nothing.
bool padKeyEvent(const Event& ev, bool driving, const GameOptions& options, Event& out);

// How hard to shake the controller for a bump of `impact` (m/s of speed
// lost): 0 for nothing felt.
float padRumble(float impact);

} // namespace reluva::age
