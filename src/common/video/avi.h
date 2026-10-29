// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// AVI files (Microsoft's RIFF "AVI " form): the first video stream's frames
// and the first audio stream's PCM sound, and a player that decodes the
// video (Cinepak) as time goes by. See docs/formats/video.md.

#include "common/audio/sound.h"
#include "common/image.h"
#include "common/types.h"
#include "common/video/cinepak.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::video {

class AviFile {
public:
    Error open(std::vector<uint8_t> data);

    std::string videoCodec() const { return videoCodec_; } // "cvid"
    int width() const { return width_; }
    int height() const { return height_; }
    double framesPerSecond() const { return fps_; }
    size_t frameCount() const { return frames_.size(); }
    // A frame's data; empty for a frame that repeats the one before.
    void frame(size_t i, const uint8_t*& data, size_t& size) const;

    bool hasAudio() const { return audioFormat_ != 0; }
    // The audio stream as a sound, if it's PCM (8- or 16-bit).
    Error audio(audio::Sound& out) const;

private:
    struct Span {
        size_t offset, size;
    };
    Error readHeaders(size_t p, size_t end);
    void readMovie(size_t p, size_t end);

    std::vector<uint8_t> data_;
    std::string videoCodec_;
    int width_ = 0, height_ = 0;
    double fps_ = 0;
    int videoStream_ = -1, audioStream_ = -1;
    int audioFormat_ = 0, audioChannels_ = 0, audioRate_ = 0, audioBits_ = 0;
    std::vector<Span> frames_, audioChunks_;
};

// Plays an AVI's video: the picture for a time, decoding the frames up to
// it (going back means decoding again from the first).
class VideoPlayer {
public:
    Error open(std::vector<uint8_t> data);
    const AviFile& file() const { return file_; }
    double durationSeconds() const;
    // The picture at a time from the start; the last frame after the end.
    Error seek(double seconds);
    size_t frameShown() const { return shown_; }
    const image::Image& picture() const { return decoder_.picture(); }
    bool finished(double seconds) const { return seconds >= durationSeconds(); }

private:
    AviFile file_;
    CinepakDecoder decoder_;
    size_t next_ = 0;  // the next frame to decode
    size_t shown_ = 0; // the frame the picture is
};

} // namespace reluva::video
