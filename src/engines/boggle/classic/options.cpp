// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/options.h"
#include <charconv>

namespace reluva::boggle {
int chooseClassicMusic(int choice, uint32_t seed) {
    if (choice != musicRandom) return choice;
    seed = seed * 1664525u + 1013904223u;
    return int(seed % classicMusicFiles.size());
}
Error readClassicOptions(const std::map<std::string,std::string>& settings, ClassicOptions& out) {
    out = {}; ClassicOptions parsed;
    if (auto it = settings.find("classic_variant"); it != settings.end()) {
        if (it->second == "master") parsed.variant = ClassicVariant::Master;
        else if (it->second != "original") return Error::fail("Unsupported Classic variant; choose Original or Master in game options");
    }
    if(auto it=settings.find("classic_cube_style");it!=settings.end()) {
        if(it->second=="rounded")parsed.cubeStyle=ClassicCubeStyle::Rounded;
        else if(it->second=="angular")parsed.cubeStyle=ClassicCubeStyle::Angular;
        else if(it->second!="auto")return Error::fail("Classic cube style must be auto, rounded or angular");
    }
    if(auto it=settings.find("classic_turns");it!=settings.end()) {
        if(it->second=="chess")parsed.turns=ClassicTurnMode::Chess;
        else if(it->second!="traditional")return Error::fail("Classic turn mode must be traditional or chess");
    }
    if (auto it = settings.find("classic_seconds"); it != settings.end()) {
        uint64_t seconds = 0; const auto& text = it->second;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), seconds);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            (seconds != 0 && (seconds < 60 || seconds > 300 || seconds % 30)))
            return Error::fail("Unsupported Classic duration; choose Not Timed or 1 to 5 minutes in 30-second steps");
        parsed.duration = seconds * 1000;
    }
    for (const auto& [key, flag] : {std::pair{"classic_cursor", &parsed.cursor}, std::pair{"classic_background", &parsed.background}, std::pair{"classic_sound", &parsed.sound}, std::pair{"classic_animations", &parsed.animations}, std::pair{"classic_menu", &parsed.menu}, std::pair{"frontend_movies", &parsed.movies}, std::pair{"classic_artwork", &parsed.artwork}, std::pair{"classic_auto_orient", &parsed.autoOrient}, std::pair{"classic_handwriting", &parsed.handwriting}}) {
        if (auto it = settings.find(key); it != settings.end()) {
            if (it->second != "0" && it->second != "1") return Error::fail("Classic checkbox options must be 0 or 1");
            *flag = it->second == "1";
        }
    }
    if (auto it = settings.find("classic_music"); it != settings.end()) {
        const auto& text = it->second;
        if (text == "none") parsed.music = musicNone;
        else if (text == "random") parsed.music = musicRandom;
        else {
            int track = -1;
            const auto result = std::from_chars(text.data(), text.data()+text.size(), track);
            if (result.ec != std::errc{} || result.ptr != text.data()+text.size() ||
                track < 0 || track >= int(classicMusicFiles.size()) || text != std::to_string(track))
                return Error::fail("Unsupported Classic music track");
            parsed.music = track;
        }
    }
    if(auto it=settings.find("classic_seed");it!=settings.end()) {
        uint32_t seed=0;const auto& text=it->second;
        const auto result=std::from_chars(text.data(),text.data()+text.size(),seed);
        if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || text!=std::to_string(seed))
            return Error::fail("Classic replay seed must be an unsigned 32-bit decimal integer");
        parsed.seed=seed;
    }
    out = parsed; return Error::success();
}
} // namespace reluva::boggle
