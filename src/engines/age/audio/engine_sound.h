// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "audio/mixer.h"
#include "engines/age/game/car.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

/**
 * A car's engine sound (`aud/cardata/player/<car>.csv`): looped recordings,
 * each faded in and out over a range of RPM and pitched up with it. See
 * docs/engines/age/games/mm2/systems/car-sounds.md.
 */
struct EngineSample {
    std::string wave; // aud/aud22/engines/<wave>.22k.wav
    float minVolume = 0;
    float maxVolume = 1;
    // RPM
    float fadeInStart = 0;
    float fadeInEnd = 0;
    float fadeOutStart = 1e9f;
    float fadeOutEnd = 1e9f;
    float minPitch = 1;
    float maxPitch = 1;
    // RPM
    float pitchStart = 0;
    float pitchEnd = 1;
};

/** A car's sound table: its horn, its clutch and its engine samples. */
struct EngineSoundSetup {
    std::string horn;
    std::string clutch; // played on changing gear
    float hornVolume = 1;
    float clutchVolume = 1;
    std::vector<EngineSample> samples;
};

/** Reads a car's sound table. Fails on one too short, or without engine samples. */
Error readEngineSound(std::string_view csv, EngineSoundSetup &out);

/**
 * A sample's volume (0..1) and pitch (1 as recorded) at an RPM: faded in
 * and out over its ranges, louder towards its maximum as the throttle
 * opens.
 */
void engineSampleMix(const EngineSample &s, float rpm, float throttle, float &volume, float &pitch);

/** The samples playing through the mixer. */
class EngineSound {
public:
    ~EngineSound() { stop(); }
    /**
     * Loads and starts them (silent until update()); missing recordings
     * are left out. Fails if the car's table won't read.
     */
    Error start(audio::Mixer &mixer, const SearchSet &files, const std::string &car);
    /**
     * Sets each sample's volume and pitch for the engine's RPM and throttle.
     * `gain` (0..1) and `pan` (-1 left .. 1 right): another car's, heard
     * from a way off.
     */
    void update(float rpm, float throttle, float gain = 1, float pan = 0);
    /** Stops the samples. */
    void stop();

private:
    audio::Mixer *_mixer = nullptr;
    EngineSoundSetup _setup;
    std::vector<audio::SoundHandle> _playing; // one per sample, kNoSound if missing
};

/**
 * A police siren (aud/cardata/opponent/policesiren.csv, or the player's
 * aud/cardata/player/sfpolicesiren.csv and londonpolicesiren.csv): looped
 * recordings, each played for a while before handing over to another, as
 * the table's "play time, next index" lines say.
 */
struct SirenSample {
    std::string wave;
    float volume = 1;
    std::vector<std::pair<float, int>> next; // seconds to play, then the sample to go on to
};
/** Reads a siren table. Fails without a sample. */
Error readSirenTable(std::string_view csv, std::vector<SirenSample> &out);

/** A siren playing through the mixer, its samples taking turns as its table says. */
class SirenSound {
public:
    ~SirenSound() { stop(); }
    /**
     * Reads the table and its recordings (a missing one is silent). Fails
     * if the table won't read.
     */
    Error load(audio::Mixer &mixer, const SearchSet &files, const std::string &table);
    /** Plays at `volume` (0..1); 0 stops it, and it starts afresh next time. */
    void update(float dt, float volume);
    /** Stops it; it starts afresh with the first sample. */
    void stop();

private:
    audio::Mixer *_mixer = nullptr;
    std::vector<SirenSample> _samples;
    std::vector<std::shared_ptr<audio::Sound>> _sounds;
    audio::SoundHandle _playing = audio::kNoSound;
    int _current = -1;
    float _left = 0;   // seconds before the next sample
    size_t _turns = 0; // which of a sample's ways on to take, in turn
};

