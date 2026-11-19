// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace reluva::audio {

// Sound made as it plays, pulled by the Mixer (Mixer::play) a little at a
// time: a video's soundtrack fed frame by frame, music put together from
// pieces, a sound that loops a number of times. Sounds decoded whole are
// Sounds (sound.h) instead.
//
// read() runs on the audio thread, under the mixer's lock; a stream fed
// from the engine's thread meanwhile must guard its own state (as
// QueuingStream does).
class Stream {
public:
    virtual ~Stream() = default;

    // Fills `out` with up to `samples` signed 16-bit samples (interleaved
    // when stereo) and returns how many it wrote: fewer at the end.
    virtual int read(int16_t *out, int samples) = 0;
    virtual bool stereo() const = 0;
    virtual int rate() const = 0;
    // No more samples will come.
    virtual bool ended() const = 0;
};

// Raw PCM held in memory, played `loops` times (0: until stopped).
class RawStream : public Stream {
public:
    enum class Format {
        U8,    // unsigned 8-bit
        S8,    // signed 8-bit
        S16LE, // signed 16-bit, little-endian
        S16BE, // signed 16-bit, big-endian
    };
    RawStream(std::vector<uint8_t> data, Format format, int rate, bool stereo = false, int loops = 1);

    int read(int16_t *out, int samples) override;
    bool stereo() const override { return stereo_; }
    int rate() const override { return rate_; }
    bool ended() const override { return pos_ >= count_ && loopsLeft_ == 1; }

private:
    int16_t sampleAt(size_t i) const;

    std::vector<uint8_t> data_;
    Format format_;
    int rate_;
    bool stereo_;
    size_t count_, pos_ = 0;
    int loopsLeft_;
};

// A stream fed while it plays (a video decoder's sound, say). Until
// finish() it plays silence whenever it runs dry, so it keeps its place in
// the mixer; then it ends once what's queued has played. Safe to feed from
// the engine's thread while the mixer reads it.
class QueuingStream : public Stream {
public:
    QueuingStream(int rate, bool stereo) : rate_(rate), stereo_(stereo) {}

    // Adds samples (interleaved when stereo).
    void queue(const std::vector<int16_t> &samples);
    // Nothing more will be queued.
    void finish();
    // Samples queued and not yet played.
    size_t queued() const;

    int read(int16_t *out, int samples) override;
    bool stereo() const override { return stereo_; }
    int rate() const override { return rate_; }
    bool ended() const override;

private:
    int rate_;
    bool stereo_;
    bool finished_ = false;
    mutable std::mutex mutex_;
    std::deque<int16_t> queue_;
};

// A whole stream read into samples (interleaved when stereo); a stream
// that never ends stops after `maxSamples`.
std::vector<int16_t> readAll(Stream &stream, size_t maxSamples = size_t(1) << 28);

} // namespace reluva::audio
