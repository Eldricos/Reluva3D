// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Clue's videos (the murder scenes, the intro, the accusations): Cinepak
// AVIs with 16-bit PCM sound, played by the shared AVI decoder
// (video/avi.h) and shown by the clock, as Clue.exe's DirectShow
// graph shows them. See docs/engines/clue/formats/videos.md.

#pragma once

#include "audio/sound.h"
#include "image/image.h"
#include "common/types.h"
#include "video/avi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::clue {

class Movie {
public:
    // Read an AVI file's headers and show its first frame. Fails if it
    // isn't an AVI or its video isn't Cinepak.
    Error open(std::vector<uint8_t> data);

    int width() const { return decoder_.width(); }
    int height() const { return decoder_.height(); }
    std::string videoCodec() const; // "cvid"
    size_t frameCount() const { return size_t(decoder_.frameCount()); }
    double framesPerSecond() const;
    double durationSeconds() const;

    bool hasAudio() const { return decoder_.hasAudio(); }
    // The sound track as samples (8- or 16-bit PCM, mono or stereo).
    Error audio(audio::Sound &out) const;

    // Show the frame due `seconds` after the start (decoding the ones
    // before it; going back starts again from the first).
    Error seek(double seconds);
    size_t frameShown() const { return shown_; }
    const image::Image &picture() const { return picture_; }
    bool finished(double seconds) const { return seconds >= durationSeconds(); }

private:
    Error restart();

    std::vector<uint8_t> data_;
    avi::Decoder decoder_;
    image::Image picture_;
    size_t shown_ = 0;
};

} // namespace reluva::clue
