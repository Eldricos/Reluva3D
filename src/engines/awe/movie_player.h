// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/types.h"
#include "common/image.h"
#include "common/video/bink_file.h"
#include "common/video/bink_video.h"

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
    explicit MoviePlayer(audio::Mixer* mixer) : mixer_(mixer) {}
    ~MoviePlayer() { stop(); }
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Starts a movie at `nowMs`, with its sound unless `sound` is false.
    Error open(std::vector<uint8_t> data, uint64_t nowMs, bool sound = true);
    // Decodes up to the frame due at `nowMs` (each builds on the last, so
    // none is skipped); true if the frame shown changed.
    bool update(uint64_t nowMs);
    // Stops its sound; the frame stays.
    void stop();
    // Pauses (the sound stopped) or goes on from the frame shown.
    void pause(bool paused, uint64_t nowMs);
    bool paused() const { return paused_; }

    bool loaded() const { return file_ != nullptr; }
    // Every frame shown and the time of the last gone by.
    bool done(uint64_t nowMs) const;
    const image::Image& frame() const { return frame_; }
    const video::BinkFile* file() const { return file_.get(); }
    bool hasSound() const { return soundtrack_ != nullptr; }
    // The frame to decode next, and the movie's time in seconds (by the
    // frames shown).
    size_t next() const { return next_; }
    double seconds() const;
    // A decoding error, if any frame had one.
    const std::string& error() const { return error_; }

private:
    void playSound(double fromSeconds);

    audio::Mixer* mixer_;
    std::unique_ptr<video::BinkFile> file_;
    std::unique_ptr<video::BinkVideoDecoder> video_;
    image::Image frame_;
    std::shared_ptr<audio::Sound> soundtrack_;
    audio::SoundHandle sound_ = audio::kNoSound;
    size_t next_ = 0;
    uint64_t startMs_ = 0; // when frame 0 was due
    bool paused_ = false;
    std::string error_;
};

} // namespace reluva::awe
