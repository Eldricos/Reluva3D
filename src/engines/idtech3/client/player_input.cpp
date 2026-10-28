// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/player_input.h"

#include "engines/idtech3/bindings.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace reluva::idtech3 {

namespace {

// An angle in degrees as usercmd_t carries it: 65536 to a full turn.
int32_t toShort(float degrees) { return int32_t(std::lround(degrees * 65536.0f / 360.0f)) & 0xffff; }

float setting(const CvarStore& cvars, const char* name, float fallback) {
    return cvars.find(name) ? cvars.value(name) : fallback;
}

} // namespace

bool PlayerInput::isButton(const std::string& name) {
    static const char* const kButtons[] = {"forward", "back", "moveleft", "moveright", "moveup", "movedown",
                                           "left", "right", "lookup", "lookdown", "speed", "strafe",
                                           "attack", "altattack", "mlook", "klook"};
    for (const char* b : kButtons)
        if (name == b)
            return true;
    return name.rfind("button", 0) == 0 && name.size() > 6 && name.size() <= 8 &&
           std::isdigit(static_cast<unsigned char>(name[6]));
}

void PlayerInput::runBinding(const std::string& line, int32_t key, bool down,
                             const std::function<void(const std::string&)>& command) {
    for (const std::string& text : splitCommands(line)) {
        const std::vector<std::string> w = splitCommand(text);
        if (w.empty())
            continue;
        if (w[0][0] == '+' || w[0][0] == '-') {
            if (w[0][0] == '-')
                continue; // a key bound to "-x" alone does nothing useful
            const std::string name = fs::toLower(w[0].substr(1));
            if (isButton(name)) {
                if (down)
                    buttons_[name].insert(key);
                else
                    buttons_[name].erase(key);
            } else {
                std::string pair = (down ? "+" : "-") + name;
                for (size_t i = 1; i < w.size(); ++i)
                    pair += " " + w[i];
                command(pair);
            }
            continue;
        }
        if (down)
            command(text);
    }
}

bool PlayerInput::held(const char* button) const {
    const auto it = buttons_.find(button);
    return it != buttons_.end() && !it->second.empty();
}

void PlayerInput::turn(const CvarStore& cvars, int32_t dtMs) {
    float mx = mouseX_, my = mouseY_;
    mouseX_ = mouseY_ = 0;
    if (setting(cvars, "m_filter", 0) != 0) {
        const float px = mx, py = my;
        mx = (mx + lastMouseX_) / 2;
        my = (my + lastMouseY_) / 2;
        lastMouseX_ = px;
        lastMouseY_ = py;
    }
    // Degrees per unit moved are the sensitivity times m_yaw and m_pitch
    // (negative m_pitch: inverted); m_filter averages this move with the
    // last. Moving the mouse up looks up: pitch counts downwards.
    const float sensitivity = setting(cvars, "sensitivity", 5);
    yaw -= mx * sensitivity * setting(cvars, "m_yaw", 0.022f);
    if (setting(cvars, "cl_freelook", 1) != 0 || held("mlook"))
        pitch += my * sensitivity * setting(cvars, "m_pitch", 0.022f);
    const float keyScale =
        (held("speed") ? setting(cvars, "cl_anglespeedkey", 1.5f) : 1.0f) * float(dtMs) / 1000.0f;
    if (!held("strafe"))
        yaw += float(held("left") - held("right")) * setting(cvars, "cl_yawspeed", 140) * keyScale;
    pitch += float(held("lookdown") - held("lookup")) * setting(cvars, "cl_pitchspeed", 140) * keyScale;
    // The joystick, as the original (CL_JoystickMove): its x at a
    // hundredth of cl_yawspeed per step of 127, unless strafing; its y
    // looks only with +mlook held (otherwise it moves; command()).
    if (!held("strafe"))
        yaw += setting(cvars, "cl_yawspeed", 140) * 0.01f * float(joyX_) * keyScale;
    if (held("mlook"))
        pitch += setting(cvars, "cl_pitchspeed", 140) * 0.01f * float(joyY_) * keyScale;
    // Not held within straight up and down here: the game keeps the view
    // there, adding its own offset (the player state's delta angles, set
    // when spawning to face the spawn point's way). Holding the command's
    // pitch as well left the view stuck above level whenever that offset
    // pointed upwards. Kept to a turn either way, as it wraps anyway.
    pitch = std::remainder(pitch, 360.0f);
}

PlayerInput::Command PlayerInput::command(const CvarStore& cvars, const GameInfo& game) const {
    Command cmd;
    cmd.angles = {toShort(pitch), toShort(yaw), 0};
    for (const auto& [name, keys] : buttons_) {
        if (keys.empty())
            continue;
        if (name == "attack")
            cmd.buttons |= game.buttonAttack;
        else if (name == "altattack")
            cmd.buttons |= game.buttonAltAttack;
        else if (name.rfind("button", 0) == 0)
            cmd.buttons |= 1 << std::clamp(std::atoi(name.c_str() + 6), 0, 31);
    }
    const int speed = (cvars.find("cl_run") ? cvars.integer("cl_run") != 0 : true) != held("speed") ? 127 : 64;
    int side = held("moveright") - held("moveleft");
    if (held("strafe"))
        side += held("right") - held("left");
    int forward = std::clamp(speed * (held("forward") - held("back")), -127, 127);
    side = std::clamp(speed * side, -127, 127);
    // The joystick's, added to the keys' (CL_JoystickMove).
    if (held("strafe"))
        side = std::clamp(side + joyX_, -127, 127);
    if (!held("mlook"))
        forward = std::clamp(forward + joyY_, -127, 127);
    cmd.forward = int8_t(forward);
    cmd.right = int8_t(side);
    cmd.up = int8_t(std::clamp(speed * (held("moveup") - held("movedown")), -127, 127));
    return cmd;
}

} // namespace reluva::idtech3
