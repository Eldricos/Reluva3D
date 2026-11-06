// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/audio/mixer.h"
#include "engines/boggle/resources.h"
#include <array>
#include "engines/boggle/classic/options.h"

namespace reluva::boggle {
enum class Cue { Shake, ClickDown, ClickUp, Writing, Seven, Eight, Tick, GameOver, Count };
class ClassicAudio {
public:
    explicit ClassicAudio(System& system) : system_(system), mixer_(system.audioRate()) {}
    ~ClassicAudio();
    Error open(const Resources& resources, bool enabled, int music = musicNone, uint32_t seed = 0x1997);
    void play(Cue cue);
    void beginRound(bool loopShake = true);
    void stopShake();
    void countdown(uint64_t remaining,size_t player=0);
    void recorded(size_t letters);
private:
    System& system_;
    audio::Mixer mixer_;
    bool active_ = false;
    audio::SoundHandle shake_ = 0, musicHandle_ = 0;
    std::shared_ptr<audio::Sound> music_;
    std::array<std::shared_ptr<audio::Sound>,size_t(Cue::Count)> sounds_{};
    std::array<audio::SoundHandle,size_t(Cue::Count)> handles_{};
    std::array<std::array<bool,11>,4> countdown_{};
};
enum class Transition { None, Opening, Closing };
class ClassicTransition {
public:
    void begin(Transition type, uint64_t now) { type_ = type; started_ = now; }
    Transition type() const { return type_; }
    // First 44 frames open/roll; last 11 close, at observed 100 ms cadence.
    size_t frame(uint64_t now) const;
    bool finish(uint64_t now);
private:
    Transition type_ = Transition::None;
    uint64_t started_ = 0;
};
} // namespace reluva::boggle