/** A skid's recording, played while the tyres' slip is in its range (0..1). */
struct SkidSample {
    std::string wave;
    float minSlip = 0;
    float maxSlip = 1;
};
/** A surface's sounds: rolling, and its skids. */
struct SurfaceSound {
    std::string wave; // the rolling sound; NOSOUND for none
    float maxSpeed = 125;
    float minVolume = 0;
    float maxVolume = 0;
    float minPitch = 1;
    float maxPitch = 1;
    float minSkidVolume = 0.5f;
    float maxSkidVolume = 0.9f;
    std::vector<SkidSample> skids;
};
/**
 * The tyres' sounds on each kind of surface (aud/cardata/player/
 * default_surfacedry.csv, _surfacewet.csv): a rolling sound louder and
 * higher with speed, and skids by how much the tyres slide. A surface's
 * number is its physics material's sound (materials.h).
 */
struct SurfaceSounds {
    int tunnel = 0; // the surface used in tunnels (not used yet)
    std::vector<SurfaceSound> surfaces;
};
/** Reads a surfaces table. Fails without a surface. */
Error readSurfaceSounds(std::string_view csv, SurfaceSounds &out);

/** A knock's recording, played for a knock within its range of force. */
struct ImpactSample {
    std::string wave;
    float minVolume = 1;
    float maxVolume = 1;
    float minForce = 0;
    float maxForce = 1e9f;
    float pitch = 1;
};
/**
 * Knocks (aud/cardata/player/default_impacts.csv): for each kind of thing
 * hit (WALL, LIGHT, SIGN...), samples by how hard.
 */
struct ImpactSounds {
    std::string name; // "WALL"
    int id = 0;
    std::vector<ImpactSample> samples;
};
/** Reads an impacts table. Fails without a kind of thing hit. */
Error readImpactSounds(std::string_view csv, std::vector<ImpactSounds> &out);

/**
 * The rest of a driven car's sounds: its horn, its clutch on changing
 * gear, its tyres on what they're on and skidding, its knocks (as the
 * WALL sounds) and its suspension bumping (aud/cardata/player/
 * suspensionaudio.csv). See docs/engines/age/games/mm2/systems/car-sounds.md.
 */
class CarSounds {
public:
    ~CarSounds() { stop(); }
    /**
     * The car's table and the shared ones; `wet` takes the wet roads'.
     * @return The first table that wouldn't read, though the rest are used.
     */
    Error start(audio::Mixer &mixer, const SearchSet &files, const std::string &car, bool wet);
    /** Follows the car (its gear, wheels and impact); `horn` held sounds it. */
    void update(float dt, const CarSim &car, bool horn);
    /** Stops the sounds that loop (the horn, rolling, skidding). */
    void stop();

private:
    using SoundPtr = std::shared_ptr<audio::Sound>;
    SoundPtr load(const std::string &wave) const;
    void loop(audio::SoundHandle &handle, const SoundPtr &sound, float volume, float pitch);

    audio::Mixer *_mixer = nullptr;
    const SearchSet *_files = nullptr;
    EngineSoundSetup _car;
    SoundPtr _horn;
    SoundPtr _clutch;
    audio::SoundHandle _hornPlaying = audio::kNoSound;
    int _lastGear = 1;

    SurfaceSounds _surfaces;
    std::vector<SoundPtr> _rolling;               // per surface
    std::vector<std::vector<SoundPtr>> _skidding; // per surface, per skid
    audio::SoundHandle _rollPlaying = audio::kNoSound;
    audio::SoundHandle _skidPlaying = audio::kNoSound;
    int _rollSurface = -1;
    int _skidSurface = -1;
    int _skidSample = -1;

    ImpactSounds _wall;
    std::vector<SoundPtr> _wallSounds;
    float _impactWait = 0; // seconds before another knock is heard

    SoundPtr _suspension;
    float _bumpMin = 2;
    float _bumpMax = 3;
    float _bumpMinVolume = 0.85f;
    float _bumpMaxVolume = 0.9f;
    float _bumpWait = 0;
};

} // namespace reluva::age
