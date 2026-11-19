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

/** A four-character code as AVI files store it ("cvid" is fourcc("cvid")). */
constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

/**
 * Video for Windows files (.avi, docs/common/video/avi.md): the first video
 * stream, with its frames decoded when it's Cinepak, and the first audio
 * stream when it's PCM, read whole.
 */
class Decoder {
public:
    /**
     * Reads the file's headers and where each frame is; the sound is read
     * here too. False if it isn't an AVI with a video stream. A video in
     * another codec loads (videoCodec() says which) but doesn't decode.
     * The decoder keeps the stream, to read frames from.
     */
    bool load(std::unique_ptr<SeekableReadStream> stream);
    /** Whether load() succeeded. */
    bool isLoaded() const { return _stream != nullptr; }

    /** The picture's size in pixels. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    ///@}
    /** The video stream's codec (its strh handler), e.g. fourcc("cvid"). */
    uint32_t videoCodec() const { return _videoCodec; }
    /** Whether its frames decode here (Cinepak). */
    bool canDecode() const { return _cinepak != nullptr; }
    /** How many frames there are, and the last decoded (-1 before the first). */
    ///@{
    int frameCount() const { return int(_frames.size()); }
    int currentFrame() const { return _current; }
    ///@}
    /** Time per frame in microseconds (avih's; 1/24 s when it has none). */
    uint32_t frameMicros() const { return _frameMicros; }
    /** Whether the last frame has been decoded. */
    bool endOfVideo() const { return _current + 1 >= frameCount(); }

    /**
     * Decodes the next frame; false at the end, if it can't decode, or if
     * the frame can't be read. A frame that's damaged leaves what it
     * couldn't draw as it was.
     */
    bool decodeNextFrame();
    /** A frame's bytes as stored (for another codec than Cinepak); false if it can't be read. */
    bool readFrame(int index, std::vector<uint8_t> &out);
    /** The picture so far (RGBA32, opaque); empty unless canDecode(). */
    const Surface &surface() const;

    /**
     * The soundtrack, as stored (PCM: little-endian when 16-bit, unsigned
     * when 8-bit, interleaved when stereo), and its rate, channels and bits
     * a sample; empty when there's none, or when it isn't PCM.
     */
    ///@{
    bool hasAudio() const { return !_audio.empty(); }
    const std::vector<uint8_t> &audio() const { return _audio; }
    int audioRate() const { return _audioRate; }
    int audioChannels() const { return _audioChannels; }
    int audioBits() const { return _audioBits; }
    ///@}

private:
    struct Chunk {
        uint32_t offset;
        uint32_t size;
    };
    bool readList(uint32_t end, int depth);

    std::unique_ptr<SeekableReadStream> _stream;
    std::unique_ptr<cinepak::Decoder> _cinepak;
    int _width = 0;
    int _height = 0;
    uint32_t _frameMicros = 41666;
    uint32_t _videoCodec = 0;
    int _streams = 0;
    int _videoStream = -1;
    int _audioStream = -1;
    int _audioRate = 0;
    int _audioChannels = 0;
    int _audioBits = 0;
    std::vector<Chunk> _frames;
    std::vector<uint8_t> _audio;
    int _current = -1;
};

} // namespace reluva::avi
