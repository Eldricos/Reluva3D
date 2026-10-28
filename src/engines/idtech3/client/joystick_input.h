// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cvars.h"

#include "common/events.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// The joystick as Elite Force's engines read it once a frame (both
// stvoy.exe and stvoyHM.exe; see docs/formats/idtech3/eliteforce-input.md):
// its buttons as the keys JOY1 to JOY32; its x and y axes either analog
// (joy_xbutton, joy_ybutton 0), turning and moving the player through
// PlayerInput, or, like its third and fourth axes, a key for each way past
// joy_threshold (x: the arrow keys left and right, y: up and down; the
// others JOY16 to JOY19); the hat's four ways JOY24 to JOY27. Nothing
// while in_joystick is 0, as the original.
class JoystickInput {
public:
    // The original's settings, at its defaults.
    static void registerCvars(CvarStore& cvars);

    // A joystick event from the system (the rest are ignored).
    void event(const Event& ev);

    // This frame's reading: the keys that went down or up since the last
    // (Quake III key numbers), and the analog axes, -127 to 127 (x right
    // and y forward negative, as the original's), 0 when not analog.
    struct Frame {
        std::vector<std::pair<int32_t, bool>> keys;
        int32_t x = 0, y = 0;
    };
    Frame frame(const CvarStore& cvars);

private:
    uint32_t buttons_ = 0, lastButtons_ = 0; // JOY1 to JOY32 held
    std::array<int32_t, 4> axes_{};          // -32768 to 32767
    uint8_t hat_ = 0;
    uint32_t lastWays_ = 0; // the axes' and hat's keys held (the original's bits)
};

} // namespace reluva::idtech3
