// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/image.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::video {

// A movie played from its file's bytes: its pictures in turn and its whole
// soundtrack. Decoded by FFmpeg's libraries when Reluva is built with them
// (RELUVA_USE_FFMPEG); without them nothing opens and a game shows what it
// would show with its movies turned off.
class Movie {
public:
    // Whether this build can play movies at all.
    static bool supported();
    // Null, with `error` set, if the movie can't be played.
    static std::unique_ptr<Movie> open(std::vector<uint8_t> data, std::string& error);
    ~Movie();
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;

    int width() const;
    int height() const;
    double duration() const; // seconds

    // The picture showing `seconds` in, as RGBA, decoding on as far as
    // that (time only goes forward); false once the pictures have run out.
    bool frameAt(double seconds, image::Image& out);
    // The soundtrack, decoded to 16 bits (no samples if it has none).
    const audio::Sound& sound() const;

    struct Impl;

private:
    explicit Movie(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace reluva::video
