// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/resources.h"
#include "common/system.h"
#include <array>
namespace reluva::boggle {
inline constexpr std::array<const char*,5> menuMovieNames={"ClsBut8","BrkBut8","BatBut8","SpcBut8","TetBut8"};
class FrontendMedia {
public:
    Error open(const Resources& resources,const std::filesystem::path& root,bool enabled);
    bool button(Surface& screen,size_t index,int x,int y,uint64_t elapsed) const;
    static Error intro(System& system,const Resources& resources,const std::filesystem::path& root,bool& quit);
private:
    std::array<video::AviCinepak,5> buttons_;
};
} // namespace reluva::boggle
