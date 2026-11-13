// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"
#include "common/stream.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::smacker {

// RAD Game Tools' Smacker video (SMK2 and SMK4), as used by many 1990s games.
//
// Frames are 8-bit with a palette that can change from frame to frame. The
// picture is coded in 4x4 blocks (mono, full, skip and fill) with four
// Huffman trees given in the header; audio tracks are raw or Huffman-packed
// DPCM.
class Decoder {
public:
    Decoder();
    ~Decoder();

    bool load(std::unique_ptr<SeekableReadStream> stream);
    void close();
    bool isLoaded() const { return stream_ != nullptr; }

    int width() const { return width_; }
    // The display height: twice the stored height for doubled/interlaced videos.
    int height() const { return displayHeight_; }
    int frameCount() const { return frameCount_; }
    int currentFrame() const { return curFrame_; }
    // Time per frame in microseconds.
    uint32_t frameMicros() const { return frameMicros_; }
    bool endOfVideo() const { return curFrame_ + 1 >= frameCount_; }

    // Decodes the next frame. Returns false at the end or on a data error.
    bool decodeNextFrame();
    const Surface& surface() const { return surface_; }
    // 256 RGB entries, 8 bits per channel.
    const uint8_t* palette() const { return palette_; }
    bool paletteChanged() const { return paletteChanged_; }

    // The first audio track with data, if any.
    bool hasAudio() const { return audioTrack_ >= 0; }
    int audioRate() const;
    bool audioStereo() const;
    // Takes the 16-bit samples decoded so far (interleaved if stereo).
    std::vector<int16_t> takeAudio();

private:
    struct BigTree;
    struct BitReader;

    bool readTrees(const std::vector<uint8_t>& data);
    void decodePalette(const uint8_t* data, uint32_t size);
    void decodeAudio(const uint8_t* data, uint32_t size);
    bool decodeVideo(const uint8_t* data, uint32_t size);

    std::unique_ptr<SeekableReadStream> stream_;
    bool smk4_ = false;
    int width_ = 0, height_ = 0, displayHeight_ = 0;
    int frameCount_ = 0;
    int curFrame_ = -1;
    uint32_t frameMicros_ = 0;
    uint32_t flags_ = 0;
    uint32_t audioRates_[7] = {};
    int audioTrack_ = -1;
    std::vector<uint32_t> frameSizes_;
    std::vector<uint8_t> frameTypes_;
    int64_t nextFramePos_ = 0;

    std::unique_ptr<BigTree> mmap_, mclr_, full_, type_;
    Surface frame_;   // as stored
    Surface surface_; // as displayed
    uint8_t palette_[768] = {};
    bool paletteChanged_ = false;
    std::vector<int16_t> audio_;
};

} // namespace reluva::smacker
