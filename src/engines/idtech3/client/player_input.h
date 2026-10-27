// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cvars.h"
#include "engines/idtech3/games.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace reluva::idtech3 {

// The player's input as Quake III's client turns it into commands: keys
// run their bindings, the engine's +buttons ("forward", "attack"...) held
// while a key holding them is; the view turned by the mouse and keys; and
// from those, the command's angles, buttons and movement.
class PlayerInput {
public:
    // A key's binding (`line`), pressed or released: "+name" holds the
    // engine's button while the key is down, or runs the client game's
    // +name/-name pair through `command`; other commands run on the press.
    void runBinding(const std::string& line, int32_t key, bool down,
                    const std::function<void(const std::string&)>& command);
    bool held(const char* button) const;
    void releaseAll() { buttons_.clear(); }
    // The buttons held, each with the keys holding it.
    const std::map<std::string, std::set<int32_t>>& buttons() const { return buttons_; }

    // The mouse moved (pixels), until the next turn().
    void addMouse(float dx, float dy) {
        mouseX_ += dx;
        mouseY_ += dy;
    }
    // Turns the view by the mouse moved and the keys held over `dtMs`, at
    // the original's settings: sensitivity, m_yaw, m_pitch, m_filter,
    // cl_freelook (else +mlook to look up and down), cl_yawspeed and
    // cl_pitchspeed degrees a second, times cl_anglespeedkey with +speed;
    // with +strafe, left and right step instead of turning.
    void turn(const CvarStore& cvars, int32_t dtMs);
    float yaw = 0, pitch = 0; // degrees

    // The command for this moment: where the player looks (65536 to a
    // turn), the game's button bits held, and movement (running unless
    // +speed is held with cl_run 1, the default, or the reverse).
    struct Command {
        std::array<int32_t, 3> angles{};
        int32_t buttons = 0;
        int8_t forward = 0, right = 0, up = 0;
    };
    Command command(const CvarStore& cvars, const GameInfo& game) const;

    // Whether `name` is one of the engine's buttons (not the client game's).
    static bool isButton(const std::string& name);

private:
    std::map<std::string, std::set<int32_t>> buttons_; // and the keys holding each
    float mouseX_ = 0, mouseY_ = 0;                     // moved since the last turn
    float lastMouseX_ = 0, lastMouseY_ = 0;             // the last turn's, for m_filter
};

} // namespace reluva::idtech3
