// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the SDL backend's game controllers (sdl_controller.cpp) against
// SDL's virtual joysticks: a wheel read by its axes, and a pad's rumble from
// the forces a game asks for. No window or real controller is needed.

#include "backends/sdl3/sdl_system.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

using namespace reluva;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

struct Rumble {
    int calls = 0;
    uint16_t low = 0, high = 0;
};

bool SDLCALL rumble(void* userdata, Uint16 low, Uint16 high) {
    auto* r = static_cast<Rumble*>(userdata);
    ++r->calls;
    r->low = low;
    r->high = high;
    return true;
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// A virtual wheel: the wheel, two pedals resting at either end and a
// clutch half way, buttons and a hat.
void testWheel() {
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    SdlSystem sys; // ends SDL when it goes
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_WHEEL;
    desc.naxes = 4;
    desc.nbuttons = 8;
    desc.nhats = 1;
    desc.name = "Test Wheel";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    CHECK(id != 0);
    SDL_Joystick* j = SDL_OpenJoystick(id);
    CHECK(j != nullptr);
    auto set = [&](int axis, int16_t v) { SDL_SetJoystickVirtualAxis(j, axis, v); };
    set(0, 16384);
    set(1, 32767);  // the accelerator, up
    set(2, -32768); // the brake, up
    set(3, 0);
    SDL_UpdateJoysticks();
    {
        GamepadState s;
        CHECK(sys.readGamepad(s));
        CHECK(s.wheel && s.name == "Test Wheel" && !s.forceFeedback);
        CHECK(near(s.axes[0], 0.5f) && s.axes[4] == 0 && s.axes[5] == 0);
        // Pressed: the accelerator all the way, the brake half.
        set(1, -32768);
        set(2, 0);
        SDL_SetJoystickVirtualButton(j, 2, true);
        SDL_SetJoystickVirtualHat(j, 0, SDL_HAT_RIGHT);
        SDL_UpdateJoysticks();
        CHECK(sys.readGamepad(s));
        CHECK(near(s.axes[5], 1.0f) && near(s.axes[4], 0.5f));
        CHECK(s.buttons == (1u << 2 | 1u << PadRight));
        // Pedals set by hand: the brake on 1, the accelerator on 2.
        sys.setWheelPedals(2, 1);
        CHECK(sys.readGamepad(s));
        CHECK(near(s.axes[4], 1.0f) && near(s.axes[5], 0.5f));
        // Forces with nothing to give them do nothing.
        GamepadForce f;
        f.constant = 0.5f;
        sys.setGamepadForce(f);
    }
    SDL_CloseJoystick(j);
    SDL_DetachVirtualJoystick(id);
}

// A virtual pad that can rumble: pushes on the heavy motor, quick
// vibrations on the light one.
void testPadRumble() {
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    SdlSystem sys;
    Rumble r;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Test Pad";
    desc.Rumble = rumble;
    desc.userdata = &r;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    CHECK(id != 0);
    {
        GamepadState s;
        CHECK(sys.readGamepad(s));
        CHECK(!s.wheel && s.forceFeedback);
        GamepadForce f;
        f.constant = -0.5f;
        sys.setGamepadForce(f);
        CHECK(r.calls == 1 && r.low > 32000 && r.low < 33000 && r.high == 0);
        f.constant = 0;
        f.vibration = 0.4f;
        f.vibrationPeriodMs = 40;
        sys.setGamepadForce(f);
        CHECK(r.low == 0 && r.high > 26000 && r.high < 26500);
        f.vibrationPeriodMs = 83; // slow, like an engine's: the heavy motor
        sys.setGamepadForce(f);
        CHECK(r.low > 26000 && r.high == 0);
        sys.setGamepadForce({});
        CHECK(r.low == 0 && r.high == 0);
    }
    SDL_DetachVirtualJoystick(id);
}

} // namespace

int main() {
    // Each test starts SDL; its SdlSystem ends it.
    testWheel();
    testPadRumble();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
