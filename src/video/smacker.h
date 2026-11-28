// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"
#include "common/stream.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::smacker {

/**
 * RAD Game Tools' Smacker video (SMK2 and SMK4), as used by many 1990s games.
 *
 * Frames are 8-bit with a palette that can change from frame to frame. The
 * picture is coded in 4x4 blocks (mono, full, skip and fill) with four
 * Huffman trees given in the header; audio tracks are raw or Huffman-packed
 * DPCM.
 */
class Decoder {
public:
    Decoder();
    ~Decoder();

    /**
     * Reads the header, frame table and Huffman trees; the decoder keeps the
     * stream, to read frames from. False if it isn't a Smacker file or they
     * don't read.
     */
    bool load(std::unique_ptr<SeekableReadStream> stream);
    /** Lets the file go; isLoaded() is false until the next load(). */
    void close();
    /** Whether a file is loaded. */
    bool isLoaded() const { return _stream != nullptr; }

    /** The picture's width in pixels. */
    int width() const { return _width; }
    /** The display height: twice the stored height for doubled/interlaced videos. */
    int height() const { return _displayHeight; }
    /** How many frames there are, and the last decoded (-1 before the first). */
    ///@{
    int frameCount() const { return _frameCount; }
    int currentFrame() const { return _curFrame; }
    ///@}
    /** Time per frame in microseconds. */
    uint32_t frameMicros() const { return _frameMicros; }
    /** Whether the last frame has been decoded. */
    bool endOfVideo() const { return _curFrame + 1 >= _frameCount; }

    /** Decodes the next frame. Returns false at the end or on a data error. */
    bool decodeNextFrame();
    /** The picture as displayed: CLUT8, in palette()'s colours. */
    const Surface &surface() const { return _surface; }
    /** 256 RGB entries, 8 bits per channel. */
    const uint8_t *palette() const { return _palette; }
    /** Whether the last frame decoded changed the palette. */
    bool paletteChanged() const { return _paletteChanged; }

    /**
     * Whether there's sound: the first audio track with a rate, if it isn't
     * Bink audio, and that track's rate and channels.
     */
    ///@{
    bool hasAudio() const { return _audioTrack >= 0; }
    int audioRate() const;
    bool audioStereo() const;
    ///@}
    /** Takes the 16-bit samples decoded so far (interleaved if stereo). */
    std::vector<int16_t> takeAudio();
    /**
     * The last frame's chunk of track `track` (0 to 6) when it isn't the
     * sound played: data a game puts there itself, such as Jedi Knight's
     * subtitle numbers. Empty if the frame had none.
     */
    const std::vector<uint8_t> &trackData(int track) const;

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
    std::vector<std::vector<uint8_t>> _trackData = std::vector<std::vector<uint8_t>>(7);
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
