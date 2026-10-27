// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/system.h"
#include "common/types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace reluva::audio {

// What a sound is, for the per-type volumes (a game's music, speech and
// effects settings).
enum class SoundType { Music, Speech, Effects };
constexpr int kSoundTypes = 3;

// A playing sound, as returned by Mixer::play; 0 is never one.
using SoundHandle = uint32_t;
constexpr SoundHandle kNoSound = 0;

constexpr int kMaxVolume = 256; // volumes run 0 (silent) .. 256 (as recorded)
constexpr int kMaxPan = 127;    // pan runs -127 (left) .. 127 (right)

// Sound made as it plays (a program filling a buffer as it's heard): its
// producer adds samples, the mixer takes them. Thread-safe.
class SoundStream {
public:
    SoundStream(int rate, int channels) : rate_(rate), channels_(channels) {}
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    // Adds interleaved samples (whole frames).
    void push(const int16_t* samples, size_t count);
    // No more will come: the stream ends once what's queued has played.
    void end();
    // Frames queued.
    size_t queued() const;

private:
    friend class Mixer;
    const int rate_, channels_;
    mutable std::mutex mutex_;
    std::vector<int16_t> samples_; // queued, oldest first
    size_t read_ = 0;              // samples_ before this have been taken
    bool ended_ = false;
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
    // Starts a stream. It waits until `prebufferMs` of it are queued, and
    // again whenever it runs dry, so a producer a little late isn't heard
    // as gaps.
    SoundHandle playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume = kMaxVolume, int pan = 0,
                           int prebufferMs = 100);
    void stop(SoundHandle handle);
    void stopType(SoundType type);
    void stopAll();

    bool isPlaying(SoundHandle handle) const;
    bool isTypePlaying(SoundType type) const;
    // How far a sound has played, in its own frames (0 if it isn't playing).
    double position(SoundHandle handle) const;

    void setVolume(SoundHandle handle, int volume);
    void setPan(SoundHandle handle, int pan);
    void setTypeVolume(SoundType type, int volume);
    int typeVolume(SoundType type) const;

    int outputRate() const { return outputRate_; }

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
        double position = 0; // in the sound's frames
        bool done = false;
        std::shared_ptr<SoundStream> stream; // instead of a sound
        size_t prebuffer = 0;                // frames a stream waits for
        bool waiting = true;                 // a stream that has run dry
    };
    void mixStream(Channel& c, int frames, int64_t gainL, int64_t gainR);
    Channel* find(SoundHandle handle);
    const Channel* find(SoundHandle handle) const;

    const int outputRate_;
    mutable std::mutex mutex_;
    std::vector<Channel> channels_;
    SoundHandle nextHandle_ = 1;
    int typeVolume_[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    std::vector<int32_t> accumulator_; // mix()'s working buffer, stereo
};

// Starts the System's audio output with the mixer as its source. The mixer
// must outlive the output: call sys.stopAudio() before destroying it.
Error startMixerOutput(System& sys, Mixer& mixer);

} // namespace reluva::audio
