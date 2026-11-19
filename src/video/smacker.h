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
    bool isLoaded() const { return _stream != nullptr; }

    int width() const { return _width; }
    // The display height: twice the stored height for doubled/interlaced videos.
    int height() const { return _displayHeight; }
    int frameCount() const { return _frameCount; }
    int currentFrame() const { return _curFrame; }
    // Time per frame in microseconds.
    uint32_t frameMicros() const { return _frameMicros; }
    bool endOfVideo() const { return _curFrame + 1 >= _frameCount; }

    // Decodes the next frame. Returns false at the end or on a data error.
    bool decodeNextFrame();
    const Surface &surface() const { return _surface; }
    // 256 RGB entries, 8 bits per channel.
    const uint8_t *palette() const { return _palette; }
    bool paletteChanged() const { return _paletteChanged; }

    // The first audio track with data, if any.
    bool hasAudio() const { return _audioTrack >= 0; }
    int audioRate() const;
    bool audioStereo() const;
    // Takes the 16-bit samples decoded so far (interleaved if stereo).
    std::vector<int16_t> takeAudio();

private:
    struct BigTree;
    struct BitReader;

    bool readTrees(const std::vector<uint8_t> &data);
    void decodePalette(const uint8_t *data, uint32_t size);
    void decodeAudio(const uint8_t *data, uint32_t size);
    bool decodeVideo(const uint8_t *data, uint32_t size);

    std::unique_ptr<SeekableReadStream> _stream;
    bool _smk4 = false;
    int _width = 0;
    int _height = 0;
    int _displayHeight = 0;
    int _frameCount = 0;
    int _curFrame = -1;
    uint32_t _frameMicros = 0;
    uint32_t _flags = 0;
    uint32_t _audioRates[7] = {};
    int _audioTrack = -1;
    std::vector<uint32_t> _frameSizes;
    std::vector<uint8_t> _frameTypes;
    int64_t _nextFramePos = 0;

    std::unique_ptr<BigTree> _mmap;
    std::unique_ptr<BigTree> _mclr;
    std::unique_ptr<BigTree> _full;
    std::unique_ptr<BigTree> _type;
    Surface _frame;   // as stored
    Surface _surface; // as displayed
    uint8_t _palette[768] = {};
    bool _paletteChanged = false;
    std::vector<int16_t> _audio;
};

} // namespace reluva::smacker
