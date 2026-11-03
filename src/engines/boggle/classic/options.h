// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <map>

namespace reluva::boggle {
enum class ClassicVariant { Original, Master };
struct ClassicOptions {
    ClassicVariant variant = ClassicVariant::Original;
    uint64_t duration = 180000; // milliseconds; zero means Not Timed
    bool sound = true, animations = true;
    size_t side() const { return variant == ClassicVariant::Master ? 5 : 4; }
    const char* dice() const { return variant == ClassicVariant::Master ? "Dicts/EngMas.dst" : "Dicts/EngOrg.dst"; }
    const char* palette() const { return variant == ClassicVariant::Master ? "Palettes/ClsM1Pal.pal" : "Palettes/ClsO1Pal.pal"; }
};
Error readClassicOptions(const std::map<std::string,std::string>& settings, ClassicOptions& out);
} // namespace reluva::boggle
