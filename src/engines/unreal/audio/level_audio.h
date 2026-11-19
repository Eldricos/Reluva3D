// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A played level's sounds through the mixer (the Maps page's): what its
// scripts play, heard from where the actor is, the actors' ambient sounds,
// the music and the dialogue's voices, at the game's volume settings. See
// docs/engines/unreal_vm.md.

#include "audio/mixer.h"
#include "engines/unreal/game/level_play.h"

#include <map>
#include <memory>
#include <vector>

namespace reluva::unreal {

class LevelAudio {
public:
    LevelAudio(audio::Mixer &mixer, const PackageSet &packages);
    ~LevelAudio();

    // A frame: the level's new sounds started, every sound's loudness and
    // side worked out again from where it is and where the listener (the
    // camera) is and faces, the ambient sounds and music brought up to
    // date. `seconds`: since the last frame.
    void update(LevelPlay &play, const Vec3f &listener, const Rotator &facing, float seconds);
    // Every sound stopped, and what it knew of the level forgotten (a level
    // stopped or another started). The decoded sounds are kept.
    void reset();

    size_t playingCount() const { return _playing.size() + _ambient.size() + (_music ? 1 : 0); }
    // What's playing, a line each (its kind and sound), then how many
    // one-off sounds have started and the last few.
    std::vector<std::string> describe() const;

private:
    std::shared_ptr<const audio::Sound> load(const ObjectRef &ref);
    // As the game's audio driver: heard from `at` as far as `radius`,
    // how loud (0..1, before the volume settings: `volume` x (1 -
    // distance / radius), in hundredths, of the driver's 127) and to which
    // side (-127..127).
    float loudness(const Vec3f &at, float radius, float volume) const;
    int panOf(const Vec3f &at, float radius) const;
    // A sound's importance: its volume x (1 - distance / radius), at
    // least 0.0001 of it. When all channels are taken, a new sound takes
    // the least important's if that's no more important than it.
    float priority(const Vec3f &at, float radius, float volume) const;
    bool makeRoom(float priority);
    // A stream's loudness (at the music volume, twice the others').
    float streamLoudness(float volume) const;
    // The pitch Doppler gives a sound at `at` moving at `velocity` (the
    // listener's taken off), mixed flat (`flat`: the driver's own) or
    // positional (its 3D provider's).
    float doppler(const Vec3f &at, const Vec3f &velocity, bool flat) const;
    Vec3f listenerSpace(const Vec3f &at) const;
    int positionalPan(const Vec3f &at) const;
    float behind(const Vec3f &at) const;
    static float reverbTail(const audio::Sound &sound);

    LevelPlay::Room _room;        // where the listener is
    audio::Mixer::Reverb _reverb; // that room's reverb on a positional sound
    Vec3f _viewerVelocity{};
    float _dopplerSpeed = 35000;

    audio::Mixer &_mixer;
    const PackageSet &_packages;
    std::map<std::pair<const Package *, int>, std::shared_ptr<const audio::Sound>> _sounds; // null: unreadable

    Vec3f _listener{};
    Vec3f _right{0, 1, 0};
    Vec3f _forward{1, 0, 0};
    Vec3f _up{0, 0, 1};
    bool _stereo = true;
    bool _reverse = false;
    bool _surround = false;
    int _channels = 16;
    float _musicSetting = 1; // MusicVolume / 255

    struct Playing {
        audio::SoundHandle handle = audio::kNoSound;
        VmObject *actor = nullptr; // followed while it lasts
        int slot = 0;
        Vec3f at{};
        float radius = 0;
        float volume = 1;
        bool everywhere = false; // a voice: not placed
        bool noOverride = false;
        bool filtered = false; // mixed flat (2D), not positional (3D)
        bool speech = false;   // a voice: no Doppler
        float obstruction = 0;
        float pitch = 1;
        float tail = 0; // its reverb's tail once it has ended (seconds)
        ObjectRef sound;
    };
    std::vector<Playing> _playing;
    std::vector<std::string> _recent; // the last one-off sounds started, oldest first
    size_t _started = 0;
    struct Ambient {
        audio::SoundHandle handle = audio::kNoSound;
        std::pair<const Package *, int> sound{};
        bool music = false;
        bool streamed = false;
        // fadeRate: a second (music: 1/3 in, -1/3 out)
        float volume = 0;
        float fade = 1;
        float fadeRate = 0;
        float radius = 0;
        Vec3f at{};
    };
    std::map<VmObject *, Ambient> _ambient;
    std::vector<LevelPlay::Ambient> _ambientNow;

    audio::SoundHandle _music = audio::kNoSound;
    VmObject *_musicSound = nullptr;
    float _musicVolume = 1;
    bool _musicStreamed = true;
    struct Fade {
        audio::SoundHandle handle;
        // its loudness (0-1) and seconds to go
        float volume;
        float left;
    };
    std::vector<Fade> _fading;
};

} // namespace reluva::unreal
