// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/system.h"
#include "common/types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace reluva::audio {

// What a sound is, for the per-type volumes (a game's music, speech and
// effects settings).
enum class SoundType { kMusic, kSpeech, kEffects };
constexpr int kSoundTypes = 3;

// A playing sound, as returned by Mixer::play; 0 is never one.
using SoundHandle = uint32_t;
constexpr SoundHandle kNoSound = 0;

constexpr int kMaxVolume = 256; // volumes run 0 (silent) .. 256 (as recorded)
constexpr int kMaxPan = 127;    // pan runs -127 (left) .. 127 (right)

// A sound made as it plays (music from a synthesizer): asked on the audio
// thread for `frames` stereo frames at the mixer's rate, added into `out`
// (interleaved, full scale +-1).
class Stream {
public:
    virtual ~Stream() = default;
    virtual void read(float* out, int frames) = 0;
};

// Mixes any number of decoded sounds into the System's stereo output:
// each resampled (linearly) to the output rate, scaled by its own volume and
// pan and its type's volume, summed and clamped to 16 bits. Shared by every
// engine. Thread-safe: mix() runs on the audio thread while the engine plays
// and stops sounds; a finished sound's channel is freed by the next mix().
class Mixer {
public:
    explicit Mixer(int outputRate);

    // Starts a sound; returns kNoSound for an empty one. A looping sound
    // plays until stopped.
    SoundHandle play(std::shared_ptr<const Sound> sound, SoundType type, int volume = kMaxVolume, int pan = 0,
                     bool loop = false);
    void stop(SoundHandle handle);
    void stopType(SoundType type);
    void stopAll();

    bool isPlaying(SoundHandle handle) const;
    bool isTypePlaying(SoundType type) const;
    // How far a sound has played, in its own frames (0 if it isn't playing).
    double position(SoundHandle handle) const;

    void setVolume(SoundHandle handle, int volume);
    void setPan(SoundHandle handle, int pan);
    // Plays a sound faster (higher) or slower (lower) than recorded:
    // 1 as recorded, 2 an octave up. Clamped to 0.05..8.
    void setPitch(SoundHandle handle, float pitch);
    // A stream plays, at its type's volume, until removed (which waits for
    // a mix() under way to finish, so the stream may then be destroyed).
    void addStream(std::shared_ptr<Stream> stream, SoundType type);
    void removeStream(const Stream* stream);

    void setTypeVolume(SoundType type, int volume);
    // The whole mix to the left (-kMaxPan) or right (kMaxPan).
    void setBalance(int balance);
    int typeVolume(SoundType type) const;

    int outputRate() const { return _outputRate; }

    // Fills `frames` stereo frames (interleaved signed 16-bit) with every
    // playing sound; silence when none is. The System's audio callback.
    void mix(int16_t* out, int frames);

private:
    struct Channel {
        SoundHandle handle;
        std::shared_ptr<const Sound> sound;
        SoundType type;
        int volume, pan;
        bool loop;
        double pitch = 1;
        double position = 0; // in the sound's frames
        bool done = false;
    };
    Channel* find(SoundHandle handle);
    const Channel* find(SoundHandle handle) const;

    const int _outputRate;
    mutable std::mutex _mutex;
    std::vector<Channel> _channels;
    SoundHandle _nextHandle = 1;
    int _typeVolume[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    int _balance = 0;
    std::vector<int32_t> _accumulator; // mix()'s working buffer, stereo
    std::vector<std::pair<std::shared_ptr<Stream>, SoundType>> _streams;
    std::vector<float> _streamBuffer;
};

// Starts the System's audio output with the mixer as its source. The mixer
// must outlive the output: call sys.stopAudio() before destroying it.
Error startMixerOutput(System& sys, Mixer& mixer);

} // namespace reluva::audio
