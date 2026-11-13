// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"
#include "engines/nfs/formats/bnk.h"

#include <cstdint>
#include <vector>

namespace reluva::nfs {

// Electronic Arts' Madcow video (.mad), as docs/engines/nfs/games/nfs3/formats/mad.md
// describes: MPEG-1-like intra blocks and whole-block motion copies, in
// YUV 4:2:0.
class MadDecoder {
public:
    // The arithmetic: the reference one FFmpeg's decoder shares, at 8 bits
    // (the default), or nfs3.exe's own (mad.md, The game's decoder).
    enum class Arithmetic { kGame, kReference };
    void setArithmetic(Arithmetic a) { _arithmetic = a; }
    // Decodes one frame chunk (MADk, MADm or MADe, its 8-byte chunk header
    // included). The picture is kept until the next.
    Error decode(const uint8_t *chunk, size_t size);
    int width() const { return _width; }
    int height() const { return _height; }
    // The picture as RGBA8888.
    void toSurface(Surface &out) const;
    // The picture at twice its size as nfs3.exe shows it on a 16-bit
    // screen (mad.md, The game's display step): its 7-bit conversion and
    // two-pixel dither, then one of its three ways of doubling, chosen by
    // the processor's clock. Lines the mode doesn't write stay black. The
    // 5:6:5 result is widened to RGBA8888 by repeating its top bits.
    enum class Display { kRepeat = 1, kAverage = 2, kInterpolate = 3 };
    void toGameSurface(Surface &out, Display mode) const;
    // Plane access, for tests and tools: stride, then the planes (0 Y, 1 U,
    // 2 V); chroma is half size each way. With the game's arithmetic the
    // samples are its 7-bit ones (0-127), 64 the middle.
    int stride(int plane) const { return plane ? _stride / 2 : _stride; }
    const uint8_t *plane(int plane) const { return _current[plane].data(); }

private:
    void decodeMacroblock(bool inter, int mbX, int mbY);
    bool decodeIntraBlock(int16_t block[64]);
    void copyBlock(int index, int mbX, int mbY, int mvX, int mvY, int add);
    void putBlock(int index, int mbX, int mbY, int16_t block[64]);

    Arithmetic _arithmetic = Arithmetic::kReference;
    bool gameIntraBlock(int32_t block[64]);
    void gamePutBlock(int index, int mbX, int mbY, int32_t block[64]);
    int64_t _gameQuant[64] = {};
    int _width = 0, _height = 0, _stride = 0, _rows = 0; // rows_: luma rows held, a multiple of 16
    std::vector<uint8_t> _current[3], _previous[3];
    bool _havePrevious = false;
    int _quant[64] = {};
    // The bitstream: 16-bit little-endian words, each read from its top bit.
    const uint8_t *_bits = nullptr;
    size_t _bitCount = 0, _bitPos = 0;
    bool _damaged = false;
    uint32_t peek(int n) const;
    uint32_t read(int n);
    int readSigned(int n);
    int readMotion();
};

// A movie file: frame chunks interleaved with an SCHl audio stream.
class MadMovie {
public:
    struct Frame {
        uint32_t offset = 0, size = 0; // the chunk, header included
        char kind = 'k';               // k (intra), m or e (inter; an e frame isn't referred to)
    };

    Error open(std::vector<uint8_t> data);
    const std::vector<Frame> &frames() const { return _frames; }
    // The first frame's size and frame duration (1/65536 milliseconds).
    int width() const { return _width; }
    int height() const { return _height; }
    uint32_t frameDuration() const { return _duration; }
    bool hasAudio() const { return _haveAudio; }
    const SoundHeader &audioHeader() const { return _audio; }
    Error decodeAudio(audio::Sound &out) const;
    Error decodeFrame(MadDecoder &decoder, size_t index) const;

private:
    std::vector<uint8_t> _data;
    std::vector<Frame> _frames;
    std::vector<std::pair<uint32_t, uint32_t>> _audioBlocks; // each SCDl's payload: offset, size
    SoundHeader _audio;
    bool _haveAudio = false;
    int _width = 0, _height = 0;
    uint32_t _duration = 0;
};

} // namespace reluva::nfs
