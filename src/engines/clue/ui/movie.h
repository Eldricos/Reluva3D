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

/** One of the game's videos, decoded a frame at a time as the clock asks. */
class Movie {
public:
    /**
     * Reads an AVI file's headers and shows its first frame. Fails if it
     * isn't an AVI or its video isn't Cinepak.
     */
    Error open(std::vector<uint8_t> data);

    /** The picture's size, in pixels. */
    ///@{
    int width() const { return _decoder.width(); }
    int height() const { return _decoder.height(); }
    ///@}
    /** The video's codec as its four letters ("cvid"). */
    std::string videoCodec() const;
    /** How many frames it has, how many it shows a second, and how long it lasts (0 without a rate). */
    ///@{
    size_t frameCount() const { return size_t(_decoder.frameCount()); }
    double framesPerSecond() const;
    double durationSeconds() const;
    ///@}

    /** Whether it has a sound track. */
    bool hasAudio() const { return _decoder.hasAudio(); }
    /**
     * The sound track as 16-bit samples; fails unless it's 8- or 16-bit
     * PCM, mono or stereo.
     */
    Error audio(audio::Sound &out) const;

    /**
     * Shows the frame due `seconds` after the start (decoding the ones
     * before it; going back starts again from the first).
     */
    Error seek(double seconds);
    /** The frame shown, and its picture. */
    ///@{
    size_t frameShown() const { return _shown; }
    const image::Image &picture() const { return _picture; }
    ///@}
    /** Whether `seconds` is past its end. */
    bool finished(double seconds) const { return seconds >= durationSeconds(); }

private:
    Error restart();

    std::vector<uint8_t> _data;
    avi::Decoder _decoder;
    image::Image _picture;
    size_t _shown = 0;
};

} // namespace reluva::clue
