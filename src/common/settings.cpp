// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/settings.h"

#include "audio/mixer.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::settings {

namespace {

const std::string *find(const std::map<std::string, std::string> &s, const char *key) {
    const auto it = s.find(key);
    return it == s.end() || it->second.empty() ? nullptr : &it->second;
}

bool flag(const std::map<std::string, std::string> &s, const char *key, bool fallback) {
    const std::string *v = find(s, key);
    return v ? *v != "0" : fallback;
}

} // namespace

DisplayOptions display(const std::map<std::string, std::string> &s) {
    DisplayOptions d;
    d.fullscreen = flag(s, kFullscreen, d.fullscreen);
    if (const std::string *v = find(s, kWindowScale))
        d.windowScale = *v == "auto" ? DisplayOptions::kAutoScale : std::clamp(std::atoi(v->c_str()), 0, 8);
    d.integerScaling = flag(s, kIntegerScaling, d.integerScaling);
    d.smooth = flag(s, kSmooth, d.smooth);
    d.vsync = flag(s, kVsync, d.vsync);
    return d;
}

int volume(const std::map<std::string, std::string> &s, const char *key, int fallbackPercent) {
    int percent = fallbackPercent;
    if (const std::string *v = find(s, key)) {
        char *end = nullptr;
        const long n = std::strtol(v->c_str(), &end, 10);
        if (end && !*end)
            percent = int(n);
    }
    return std::clamp(percent, 0, 200) * audio::kMaxVolume / 100;
}

} // namespace reluva::settings
