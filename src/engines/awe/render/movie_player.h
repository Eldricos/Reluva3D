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
// soundtrack (decoded whole) played alongside through the mixer. The
// Movies page's player, and the game's "movie" verb's.

namespace reluva::awe {

class MoviePlayer {
public:
    explicit MoviePlayer(audio::Mixer *mixer) : _mixer(mixer) {}
    ~MoviePlayer() { stop(); }
    MoviePlayer(const MoviePlayer &) = delete;
    MoviePlayer &operator=(const MoviePlayer &) = delete;

    // Starts a movie at `nowMs`, with its sound unless `sound` is false.
    Error open(std::vector<uint8_t> data, uint64_t nowMs, bool sound = true);
    // Decodes up to the frame due at `nowMs` (each builds on the last, so
    // none is skipped); true if the frame shown changed.
    bool update(uint64_t nowMs);
    // Stops its sound; the frame stays.
    void stop();
    // Pauses (the sound stopped) or goes on from the frame shown.
    void pause(bool paused, uint64_t nowMs);
    bool paused() const { return _paused; }

    bool loaded() const { return _movie != nullptr; }
    // Every frame shown and the time of the last gone by.
    bool done(uint64_t nowMs) const;
    const image::Image &frame() const { return _frame; }
    const bink::Movie *movie() const { return _movie.get(); }
    double fps() const; // the movie's frames a second (0 with none)
    bool hasSound() const { return _soundtrack != nullptr; }
    // The frame to decode next, and the movie's time in seconds (by the
    // frames shown).
    size_t next() const { return _next; }
    double seconds() const;
    // A decoding error, if any frame had one.
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

} // namespace reluva::awe
