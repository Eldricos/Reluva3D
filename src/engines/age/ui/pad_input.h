// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/system.h"
#include "engines/age/game/car.h"
#include "engines/age/game/options.h"

#include <cstdint>
#include <vector>

namespace reluva::age {

/**
 * A game controller (System::readGamepad), which the original had too: its
 * Controller choice and Force Feedback. The left stick (or a wheel) steers,
 * the right trigger is the throttle and the left the brakes, by how far
 * they're pushed; the buttons press the controls' keys (padKey). Reluva's
 * layout.
 */
struct PadDrive {
    float steer = 0;
    float throttle = 0;
    float brake = 0;
    uint32_t buttons = 0;   // GamepadButton bits held at the last poll
    float deadZone = 0.15f; // of the stick's travel, round the middle (Joystick Dead Zone)

    /** An axis (GamepadState::axes' order) moved, with a dead zone round the middle. */
    void axis(int axis, float value);
    /** Added to the keys' controls: a stick pushed further than the keys steer wins, the pedals the more pressed. */
    void apply(CarControls &c) const;
    /** Whether the stick and both triggers are at rest. */
    bool idle() const { return steer == 0 && throttle == 0 && brake == 0; }
};

/**
 * Reads the controller: its axes into `pad` (unless `axes` is false, as
 * while a replay plays back its own), and each button pressed or let go
 * since the last poll as the key event it stands for, into `keys`.
 */
void pollPad(System &sys, PadDrive &pad, bool driving, bool axes, const GameOptions &options, std::vector<Event> &keys);

/**
 * The key a button (GamepadButton) stands for while driving (a control's
 * first key, as the options bind it), or in the menus (the arrows, Return
 * and Escape); 0 for none. Start pauses (Escape) while driving.
 */
uint32_t padKey(int button, bool driving, const GameOptions &options);

/** How hard to shake the controller for a bump of `impact` (m/s of speed lost): 0 for nothing felt. */
float padRumble(float impact);

/** A knock's shake of the controller (System::setGamepadForce), stopped once it's had its time. */
class PadShaker {
public:
    /**
     * Starts a shake of 200 ms, in place of any under way.
     * @param strength 0 to 1 (clamped), as padRumble gives.
     */
    void knock(System &sys, float strength, uint64_t nowMs);
    /** Stops the shake once its time is up; called each frame. */
    void update(System &sys, uint64_t nowMs);

private:
    uint64_t _until = 0; // when the shake stops (0: none under way)
};

} // namespace reluva::age
