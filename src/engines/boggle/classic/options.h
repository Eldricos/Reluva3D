// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <map>
#include <array>

namespace reluva::boggle {
// Resource filenames only; display titles are read from the installed DLL.
inline constexpr std::array<const char*,15> classicMusicFiles = {
    "Clasical", "BogDown", "NewAge", "Starlite", "Stroll", "Smokey", "MaconOut",
    "Beach", "Volvo", "LoveBead", "Safari", "Strut", "Snappin", "Blonde", "Texas"
};
inline constexpr int musicNone = -1, musicRandom = -2;
enum class ClassicVariant { Original, Master };
struct ClassicOptions {
    ClassicVariant variant = ClassicVariant::Original;
    uint64_t duration = 180000; // milliseconds; zero means Not Timed
    bool sound = true, animations = true, menu = true, movies = true;
    int music = musicNone;
    size_t side() const { return variant == ClassicVariant::Master ? 5 : 4; }
    const char* dice() const { return variant == ClassicVariant::Master ? "Dicts/EngMas.dst" : "Dicts/EngOrg.dst"; }
    const char* palette() const { return variant == ClassicVariant::Master ? "Palettes/ClsM1Pal.pal" : "Palettes/ClsO1Pal.pal"; }
};
// Select once for a native game session; does not emulate the original RNG.
int chooseClassicMusic(int choice, uint32_t seed);
Error readClassicOptions(const std::map<std::string,std::string>& settings, ClassicOptions& out);
} // namespace reluva::boggle
