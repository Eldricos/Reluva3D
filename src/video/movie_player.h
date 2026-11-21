// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/types.h"
#include "image/image.h"
#include "video/bink.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A Bink movie played in time: its frames decoded as they fall due, its
// soundtrack (decoded whole) played alongside through the mixer. For an
// engine that shows a movie in its own loop, with its own drawing.

namespace reluva::video {

/** A Bink movie played in time, its sound through a mixer. */
class MoviePlayer {
public:
    /** A player with no movie; `mixer` (null: no sound) must outlive it. */
    explicit MoviePlayer(audio::Mixer *mixer) : _mixer(mixer) {}
    ~MoviePlayer() { stop(); }
    MoviePlayer(const MoviePlayer &) = delete;
    MoviePlayer &operator=(const MoviePlayer &) = delete;

    /**
     * Starts a movie at `nowMs`, with its sound unless `sound` is false.
     * Fails if the movie won't open; a soundtrack that won't decode is
     * left out, the reason in error().
     */
    Error open(std::vector<uint8_t> data, uint64_t nowMs, bool sound = true);
    /**
     * Decodes up to the frame due at `nowMs` (each builds on the last, so
     * none is skipped); true if the frame shown changed.
     */
    bool update(uint64_t nowMs);
    /** Stops its sound; the frame stays. */
    void stop();
    /** Pauses (the sound stopped) or goes on from the frame shown. */
    void pause(bool paused, uint64_t nowMs);
    /** Whether it's paused. */
    bool paused() const { return _paused; }

    /** Whether a movie opened. */
    bool loaded() const { return _movie != nullptr; }
    /** Whether every frame has been shown and the last one's time has gone by (true with no movie). */
    bool done(uint64_t nowMs) const;
    /** The frame shown: empty before the first. */
    const image::Image &frame() const { return _frame; }
    /** The movie, or null. */
    const bink::Movie *movie() const { return _movie.get(); }
    /** The movie's frames a second (0 with none). */
    double fps() const;
    /** Whether its soundtrack was decoded, to be played. */
    bool hasSound() const { return _soundtrack != nullptr; }
    /** The frame to decode next, and the movie's time in seconds (by the frames shown). */
    ///@{
    size_t next() const { return _next; }
    double seconds() const;
    ///@}
    /** A decoding error, if any frame had one. */
    const std::string &error() const { return _error; }

private:
    void playSound(double fromSeconds);

    audio::Mixer *_mixer;
    std::unique_ptr<bink::Movie> _movie;
    image::Image _frame;
    std::shared_ptr<audio::Sound> _soundtrack;
    audio::SoundHandle _sound = audio::kNoSound;
    size_t _next = 0;
    uint64_t _startMs = 0; // when frame 0 was due
    bool _paused = false;
    std::string _error;
};

} // namespace reluva::video
