// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/stream.h"
#include "common/types.h"
#include "video/cinepak.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::avi {

// A four-character code as AVI files store it ("cvid" is fourcc("cvid")).
constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

// Video for Windows files (.avi, docs/common/avi.md): the first video
// stream, with its frames decoded when it's Cinepak, and the first audio
// stream when it's PCM, read whole.
class Decoder {
public:
    // Reads the file's headers and where each frame is; the sound is read
    // here too. False if it isn't an AVI with a video stream. A video in
    // another codec loads (videoCodec() says which) but doesn't decode.
    bool load(std::unique_ptr<SeekableReadStream> stream);
    bool isLoaded() const { return stream_ != nullptr; }

    int width() const { return width_; }
    int height() const { return height_; }
    // The video stream's codec (its strh handler), e.g. fourcc("cvid").
    uint32_t videoCodec() const { return videoCodec_; }
    bool canDecode() const { return cinepak_ != nullptr; }
    int frameCount() const { return int(frames_.size()); }
    int currentFrame() const { return current_; }
    // Time per frame in microseconds (avih's; 1/24 s when it has none).
    uint32_t frameMicros() const { return frameMicros_; }
    bool endOfVideo() const { return current_ + 1 >= frameCount(); }

    // Decodes the next frame; false at the end, or if it can't be read or
    // decoded.
    bool decodeNextFrame();
    // A frame's bytes as stored (for another codec than Cinepak).
    bool readFrame(int index, std::vector<uint8_t> &out);
    // The picture so far (RGBA32, opaque); empty unless canDecode().
    const Surface &surface() const;

    // The soundtrack, as stored (PCM: little-endian when 16-bit, unsigned
    // when 8-bit, interleaved when stereo); empty when there's none, or
    // when it isn't PCM.
    bool hasAudio() const { return !audio_.empty(); }
    const std::vector<uint8_t> &audio() const { return audio_; }
    int audioRate() const { return audioRate_; }
    int audioChannels() const { return audioChannels_; }
    int audioBits() const { return audioBits_; }

private:
    struct Chunk {
        uint32_t offset, size;
    };
    bool readList(uint32_t end, int depth);

    std::unique_ptr<SeekableReadStream> stream_;
    std::unique_ptr<cinepak::Decoder> cinepak_;
    int width_ = 0, height_ = 0;
    uint32_t frameMicros_ = 41666;
    uint32_t videoCodec_ = 0;
    int streams_ = 0, videoStream_ = -1, audioStream_ = -1;
    int audioRate_ = 0, audioChannels_ = 0, audioBits_ = 0;
    std::vector<Chunk> frames_;
    std::vector<uint8_t> audio_;
    int current_ = -1;
};

} // namespace reluva::avi
