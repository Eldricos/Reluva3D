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

} // namespace reluva::age
