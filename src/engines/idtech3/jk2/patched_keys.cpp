// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/patched_keys.h"

#include "engines/idtech3/client/bindings.h"

#include <array>
#include <utility>

namespace reluva::idtech3::jk2 {

namespace {

// Quake III's number (or Reluva's), 1.04's: matched by the names both
// tables give.
constexpr std::array<std::pair<int32_t, int32_t>, 58> kKeys = {{
    {q3key::kTab, 9},
    {q3key::kEnter, 10},
    {q3key::kEscape, 27},
    {q3key::kSpace, 32},
    {q3key::kBackspace, 8},
    {q3key::kCapsLock, 4},
    {q3key::kScrollLock, 6},
    {'`', kPatchedConsoleKey}, // whatever character the key under Escape has (0x4470dc)
    {q3key::kPause, 7},
    {q3key::kUp, 170},
    {q3key::kDown, 171},
    {q3key::kLeft, 172},
    {q3key::kRight, 173},
    {q3key::kAlt, 3},
    {q3key::kCtrl, 2},
    {q3key::kShift, 1},
    {q3key::kInsert, 143},
    {q3key::kDelete, 127},
    {q3key::kPageDown, 158},
    {q3key::kPageUp, 145},
    {q3key::kHome, 144},
    {q3key::kEnd, 157},
    {q3key::kF1, 28},
    {q3key::kF1 + 1, 29},
    {q3key::kF1 + 2, 30},
    {q3key::kF1 + 3, 31},
    {q3key::kF1 + 4, 132},
    {q3key::kF1 + 5, 133},
    {q3key::kF1 + 6, 134},
    {q3key::kF1 + 7, 135},
    {q3key::kF1 + 8, 149},
    {q3key::kF1 + 9, 150},
    {q3key::kF1 + 10, 151},
    {q3key::kF1 + 11, 152},
    {160, 23}, // KP_HOME
    {161, 24}, // KP_UPARROW
    {162, 25}, // KP_PGUP
    {163, 20}, // KP_LEFTARROW
    {164, 21}, // KP_5
    {165, 22}, // KP_RIGHTARROW
    {166, 17}, // KP_END
    {167, 18}, // KP_DOWNARROW
    {168, 19}, // KP_PGDN
    {q3key::kKpEnter, 13},
    {170, 16},  // KP_INS
    {171, 14},  // KP_DEL
    {172, 247}, // KP_SLASH
    {173, 12},  // KP_MINUS
    {174, 11},  // KP_PLUS
    {q3key::kNumLock, 5},
    {176, 215}, // KP_STAR
    {q3key::kMouse1, 141},
    {q3key::kMouse2, 142},
    {q3key::kMouse3, 166},
    {q3key::kMouse4, 167},
    {q3key::kMouse5, 168},
    {q3key::kWheelDown, 139},
    {q3key::kWheelUp, 137},
}};

// The joystick's buttons, JOY1 on, are 1.04's JOY0 on (256), and AUX1 on
// its AUX0 on (288).
constexpr int32_t kJoy = 185;
constexpr int32_t kAux = 217;
constexpr int32_t kAuxEnd = 233;
constexpr int32_t kPatchedJoy = 256;
constexpr int32_t kPatchedAux = 288;

} // namespace

int32_t toPatchedKey(int32_t key) {
    if (key >= q3key::kCharFlag)
        return key;
    for (const auto &[q3, patched] : kKeys)
        if (q3 == key)
            return patched;
    // Letters are their virtual keys, capitals (0x502aa0); other
    // characters as MapVirtualKey gives them, Windows-1252's past ASCII,
    // whatever key of 1.04's that number is besides.
    if (key >= 'a' && key <= 'z')
        return key - 'a' + 'A';
    if (key > ' ' && key < 127)
        return key;
    if (key >= q3key::kCharacters + 0x80 && key < q3key::kCharacters + 0x100)
        return key - q3key::kCharacters;
    if (key >= kJoy && key < kAuxEnd)
        return key < kAux ? kPatchedJoy + key - kJoy : kPatchedAux + key - kAux;
    return 0;
}

int32_t fromPatchedKey(int32_t key) {
    if (key >= q3key::kCharFlag)
        return key;
    for (const auto &[q3, patched] : kKeys)
        if (patched == key)
            return q3;
    if (key >= 'A' && key <= 'Z')
        return key - 'A' + 'a';
    if (key > ' ' && key < 127)
        return key;
    if (key >= 0x80 && key < 0x100)
        return q3key::kCharacters + key;
    if (key >= kPatchedJoy && key < kPatchedAux + kAuxEnd - kAux)
        return key < kPatchedAux ? kJoy + key - kPatchedJoy : kAux + key - kPatchedAux;
    return 0;
}

} // namespace reluva::idtech3::jk2
