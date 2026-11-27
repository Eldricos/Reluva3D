// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/options.h"

#include <cmath>
#include <cstdlib>
#include <system_error>
#include <string>

namespace reluva::stawayteam {

namespace {

// The program's section and keys (0x63b6e0, 0x6496a0, 0x64968c, 0x649678, 0x63b990).
const char *const kSection = "Options";
const char *const kScrollSpeed = "Scroll Speed";
const char *const kScrollAccel = "Scroll Acceleration";
const char *const kPauseWhenHit = "Pause Game When Hit";
const char *const kPauseHelp = "Show Pause When Hit Help";
const char *const kBrightness = "Screen Brightness";
// The sound's keys, as the program spells them (0x6456c4...).
const char *const kMasterVolume = "Master Voume";
const char *const kMasterMute = "Master Mute";
const char *const kEffectsVolume = "SFx Voume";
const char *const kEffectsMute = "SFx Mute";
const char *const kMusicVolume = "Music Volume";
const char *const kMusicMute = "Music Mute";
const char *const kVoiceovers = "Play Voiceovers";
const char *const kStereo = "Stereo Sounds";
const char *const kMixing = "Sounds Mixing Channels";
// The network's name (0x640e54, 0x640e60).
const char *const kNetSection = "Net Game";
const char *const kNetPlayerName = "Net Player Name";

int whole(const Config &file, const char *key, int fallback) {
    const std::string value = file.get(kSection, key);
    char *end = nullptr;
    const long n = std::strtol(value.c_str(), &end, 10);
    return !value.empty() && end && *end == '\0' ? int(n) : fallback;
}

bool flag(const Config &file, const char *key, bool fallback) {
    return whole(file, key, fallback ? 1 : 0) != 0;
}

// Hundredths of a decibel as the mixer's 0 to kMaxVolume.
double gain(int hundredths) {
    return std::pow(10.0, double(hundredths) / 2000.0);
}

float number(const Config &file, const char *key, float fallback) {
    const std::string value = file.get(kSection, key);
    if (value.empty())
        return fallback;
    char *end = nullptr;
    const float n = std::strtof(value.c_str(), &end);
    return end && *end == '\0' ? n : fallback;
}

} // namespace

void GameOptions::load(const std::filesystem::path &file) {
    _path = file;
    _loaded = !file.empty() && !_file.load(file);
    if (!_loaded)
        return;
    scrollSpeed = number(_file, kScrollSpeed, kScrollSpeedDefault);
    scrollAccel = number(_file, kScrollAccel, kScrollAccelDefault);
    pauseWhenHit = _file.get(kSection, kPauseWhenHit, "1") != "0";
    showPauseHelp = _file.get(kSection, kPauseHelp, "1") != "0";
    brightness = number(_file, kBrightness, kBrightnessMin);
    masterVolume = whole(_file, kMasterVolume, 0);
    masterMute = flag(_file, kMasterMute, false);
    effectsVolume = number(_file, kEffectsVolume, 1.0f);
    effectsMute = flag(_file, kEffectsMute, false);
    musicVolume = whole(_file, kMusicVolume, kMusicVolumeDefault);
    musicMute = flag(_file, kMusicMute, false);
    playVoiceovers = flag(_file, kVoiceovers, true);
    stereo = flag(_file, kStereo, true);
    mixingChannels = whole(_file, kMixing, kMixingFaster);
    netPlayerName = _file.get(kNetSection, kNetPlayerName);
    controls.load(_file);
}

void GameOptions::save() const {
    if (!_loaded)
        return;
    Config file = _file;
    file.set(kSection, kScrollSpeed, std::to_string(scrollSpeed));
    file.set(kSection, kScrollAccel, std::to_string(scrollAccel));
    file.set(kSection, kPauseWhenHit, pauseWhenHit ? "1" : "0");
    file.set(kSection, kPauseHelp, showPauseHelp ? "1" : "0");
    file.set(kSection, kBrightness, std::to_string(brightness));
    file.set(kSection, kMasterVolume, std::to_string(masterVolume));
    file.set(kSection, kMasterMute, masterMute ? "1" : "0");
    file.set(kSection, kEffectsVolume, std::to_string(effectsVolume));
    file.set(kSection, kEffectsMute, effectsMute ? "1" : "0");
    file.set(kSection, kMusicVolume, std::to_string(musicVolume));
    file.set(kSection, kMusicMute, musicMute ? "1" : "0");
    file.set(kSection, kVoiceovers, playVoiceovers ? "1" : "0");
    file.set(kSection, kStereo, stereo ? "1" : "0");
    file.set(kSection, kMixing, std::to_string(mixingChannels));
    file.set(kNetSection, kNetPlayerName, netPlayerName);
    controls.save(file);
    // The folder for the game's files may not be made yet.
    std::error_code ec;
    std::filesystem::create_directories(_path.parent_path(), ec);
    (void)file.save();
}

void GameOptions::applySound(audio::Mixer &mixer) const {
    // The game sets the master on its sound device, music on its music
    // player and effects as a scale (0x56f7a2, 0x56f930, 0x56f877); Reluva
    // has the master on both of the others, as its own master is the
    // player's in the launcher.
    const double master = gain(masterMute ? kVolumeMin : masterVolume);
    const double music = gain(musicMute ? kVolumeMin : musicVolume);
    const double effects = effectsMute ? kEffectsVolumeMuted : effectsVolume;
    mixer.setTypeVolume(audio::SoundType::kMusic, int(std::lround(master * music * audio::kMaxVolume)));
    mixer.setTypeVolume(audio::SoundType::kEffects, int(std::lround(master * effects * audio::kMaxVolume)));
    mixer.setTypeVolume(audio::SoundType::kSpeech, int(std::lround(master * effects * audio::kMaxVolume)));
    // MONO makes the sound device's output one channel (0x56c080, at 22050
    // a second in 16 bits, 0x56cb90). Sounds Mixing Channels is kept on
    // the device (0x504400) and nothing found reads it.
    mixer.setMono(!stereo);
}

} // namespace reluva::stawayteam
