// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Game controllers for SdlSystem: the first pad connected (SDL's gamepads),
// or else the first racing wheel (a joystick SDL knows as one), and the
// forces they're to give: a wheel's motor through SDL's haptics, a pad's
// rumble otherwise.

#include "backends/sdl3/sdl_controller.h"

#include <algorithm>
#include <cmath>

namespace reluva {

namespace {

// An effect of `type` lasting until changed, on X (the wheel), from +X as
// a positive level: DirectInput's way (GamepadForce), which SDL shares.
SDL_HapticEffect effectOf(uint16_t type) {
    SDL_HapticEffect e{};
    e.type = type;
    if (type == SDL_HAPTIC_CONSTANT) {
        e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        e.constant.direction.dir[0] = 1;
        e.constant.length = SDL_HAPTIC_INFINITY;
    } else if (type == SDL_HAPTIC_SINE || type == SDL_HAPTIC_SQUARE) {
        e.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        e.periodic.direction.dir[0] = 1;
        e.periodic.length = SDL_HAPTIC_INFINITY;
    } else {
        e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        e.condition.direction.dir[0] = 1;
        e.condition.length = SDL_HAPTIC_INFINITY;
    }
    return e;
}

int16_t level(float v) { return int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767)); }

// Makes the effect if it isn't yet (and runs it, for ever), else changes it.
void apply(SDL_Haptic* haptic, int& id, const SDL_HapticEffect& e) {
    if (id < 0) {
        id = SDL_CreateHapticEffect(haptic, &e);
        if (id >= 0)
            SDL_RunHapticEffect(haptic, id, 1);
        return;
    }
    SDL_UpdateHapticEffect(haptic, id, &e);
}

} // namespace

void SdlSystem::closeController() {
    if (!controller_)
        return;
    Controller& c = *controller_;
    if (c.haptic)
        SDL_CloseHaptic(c.haptic); // its effects with it
    if (c.gamepad) {
        SDL_RumbleGamepad(c.gamepad, 0, 0, 0);
        SDL_CloseGamepad(c.gamepad);
    }
    if (c.wheel)
        SDL_CloseJoystick(c.wheel);
    const int accelerator = c.accelerator, brake = c.brake;
    controller_ = std::make_unique<Controller>();
    controller_->accelerator = accelerator;
    controller_->brake = brake;
}

void SdlSystem::setWheelPedals(int accelerator, int brake) {
    if (!controller_)
        controller_ = std::make_unique<Controller>();
    controller_->accelerator = accelerator;
    controller_->brake = brake;
    controller_->pedalAxis[0] = controller_->pedalAxis[1] = -1;
}

