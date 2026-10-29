// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "engines/age/car.h"

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
    std::string horn, clutch; // the clutch's: played on changing gear
    float hornVolume = 1, clutchVolume = 1;
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
    // `gain` (0..1) and `pan` (-1 left .. 1 right): another car's, heard
    // from a way off.
    void update(float rpm, float throttle, float gain = 1, float pan = 0);
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

// The tyres' sounds on each kind of surface (aud/cardata/player/
// default_surfacedry.csv, _surfacewet.csv): a rolling sound louder and
// higher with speed, and skids by how much the tyres slide. A surface's
// number is its physics material's sound (materials.h).
struct SkidSample {
    std::string wave;
    float minSlip = 0, maxSlip = 1;
};
struct SurfaceSound {
    std::string wave; // NOSOUND for none
    float maxSpeed = 125;
    float minVolume = 0, maxVolume = 0, minPitch = 1, maxPitch = 1;
    float minSkidVolume = 0.5f, maxSkidVolume = 0.9f;
    std::vector<SkidSample> skids;
};
struct SurfaceSounds {
    int tunnel = 0; // the surface used in tunnels (not used yet)
    std::vector<SurfaceSound> surfaces;
};
Error readSurfaceSounds(std::string_view csv, SurfaceSounds& out);

// Knocks (aud/cardata/player/default_impacts.csv): for each kind of thing
// hit (WALL, LIGHT, SIGN...), samples by how hard.
struct ImpactSample {
    std::string wave;
    float minVolume = 1, maxVolume = 1, minForce = 0, maxForce = 1e9f, pitch = 1;
};
struct ImpactSounds {
    std::string name; // "WALL"
    int id = 0;
    std::vector<ImpactSample> samples;
};
Error readImpactSounds(std::string_view csv, std::vector<ImpactSounds>& out);

// The rest of a driven car's sounds: its horn, its clutch on changing
// gear, its tyres on what they're on and skidding, its knocks (as the
// WALL sounds) and its suspension bumping (aud/cardata/player/
// suspensionaudio.csv). See docs/engines/age-driving.md.
class CarSounds {
public:
    ~CarSounds() { stop(); }
    // The car's table and the shared ones; `wet` takes the wet roads'.
    Error start(audio::Mixer& mixer, const SearchSet& files, const std::string& car, bool wet);
    // Follows the car (its gear, wheels and impact); `horn` held sounds it.
    void update(float dt, const CarSim& car, bool horn);
    void stop();

private:
    using SoundPtr = std::shared_ptr<audio::Sound>;
    SoundPtr load(const std::string& wave) const;
    void loop(audio::SoundHandle& handle, const SoundPtr& sound, float volume, float pitch);

    audio::Mixer* mixer_ = nullptr;
    const SearchSet* files_ = nullptr;
    EngineSoundSetup car_;
    SoundPtr horn_, clutch_;
    audio::SoundHandle hornPlaying_ = audio::kNoSound;
    int lastGear_ = 1;

    SurfaceSounds surfaces_;
    std::vector<SoundPtr> rolling_;               // per surface
    std::vector<std::vector<SoundPtr>> skidding_; // per surface, per skid
    audio::SoundHandle rollPlaying_ = audio::kNoSound, skidPlaying_ = audio::kNoSound;
    int rollSurface_ = -1, skidSurface_ = -1, skidSample_ = -1;

    ImpactSounds wall_;
    std::vector<SoundPtr> wallSounds_;
    float impactWait_ = 0; // seconds before another knock is heard

    SoundPtr suspension_;
    float bumpMin_ = 2, bumpMax_ = 3, bumpMinVolume = 0.85f, bumpMaxVolume = 0.9f, bumpWait_ = 0;
};

} // namespace reluva::age
