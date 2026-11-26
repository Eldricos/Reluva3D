// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/resources.h"
#include "engines/boggle/frontend/layout.h"
#include "common/system.h"
#include <array>
#include "common/audio/mixer.h"
namespace reluva::boggle {
inline constexpr std::array<const char*,5> menuMovieNames={"ClsBut8","BrkBut8","BatBut8","SpcBut8","TetBut8"};
// Observed per-button animation intervals; idle buttons hold their last frame.
class MenuMotion {
public:
    void activate(uint64_t now);
    void update(uint64_t now,int hovered,bool focused=true);
    size_t frame(size_t index) const {return index<5?frames_[index]:0;}
    uint64_t cycle(size_t index) const {return index<5?cycles_[index]:0;}
    bool focused() const {return focused_;}
    bool playing(size_t index) const {return focused_ && (hovered_==int(index) || (index==2 && intro_));}
    void counts(std::array<size_t,5> value){counts_=value;}
private:
    std::array<size_t,5> counts_{30,30,30,51,30},frames_{};
    std::array<uint64_t,5> cycles_{},carry_{};
    uint64_t last_=0;int hovered_=-1;bool intro_=true,focused_=true;
};
class MenuAudio {
public:
    explicit MenuAudio(System& system):system_(system),mixer_(system.audioRate()){}
    ~MenuAudio(){stop();}
    Error open(const Resources& resources,const std::filesystem::path& root,bool enabled);
    void enter();void stop();void update(const MenuMotion& motion);
private:
    System& system_;audio::Mixer mixer_;bool active_=false;
    std::array<std::shared_ptr<audio::Sound>,6> sounds_{};
    audio::SoundHandle entry_=0;
    std::array<audio::SoundHandle,5> handles_{};
    std::array<uint64_t,5> cycles_{};
    std::array<bool,5> playing_{};
};
class FrontendMedia {
public:
    Error open(const Resources& resources,const std::filesystem::path& root,bool enabled);
    bool button(Surface& screen,size_t index,int x,int y,uint64_t elapsed) const;
    bool buttonFit(Surface& screen,size_t index,SelectionRect bounds,uint64_t elapsed) const;
    void activate(uint64_t now);
    MenuMotion& motion(){return motion_;}
    static Error intro(System& system,const Resources& resources,const std::filesystem::path& root,bool& quit);
private:
    std::array<video::AviCinepak,5> buttons_;
    MenuMotion motion_;bool controlled_=false;
};
} // namespace reluva::boggle
