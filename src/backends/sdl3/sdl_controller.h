// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "backends/sdl3/sdl_system.h"

namespace reluva {

// SdlSystem's game controller (sdl_controller.cpp).
struct SdlSystem::Controller {
    SDL_Gamepad* gamepad = nullptr;
    SDL_Joystick* wheel = nullptr; // a wheel with no gamepad layout
    bool isWheel = false;
    // A wheel's pedals by axis (-1: found as they rest), and the end each
    // of its axes rests at: where it's first seen at one (0 until then).
    int accelerator = -1, brake = -1;
    int pedalAxis[2] = {-1, -1};
    float rest[16] = {};

    // Forces: the haptic device and an effect for each kind it can do (-1
    // none yet), or else the pad's rumble, sent again before it runs out.
    SDL_Haptic* haptic = nullptr;
    uint32_t features = 0;
    int constant = -1, spring = -1, damper = -1, periodic = -1;
    bool periodicSquare = false;
    bool autoCentre = true;
    bool rumble = false;
    uint16_t rumbleLow = 0, rumbleHigh = 0;
    uint64_t rumbleSentMs = 0;

    SDL_Joystick* joystick() const { return gamepad ? SDL_GetGamepadJoystick(gamepad) : wheel; }
    bool connected() const { return gamepad ? SDL_GamepadConnected(gamepad) : wheel && SDL_JoystickConnected(wheel); }
};

} // namespace reluva
