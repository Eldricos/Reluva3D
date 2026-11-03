// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/options.h"
#include <charconv>

namespace reluva::boggle {
Error readClassicOptions(const std::map<std::string,std::string>& settings, ClassicOptions& out) {
    out = {}; ClassicOptions parsed;
    if (auto it = settings.find("classic_variant"); it != settings.end()) {
        if (it->second == "master") parsed.variant = ClassicVariant::Master;
        else if (it->second != "original") return Error::fail("Unsupported Classic variant; choose Original or Master in game options");
    }
    if (auto it = settings.find("classic_seconds"); it != settings.end()) {
        uint64_t seconds = 0; const auto& text = it->second;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), seconds);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            (seconds != 0 && (seconds < 60 || seconds > 300 || seconds % 60)))
            return Error::fail("Unsupported Classic duration; choose Not Timed or 1, 2, 3, 4 or 5 minutes");
        parsed.duration = seconds * 1000;
    }
    for (const auto& [key, flag] : {std::pair{"classic_sound", &parsed.sound}, std::pair{"classic_animations", &parsed.animations}}) {
        if (auto it = settings.find(key); it != settings.end()) {
            if (it->second != "0" && it->second != "1") return Error::fail("Classic sound/animation options must be 0 or 1");
            *flag = it->second == "1";
        }
    }
    out = parsed; return Error::success();
}
} // namespace reluva::boggle
