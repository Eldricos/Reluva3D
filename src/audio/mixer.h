// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "audio/stream.h"
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
// The player's volumes (the master volume and each type's) go up to twice
// as loud; kMaxVolume, the default, plays sounds as they were recorded.
constexpr int kMaxBoostVolume = 2 * kMaxVolume;
constexpr int kMaxPan = 127; // pan runs -127 (left) .. 127 (right)

// Sound made as it plays (a program filling a buffer as it's heard): its
// producer adds samples, the mixer takes them. Unlike a QueuingStream
// (stream.h), which plays silence when it runs dry, the mixer waits for a
// little of it to be queued first, and again whenever it runs dry, so a
// producer a little late isn't heard as gaps (Mixer::playStream).
// Thread-safe.
class SoundStream {
public:
    SoundStream(int rate, int channels) : rate_(rate), channels_(channels) {}
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    // Adds interleaved samples (whole frames).
    void push(const int16_t *samples, size_t count);
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

// Mixes any number of decoded sounds and streams into the System's stereo output:
// each resampled (linearly) to the output rate, scaled by its own volume and
// pan, its type's volume and the master volume, summed and clamped to 16
// bits. Shared by every
// engine. Thread-safe: mix() runs on the audio thread while the engine plays
// and stops sounds; a finished sound's channel is freed by the next mix().
class Mixer {
public:
    explicit Mixer(int outputRate);

    // Starts a sound; returns kNoSound for an empty one. A looping sound
    // plays until stopped.
    SoundHandle play(std::shared_ptr<const Sound> sound, SoundType type, int volume = kMaxVolume, int pan = 0,
                     bool loop = false);
    // Starts a stream (stream.h), pulled as it plays; it ends when the
    // stream does, or is stopped. The mixer keeps a reference: the engine
    // may keep one too, to feed it.
    SoundHandle play(std::shared_ptr<Stream> stream, SoundType type, int volume = kMaxVolume, int pan = 0);
    // Starts a SoundStream. It waits until `prebufferMs` of it are queued,
    // and again whenever it runs dry.
    SoundHandle playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume = kMaxVolume, int pan = 0,
                           int prebufferMs = 100);
    void stop(SoundHandle handle);
    void stopType(SoundType type);
    void stopAll();

    bool isPlaying(SoundHandle handle) const;
    bool isTypePlaying(SoundType type) const;
    // How far a sound (or a stream) has played, in its own frames (0 if it
    // isn't playing).
    double position(SoundHandle handle) const;

    // A paused sound keeps its place and plays on from there when
    // unpaused; it still counts as playing.
    void setPaused(SoundHandle handle, bool paused);
    bool isPaused(SoundHandle handle) const;
    // Every sound held where it is (the game paused), apart from each
    // sound's own pause; mix() gives silence meanwhile.
    void setAllPaused(bool paused);
    bool allPaused() const;

    void setVolume(SoundHandle handle, int volume);
    void setPan(SoundHandle handle, int pan);
    // A type's volume (the player's music, speech and effects settings),
    // 0 .. kMaxBoostVolume.
    void setTypeVolume(SoundType type, int volume);
    int typeVolume(SoundType type) const;
    // Every sound's volume, 0 .. kMaxBoostVolume (kMaxVolume by default).
    void setMasterVolume(int volume);
    int masterVolume() const;

    int outputRate() const { return outputRate_; }

    // Fills `frames` stereo frames (interleaved signed 16-bit) with every
    // playing sound; silence when none is. The System's audio callback.
    void mix(int16_t *out, int frames);

private:
    struct Channel {
        SoundHandle handle;
        std::shared_ptr<const Sound> sound; // or
        std::shared_ptr<Stream> stream;
        SoundType type;
        int volume, pan;
        bool loop;
        bool paused = false;
        double position = 0; // in the sound's frames
        bool done = false;
        // A stream's: the frames either side of `position`, and the next.
        int16_t cur[2] = {0, 0}, next[2] = {0, 0};
        bool primed = false;
        // A SoundStream's (playStream), instead of a sound or a stream.
        std::shared_ptr<SoundStream> queue = nullptr;
        size_t prebuffer = 0; // frames it waits for
        bool waiting = true;  // it has run dry
    };
    void mixSound(Channel &c, int frames, int64_t gain);
    void mixStream(Channel &c, int frames, int64_t gain);
    void mixQueue(Channel &c, int frames, int64_t gain);
    static bool readFrame(Stream &s, int16_t frame[2]);
    Channel *find(SoundHandle handle);
    const Channel *find(SoundHandle handle) const;

    const int outputRate_;
    mutable std::mutex mutex_;
    std::vector<Channel> channels_;
    SoundHandle nextHandle_ = 1;
    int typeVolume_[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    int masterVolume_ = kMaxVolume;
    bool allPaused_ = false;
    std::vector<int32_t> accumulator_; // mix()'s working buffer, stereo
};

// Starts the System's audio output with the mixer as its source. The mixer
// must outlive the output: call sys.stopAudio() before destroying it.
Error startMixerOutput(System &sys, Mixer &mixer);

} // namespace reluva::audio
