// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <map>
#include <string>

namespace reluva::settings {

// The settings every engine has, as kept in reluva.ini: for every game
// ([reluva]), for an engine's games ([engine:<id>]) or for one game (its own
// section); a game gets them merged (Config::settingsFor). A setting not set
// anywhere is Reluva's default, which is how Reluva has always played.
//
// Display (applied by the frontend, System::setDisplayOptions):
constexpr const char *kFullscreen = "fullscreen";          // "1": start in full screen (default "0")
constexpr const char *kWindowScale = "window_scale";       // "" (default): leave the window as it is;
                                                           // "auto": 2x where it fits, else 1x; "1".."4"
constexpr const char *kIntegerScaling = "integer_scaling"; // "1": whole multiples only, with borders
constexpr const char *kSmooth = "smooth";                  // "1": smooth the picture when scaled
constexpr const char *kVsync = "vsync";                    // "0": don't wait for the display (default "1")
// Sound (applied by each engine to its Mixer):
constexpr const char *kMasterVolume = "master_volume"; // percent, 0..200 (default 100)

DisplayOptions display(const std::map<std::string, std::string> &settings);
// A percentage setting (0..200, `fallback` when unset or not a number) on
// the Mixer's scale (audio::kMaxVolume is 100%).
int volume(const std::map<std::string, std::string> &settings, const char *key, int fallbackPercent = 100);

} // namespace reluva::settings
