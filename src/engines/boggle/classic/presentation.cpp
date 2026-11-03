// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/presentation.h"
#include <algorithm>

namespace reluva::boggle {
ClassicAudio::~ClassicAudio() { if (active_) system_.stopAudio(); }
Error ClassicAudio::open(const Resources& resources, bool enabled, int music, uint32_t seed) {
    if (music < musicRandom || music >= int(classicMusicFiles.size()))
        return Error::fail("Unsupported Classic music track");
    music = chooseClassicMusic(music,seed);
    if (music >= 0) {
        auto sound = std::make_shared<audio::Sound>();
        if (auto err = resources.sound(std::string("Waves/")+classicMusicFiles[size_t(music)]+".wav",*sound)) return err;
        music_ = std::move(sound);
    }
    if (!enabled && !music_) return Error::success();
    const char* names[] = {"Shake", "ClkDown", "ClkUp", "Writing", "Seven", "Eight", "Tick", "GameOver"};
    for (size_t i=0;enabled && i<sounds_.size();++i) {
        auto sound = std::make_shared<audio::Sound>();
        if (auto err = resources.sound(std::string("Waves/")+names[i]+".wav",*sound)) return err;
        sounds_[i] = std::move(sound);
    }
    // A missing device is non-fatal; the game and timer remain usable silently.
    active_ = !audio::startMixerOutput(system_,mixer_);
    if (!active_) system_.stopAudio(); // clear even a failed backend's callback
    return Error::success();
}
void ClassicAudio::play(Cue cue) {
    if (!active_ || size_t(cue) >= sounds_.size() || !sounds_[size_t(cue)]) return;
    if (cue == Cue::Shake) {
        stopShake(); shake_ = mixer_.play(sounds_[size_t(cue)],audio::SoundType::Effects,256,0,true);
    } else {
        auto& handle = handles_[size_t(cue)]; mixer_.stop(handle);
        handle = mixer_.play(sounds_[size_t(cue)],audio::SoundType::Effects);
    }
}
void ClassicAudio::beginRound(bool loopShake) {
    mixer_.stopType(audio::SoundType::Effects); shake_=0; handles_.fill(0); countdown_.fill(false);
    if (active_ && music_ && !mixer_.isPlaying(musicHandle_))
        musicHandle_ = mixer_.play(music_,audio::SoundType::Music,256,0,true);
    if (active_ && sounds_[size_t(Cue::Shake)]) shake_ = mixer_.play(sounds_[size_t(Cue::Shake)],audio::SoundType::Effects,256,0,loopShake);
}
void ClassicAudio::stopShake() { if (shake_) mixer_.stop(shake_); shake_=0; }
void ClassicAudio::countdown(uint64_t remaining) {
    const uint64_t second = remaining/1000;
    if (second > 10 || countdown_[size_t(second)]) return;
    countdown_[size_t(second)] = true;
    play(second ? Cue::Tick : Cue::GameOver);
}
void ClassicAudio::recorded(size_t letters) { play(letters >= 8 ? Cue::Eight : letters == 7 ? Cue::Seven : Cue::Writing); }
size_t ClassicTransition::frame(uint64_t now) const {
    const uint64_t elapsed = now >= started_ ? now-started_ : 0;
    return type_ == Transition::Closing ? 44 + size_t(std::min<uint64_t>(10,elapsed/100)) : size_t(std::min<uint64_t>(43,elapsed/100));
}
bool ClassicTransition::finish(uint64_t now) {
    if (type_ == Transition::None || now < started_) return false;
    const uint64_t duration = type_ == Transition::Opening ? 4400 : 1100;
    if (now-started_ < duration) return false;
    type_ = Transition::None; return true;
}
} // namespace reluva::boggle
