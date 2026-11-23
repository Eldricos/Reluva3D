// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace reluva::legoisland {

/** Frames a second when the settings don't say: a display's. */
constexpr int kDefaultFrameRate = 60;

/**
 * A Direct3D device as the setup (CONFIG.EXE) writes it to the registry:
 * its "3D Device ID", the display driver (0, the primary display), then the
 * device's GUID as four words in hex; and its "3D Device Name".
 */
struct SetupDevice {
    const char *id;
    const char *name;
};

/** Direct3D's hardware device, HAL: the game smooths its textures on it. */
inline constexpr SetupDevice kHardwareDevice{"0 0x84e63de0 0x11cf46aa 0x00006f81 0x6e1520c0", "Direct3D HAL"};
/** Its software one, as the setup wrote it on a player's machine: textures unsmoothed. */
inline constexpr SetupDevice kSoftwareDevice{"0 0xa4665c60 0x11cf2673 0xaa001aa3 0x5633b900", "RGB Emulation"};

/**
 * How a game is played, from the launcher's settings for it (the engine's
 * options, MetaEngine::engineOptions).
 */
struct PlaySettings {
    int frameRate = kDefaultFrameRate; // at most so many a second; 0, no limit
    bool turningByTime = true;
    bool joystick = false;
    // The program's registry settings, as CONFIG.EXE writes them
    // (GameProcess::setSetting): only those set, the rest left at the
    // program's own defaults (but for the island's model, GameProcess's).
    std::vector<std::pair<std::string, std::string>> registry;
};

/** The game's PlaySettings from its settings (GameTarget::settings). */
PlaySettings playSettings(const std::map<std::string, std::string> &settings);

} // namespace reluva::legoisland
