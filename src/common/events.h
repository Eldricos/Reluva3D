// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <string>

namespace reluva {

// Key codes are backend-independent. Printable keys use their lowercase
// ASCII value; everything else uses the values below.
namespace key {
constexpr uint32_t kUnknown = 0;
constexpr uint32_t kBackspace = 8;
constexpr uint32_t kTab = 9;
constexpr uint32_t kReturn = 13;
constexpr uint32_t kEscape = 27;
constexpr uint32_t kSpace = 32;
constexpr uint32_t kDelete = 127;

constexpr uint32_t kUp = 0x100;
constexpr uint32_t kDown = 0x101;
constexpr uint32_t kLeft = 0x102;
constexpr uint32_t kRight = 0x103;
constexpr uint32_t kInsert = 0x104;
constexpr uint32_t kHome = 0x105;
constexpr uint32_t kEnd = 0x106;
constexpr uint32_t kPageUp = 0x107;
constexpr uint32_t kPageDown = 0x108;
constexpr uint32_t kF1 = 0x110;  // F1..F12 are consecutive
constexpr uint32_t kKp0 = 0x120; // the numeric keypad's 0..9 are consecutive, whatever NumLock says
constexpr uint32_t kKpPeriod = 0x12a;
constexpr uint32_t kKpEnter = 0x12b;
constexpr uint32_t kKpPlus = 0x12c;
constexpr uint32_t kKpMinus = 0x12d;
constexpr uint32_t kKpMultiply = 0x12e;
constexpr uint32_t kKpDivide = 0x12f;

constexpr uint32_t kLShift = 0x130;
constexpr uint32_t kRShift = 0x131;
constexpr uint32_t kLCtrl = 0x132;
constexpr uint32_t kRCtrl = 0x133;
constexpr uint32_t kLAlt = 0x134;
constexpr uint32_t kRAlt = 0x135;
} // namespace key

namespace keymod {
constexpr uint16_t kShift = 1 << 0;
constexpr uint16_t kCtrl = 1 << 1;
constexpr uint16_t kAlt = 1 << 2;
} // namespace keymod

enum class EventType {
    kNone,
    kQuit,             // user closed the window: leave the engine and the app
    kReturnToLauncher, // user asked to go back to the launcher
    kKeyDown,
    kKeyUp,
    kMouseMove,
    kMouseDown,
    kMouseUp,
    kMouseWheel,
    kTextInput, // text typed, after the keyboard layout and dead keys (System::setTextInput)
    // The joystick (the first connected): a button by its number from 0,
    // an axis moved (its number in the device's order: x, y, then the
    // rest; -32768 to 32767, left and up negative), the hat's directions
    // (joyhat bits), and the joystick gone (all let go).
    kJoyButtonDown,
    kJoyButtonUp,
    kJoyAxis,
    kJoyHat,
    kJoyRemoved,
    // The window got or lost the keyboard's focus (the player switched to
    // another program and back), for games that pause meanwhile.
    kFocusGained,
    kFocusLost,
};

namespace joyhat {
constexpr uint8_t kUp = 1, kRight = 2, kDown = 4, kLeft = 8;
} // namespace joyhat

enum class MouseButton { kNone, kLeft, kRight, kMiddle };

struct Event {
    EventType type = EventType::kNone;
    uint32_t key = key::kUnknown; // KeyDown/KeyUp
    uint16_t mods = 0;            // KeyDown/KeyUp, and the mouse events (keymod bits)
    bool repeat = false;          // KeyDown auto-repeat
    int mouseX = 0, mouseY = 0;   // mouse events, in game framebuffer pixels
    float moveX = 0, moveY = 0;   // MouseMove: how far the mouse moved, in its own units (System::setRelativeMouse)
    MouseButton button = MouseButton::kNone;
    int clicks = 0; // MouseDown/MouseUp: 1, or 2 for a double click, ...
    int wheelY = 0;
    char text[32] = {}; // TextInput: UTF-8, NUL-terminated
    int joyButton = 0;  // JoyButtonDown/JoyButtonUp
    int joyAxis = 0;    // JoyAxis
    int joyValue = 0;   // JoyAxis: -32768 to 32767
    uint8_t joyHat = 0; // JoyHat: joyhat bits held
};

// A game controller's state, as System::readGamepad gives it: SDL's
// gamepad layout (an Xbox-style pad). A racing wheel comes the same way:
// the wheel as the left stick's X, the brake and accelerator as the
// triggers, its buttons as the pad's.
struct GamepadState {
    // Sticks -1..1 (left X, left Y, right X, right Y; +Y down), then the
    // triggers 0..1 (left, right).
    float axes[6] = {};
    // A bit for each GamepadButton held.
    uint32_t buttons = 0;
    std::string name;
    bool wheel = false;         // a racing wheel
    bool forceFeedback = false; // it can be given forces (a wheel's motor) or rumble (System::setGamepadForce)
};

// The forces a game controller is to give (System::setGamepadForce), as
// DirectInput and SDL's haptics describe them, along X (a wheel's turn, a
// stick's left and right). A force's sign is the side it comes from: +1
// from the right, pushing to the left. Gains are applied already.
struct GamepadForce {
    float constant = 0;         // -1..1: a steady push
    float spring = 0;           // 0..1: how hard it's pulled to springCentre
    float springCentre = 0;     // -1..1
    float springSaturation = 1; // 0..1: the most the spring pulls with
    float damper = 0;           // 0..1: resisting being moved, by how fast
    float vibration = 0;        // 0..1: a periodic push, back and forth
    uint32_t vibrationPeriodMs = 0;
    bool vibrationSquare = false; // a square wave, else a sine
    bool autoCentre = false;      // the device's own centring spring (DirectInput's DIPROP_AUTOCENTER)
    bool operator==(const GamepadForce &) const = default;
};
enum GamepadButton {
    kPadSouth,
    kPadEast,
    kPadWest,
    kPadNorth,
    kPadBack,
    kPadGuide,
    kPadStart,
    kPadLeftStick,
    kPadRightStick,
    kPadLeftShoulder,
    kPadRightShoulder,
    kPadUp,
    kPadDown,
    kPadLeft,
    kPadRight,
    kPadButtons
};

} // namespace reluva
