// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

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
    kPadDown,   // a game controller's button (Event::pad)
    kPadUp,
    kPadAxis, // a game controller's stick or trigger moved (Event::pad, Event::value)
};

// A game controller's buttons and axes, laid out as an Xbox pad's (other
// pads by position: South is A or Cross).
namespace pad {
enum Button : int {
    kSouth,
    kEast,
    kWest,
    kNorth,
    kBack,
    kGuide,
    kStart,
    kLeftStick,
    kRightStick,
    kLeftShoulder,
    kRightShoulder,
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kButtons
};
enum Axis : int { kLeftX, kLeftY, kRightX, kRightY, kLeftTrigger, kRightTrigger, kAxes };
} // namespace pad

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
    int pad = -1;       // PadDown/PadUp: a pad::Button; PadAxis: a pad::Axis
    float value = 0;    // PadAxis: sticks -1 (left, up) .. 1, triggers 0 .. 1
};

} // namespace reluva