bool SdlSystem::readGamepad(GamepadState& out) {
    out = GamepadState{};
    if (!controller_)
        controller_ = std::make_unique<Controller>();
    if ((controller_->gamepad || controller_->wheel) && !controller_->connected())
        closeController();
    Controller& c = *controller_;
    if (!c.gamepad && !c.wheel) {
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
            if (count > 0)
                c.gamepad = SDL_OpenGamepad(ids[0]);
            SDL_free(ids);
        }
        if (!c.gamepad)
            if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
                for (int i = 0; i < count && !c.wheel; ++i)
                    if (SDL_GetJoystickTypeForID(ids[i]) == SDL_JOYSTICK_TYPE_WHEEL)
                        c.wheel = SDL_OpenJoystick(ids[i]);
                SDL_free(ids);
            }
        if (!c.gamepad && !c.wheel)
            return false;
        SDL_Joystick* j = c.joystick();
        c.isWheel = c.wheel || SDL_GetJoystickType(j) == SDL_JOYSTICK_TYPE_WHEEL;
        if (SDL_IsJoystickHaptic(j) && (SDL_WasInit(SDL_INIT_HAPTIC) || SDL_InitSubSystem(SDL_INIT_HAPTIC)))
            c.haptic = SDL_OpenHapticFromJoystick(j);
        if (c.haptic) {
            c.features = SDL_GetHapticFeatures(c.haptic);
            if (c.features & SDL_HAPTIC_GAIN)
                SDL_SetHapticGain(c.haptic, 100);
        }
        c.rumble = c.gamepad && SDL_GetBooleanProperty(SDL_GetGamepadProperties(c.gamepad),
                                                        SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    }
    out.wheel = c.isWheel;
    out.forceFeedback = c.haptic || c.rumble;
    // The rumble runs out unless sent again.
    if (c.rumble && (c.rumbleLow || c.rumbleHigh) && SDL_GetTicks() - c.rumbleSentMs > 500) {
        SDL_RumbleGamepad(c.gamepad, c.rumbleLow, c.rumbleHigh, 1000);
        c.rumbleSentMs = SDL_GetTicks();
    }

    if (c.gamepad) {
        const SDL_GamepadAxis axes[6] = {SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
                                         SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
        for (int i = 0; i < 6; ++i)
            out.axes[i] = std::max(-1.0f, float(SDL_GetGamepadAxis(c.gamepad, axes[i])) / 32767.0f);
        const SDL_GamepadButton buttons[kPadButtons] = {
            SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,     SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_GAMEPAD_BUTTON_DPAD_LEFT,      SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        for (int i = 0; i < kPadButtons; ++i)
            if (SDL_GetGamepadButton(c.gamepad, buttons[i]))
                out.buttons |= 1u << i;
        if (const char* name = SDL_GetGamepadName(c.gamepad))
            out.name = name;
        return true;
    }

    // A wheel by its axes: the wheel on the first; the pedals as set, or
    // the first two after it found resting at an end (pressed, they move
    // to the other). Each pedal 0 up to 1 pressed.
    SDL_Joystick* j = c.wheel;
    const int axisCount = SDL_GetNumJoystickAxes(j);
    auto axis = [&](int i) { return i >= 0 && i < axisCount ? float(SDL_GetJoystickAxis(j, i)) / 32767.0f : 0.0f; };
    out.axes[0] = std::max(-1.0f, axis(0));
    for (int i = 1; i < std::min(axisCount, 16); ++i)
        if (c.rest[i] == 0 && std::fabs(axis(i)) > 0.9f)
            c.rest[i] = axis(i) > 0 ? 1.0f : -1.0f;
    const int chosen[2] = {c.accelerator, c.brake};
    for (int p = 0; p < 2; ++p) {
        if (c.pedalAxis[p] < 0) {
            if (chosen[p] >= 0) {
                c.pedalAxis[p] = chosen[p];
            } else {
                const int other = c.pedalAxis[1 - p] >= 0 ? c.pedalAxis[1 - p] : chosen[1 - p];
                for (int i = 1; i < axisCount; ++i)
                    if (i != other && i < 16 && c.rest[i] != 0) {
                        c.pedalAxis[p] = i;
                        break;
                    }
            }
        }
        const int a = c.pedalAxis[p];
        if (a < 1 || a >= 16 || c.rest[a] == 0)
            continue;
        const float v = std::clamp(axis(a), -1.0f, 1.0f);
        out.axes[p == 0 ? 5 : 4] = std::clamp((c.rest[a] - v) * c.rest[a] / 2, 0.0f, 1.0f);
    }
    // Its buttons as the pad's, in order, and its hat as the d-pad.
    for (int i = 0; i < std::min(SDL_GetNumJoystickButtons(j), int(PadUp)); ++i)
        if (SDL_GetJoystickButton(j, i))
            out.buttons |= 1u << i;
    if (SDL_GetNumJoystickHats(j) > 0) {
        const uint8_t hat = SDL_GetJoystickHat(j, 0);
        if (hat & SDL_HAT_UP)
            out.buttons |= 1u << PadUp;
        if (hat & SDL_HAT_DOWN)
            out.buttons |= 1u << PadDown;
        if (hat & SDL_HAT_LEFT)
            out.buttons |= 1u << PadLeft;
        if (hat & SDL_HAT_RIGHT)
            out.buttons |= 1u << PadRight;
    }
    if (const char* name = SDL_GetJoystickName(j))
        out.name = name;
    return true;
}

void SdlSystem::setGamepadForce(const GamepadForce& f) {
    if (!controller_)
        return;
    Controller& c = *controller_;
    if (c.haptic) {
        if (c.features & SDL_HAPTIC_AUTOCENTER && f.autoCentre != c.autoCentre)
            SDL_SetHapticAutocenter(c.haptic, f.autoCentre ? 100 : 0);
        c.autoCentre = f.autoCentre;
        if (c.features & SDL_HAPTIC_CONSTANT) {
            SDL_HapticEffect e = effectOf(SDL_HAPTIC_CONSTANT);
            e.constant.level = level(f.constant);
            apply(c.haptic, c.constant, e);
        }
        for (const bool isSpring : {true, false}) {
            const uint32_t type = isSpring ? SDL_HAPTIC_SPRING : SDL_HAPTIC_DAMPER;
            if (!(c.features & type))
                continue;
            SDL_HapticEffect e = effectOf(uint16_t(type));
            const int16_t k = level(isSpring ? f.spring : f.damper);
            const uint16_t saturation = uint16_t(std::lround(std::clamp(isSpring ? f.springSaturation : 1.0f, 0.0f, 1.0f) * 65535));
            e.condition.right_coeff[0] = e.condition.left_coeff[0] = k;
            e.condition.right_sat[0] = e.condition.left_sat[0] = saturation;
            if (isSpring)
                e.condition.center[0] = level(f.springCentre);
            apply(c.haptic, isSpring ? c.spring : c.damper, e);
        }
        // A sine or square, made again when it changes from one to the other.
        const uint32_t wave = f.vibrationSquare ? SDL_HAPTIC_SQUARE : SDL_HAPTIC_SINE;
        if (c.features & wave) {
            if (c.periodic >= 0 && c.periodicSquare != f.vibrationSquare) {
                SDL_DestroyHapticEffect(c.haptic, c.periodic);
                c.periodic = -1;
            }
            c.periodicSquare = f.vibrationSquare;
            SDL_HapticEffect e = effectOf(uint16_t(wave));
            e.periodic.magnitude = level(f.vibration);
            e.periodic.period = uint16_t(std::clamp<uint32_t>(f.vibrationPeriodMs, 1, 65535));
            apply(c.haptic, c.periodic, e);
        }
        return;
    }
    if (c.rumble) {
        // A pad: the pushes on its heavy motor, the vibration on the light
        // one if it's quick (under a twentieth of a second), else the heavy.
        const bool quick = f.vibrationPeriodMs > 0 && f.vibrationPeriodMs < 50;
        const float low = std::fabs(f.constant) + (quick ? 0 : f.vibration), high = quick ? f.vibration : 0;
        c.rumbleLow = uint16_t(std::lround(std::clamp(low, 0.0f, 1.0f) * 65535));
        c.rumbleHigh = uint16_t(std::lround(std::clamp(high, 0.0f, 1.0f) * 65535));
        SDL_RumbleGamepad(c.gamepad, c.rumbleLow, c.rumbleHigh, 1000);
        c.rumbleSentMs = SDL_GetTicks();
    }
}

} // namespace reluva
