// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/system.h"
#include "common/types.h"

#include <cstdint>
#include <deque>
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

// Sound made as it plays (an emulated sound card's buffers, say), pulled
// by the mixer on the audio thread.
class Stream {
public:
    virtual ~Stream() = default;
    // Fills `frames` stereo frames (interleaved signed 16-bit) at `rate`
    // frames a second. Runs on the audio thread.
    virtual void read(int16_t* out, int frames, int rate) = 0;
};

// Sounds played one after another with nothing between them: a queue
// the engine fills (ahead of time) while the mixer reads it as a stream.
// Each sound's last frame leads into the next one's first, so music in
// pieces plays as one. Each sound is tagged with a number of the engine's,
// for telling which is playing.
class SoundQueue : public Stream {
public:
    void push(std::shared_ptr<const Sound> sound, int tag);
    // The tag of the sound playing (-1: none left), and how many sounds are
    // queued with it.
    int playing() const;
    size_t queued() const;
    void read(int16_t* out, int frames, int rate) override;

private:
    struct Entry {
        std::shared_ptr<const Sound> sound;
        int tag;
    };
    mutable std::mutex mutex_;
    std::deque<Entry> queue_;
    double position_ = 0; // in the first sound's frames
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
    // Starts a stream, which plays until stopped.
    SoundHandle playStream(std::shared_ptr<Stream> stream, SoundType type, int volume = kMaxVolume);
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
        std::shared_ptr<Stream> stream; // instead of a sound
        SoundType type;
        int volume, pan;
        bool loop;
        double position = 0; // in the sound's frames
        bool done = false;
    };
    Channel* find(SoundHandle handle);
    const Channel* find(SoundHandle handle) const;

    const int outputRate_;
    mutable std::mutex mutex_;
    std::vector<Channel> channels_;
    SoundHandle nextHandle_ = 1;
    int typeVolume_[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    std::vector<int32_t> accumulator_; // mix()'s working buffer, stereo
    std::vector<int16_t> streamBuffer_; // a stream's frames, before mixing
};

// Starts the System's audio output with the mixer as its source. The mixer
// must outlive the output: call sys.stopAudio() before destroying it.
Error startMixerOutput(System& sys, Mixer& mixer);

} // namespace reluva::audio
