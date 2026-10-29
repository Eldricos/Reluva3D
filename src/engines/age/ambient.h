// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "engines/age/vecmath.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

// One sound of an ambience (a row of aud/ambient/<name>.csv).
struct AmbientSample {
    std::string name; // aud/aud22/Amb3D/<name>.22k.wav...
    float volume = 1; // 0..1
    int type = 0;     // 0 loops; 1 and 2 are one-shots; 3 played when something happens (a scream)
    float low = 0;    // a one-shot's seconds between plays: from `low`
    float high = 0;   // to `high`
    bool active = true;
};

// aud/ambient/<name>.csv: a sound heard near a set of points (the city's
// audio_pathsets/<name>.pathset's, written out again).
struct Ambience {
    float minDistance = 0; // heard in full this near a point
    float maxDistance = 0; // and not at all this far
    int priority = 0;      // "3D priority"
    int area = 0;          // "Audible area": not used yet
    std::vector<AmbientSample> samples;
    std::vector<Vec3> points; // VECTORPOINTS
};
Error parseAmbience(const std::string& text, Ambience& out);
// aud/ambient/<city>ambientcontainer.csv: the city's ambiences, by name.
std::vector<std::string> parseAmbientContainer(const std::string& text);

// How loud an ambience is `distance` from its nearest point: in full to
// its least distance, falling evenly to nothing at its greatest.
float ambientGain(const Ambience& a, float distance);

// A city's ambiences (its aud/ambient/<city>ambientcontainer.csv) round
// the listener: each one's loops and its one-shots now and then, as loud
// as its nearest point is near. See docs/engines/age-traffic.md.
class AmbientSounds {
public:
    AmbientSounds() = default;
    ~AmbientSounds() { clear(); }
    AmbientSounds(const AmbientSounds&) = delete;
    AmbientSounds& operator=(const AmbientSounds&) = delete;

    Error load(const SearchSet& files, const std::string& map, audio::Mixer* mixer, uint32_t seed = 1);
    void clear();
    void update(float dt, const Vec3& listener);
    // All of them louder or quieter (the Audio Options' ambient: 0 off).
    void setGain(float gain) { gain_ = gain; }

    const std::vector<Ambience>& ambiences() const { return ambiences_; }
    // How many sounds are playing (for tests).
    size_t playing() const;

private:
    struct Voice {
        size_t ambience = 0;
        const AmbientSample* sample = nullptr;
        std::shared_ptr<const audio::Sound> sound;
        audio::SoundHandle handle = audio::kNoSound;
        float wait = 0; // a one-shot's seconds to its next play
    };
    std::vector<Ambience> ambiences_;
    std::vector<Voice> voices_;
    audio::Mixer* mixer_ = nullptr;
    std::mt19937 random_;
    float gain_ = 1;
};

} // namespace reluva::age
