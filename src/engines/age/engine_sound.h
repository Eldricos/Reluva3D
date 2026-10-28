// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// A car's engine sound (aud/cardata/player/<car>.csv): looped recordings,
// each faded in and out over a range of RPM and pitched up with it. See
// docs/engines/age-driving.md.
struct EngineSample {
    std::string wave; // aud/aud22/engines/<wave>.22k.wav
    float minVolume = 0, maxVolume = 1;
    float fadeInStart = 0, fadeInEnd = 0, fadeOutStart = 1e9f, fadeOutEnd = 1e9f; // RPM
    float minPitch = 1, maxPitch = 1;
    float pitchStart = 0, pitchEnd = 1; // RPM
};

struct EngineSoundSetup {
    std::string horn;
    std::vector<EngineSample> samples;
};

Error readEngineSound(std::string_view csv, EngineSoundSetup& out);

// A sample's volume (0..1) and pitch (1 as recorded) at an RPM: faded in
// and out over its ranges, louder towards its maximum as the throttle
// opens.
void engineSampleMix(const EngineSample& s, float rpm, float throttle, float& volume, float& pitch);

// The samples playing through the mixer.
class EngineSound {
public:
    ~EngineSound() { stop(); }
    // Loads and starts them (silent until update()); missing recordings
    // are left out.
    Error start(audio::Mixer& mixer, const SearchSet& files, const std::string& car);
    void update(float rpm, float throttle);
    void stop();

private:
    audio::Mixer* mixer_ = nullptr;
    EngineSoundSetup setup_;
    std::vector<audio::SoundHandle> playing_; // one per sample, kNoSound if missing
};

// A police siren (aud/cardata/opponent/policesiren.csv, or the player's
// aud/cardata/player/sfpolicesiren.csv and londonpolicesiren.csv): looped
// recordings, each played for a while before handing over to another, as
// the table's "play time, next index" lines say.
struct SirenSample {
    std::string wave;
    float volume = 1;
    std::vector<std::pair<float, int>> next; // seconds to play, then the sample to go on to
};
Error readSirenTable(std::string_view csv, std::vector<SirenSample>& out);

class SirenSound {
public:
    ~SirenSound() { stop(); }
    Error load(audio::Mixer& mixer, const SearchSet& files, const std::string& table);
    // Plays at `volume` (0..1); 0 stops it, and it starts afresh next time.
    void update(float dt, float volume);
    void stop();

private:
    audio::Mixer* mixer_ = nullptr;
    std::vector<SirenSample> samples_;
    std::vector<std::shared_ptr<audio::Sound>> sounds_;
    audio::SoundHandle playing_ = audio::kNoSound;
    int current_ = -1;
    float left_ = 0;   // seconds before the next sample
    size_t turns_ = 0; // which of a sample's ways on to take, in turn
};

} // namespace reluva::age
