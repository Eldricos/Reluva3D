// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoisland/play_settings.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace reluva::legoisland {

PlaySettings playSettings(const std::map<std::string, std::string> &settings) {
    auto set = [&](const char *key) -> const std::string * {
        const auto it = settings.find(key);
        return it == settings.end() ? nullptr : &it->second;
    };
    PlaySettings out;
    // One that isn't a number is left at the default; one below 0 is no limit.
    if (const std::string *v = set("framerate")) {
        int rate = 0;
        if (std::from_chars(v->data(), v->data() + v->size(), rate).ec == std::errc())
            out.frameRate = std::max(0, rate);
    }
    if (const std::string *v = set("turning"))
        out.turningByTime = *v != "frame";
    if (const std::string *v = set("islandquality"))
        out.registry.emplace_back("Island Quality", *v);
    if (const std::string *v = set("islandtexture"))
        out.registry.emplace_back("Island Texture", *v);
    for (const auto &[key, name] : {std::pair{"music", "Music"}, {"sound3d", "3DSound"}, {"drawcursor", "Draw Cursor"}})
        if (const std::string *v = set(key))
            out.registry.emplace_back(name, *v != "0" ? "YES" : "NO");
    // The first joystick connected, as joystick 0.
    if (const std::string *v = set("usejoystick"); v && *v != "0") {
        out.joystick = true;
        out.registry.emplace_back("UseJoystick", "YES");
        out.registry.emplace_back("JoystickIndex", "0");
    }
    return out;
}

} // namespace reluva::legoisland
