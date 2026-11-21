// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/options.h"

#include <cstdlib>
#include <system_error>
#include <string>

namespace reluva::stawayteam {

namespace {

// The program's section and keys (0x63b6e0, 0x6496a0, 0x64968c, 0x649678, 0x63b990).
const char *const kSection = "Options";
const char *const kScrollSpeed = "Scroll Speed";
const char *const kScrollAccel = "Scroll Acceleration";
const char *const kPauseWhenHit = "Pause Game When Hit";
const char *const kBrightness = "Screen Brightness";

float number(const Config &file, const char *key, float fallback) {
    const std::string value = file.get(kSection, key);
    if (value.empty())
        return fallback;
    char *end = nullptr;
    const float n = std::strtof(value.c_str(), &end);
    return end && *end == '\0' ? n : fallback;
}

} // namespace

void GameOptions::load(const std::filesystem::path &file) {
    _path = file;
    _loaded = !file.empty() && !_file.load(file);
    if (!_loaded)
        return;
    scrollSpeed = number(_file, kScrollSpeed, kScrollSpeedDefault);
    scrollAccel = number(_file, kScrollAccel, kScrollAccelDefault);
    pauseWhenHit = _file.get(kSection, kPauseWhenHit, "1") != "0";
    brightness = number(_file, kBrightness, kBrightnessMin);
}

void GameOptions::save() const {
    if (!_loaded)
        return;
    Config file = _file;
    file.set(kSection, kScrollSpeed, std::to_string(scrollSpeed));
    file.set(kSection, kScrollAccel, std::to_string(scrollAccel));
    file.set(kSection, kPauseWhenHit, pauseWhenHit ? "1" : "0");
    file.set(kSection, kBrightness, std::to_string(brightness));
    // The folder for the game's files may not be made yet.
    std::error_code ec;
    std::filesystem::create_directories(_path.parent_path(), ec);
    (void)file.save();
}

} // namespace reluva::stawayteam
