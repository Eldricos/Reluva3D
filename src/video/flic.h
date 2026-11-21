// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/stream.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace reluva::flic {

/**
 * Autodesk Animator's FLIC animations: FLI (320x200, 1/70 s ticks) and FLC
 * (any size, milliseconds), 8-bit with a palette that frames may change.
 * Each frame is a list of chunks: palettes (64 or 256 levels), a whole
 * picture (raw or run-length), or the changes since the last (by bytes, or
 * by words in FLC) (docs/common/video/flic.md).
 */
class Decoder {
public:
    /**
     * Reads the 128-byte header; the decoder keeps the stream, to read
     * frames from. False if it isn't a FLIC file or its size is unusable.
     */
    bool load(std::unique_ptr<SeekableReadStream> stream);
    /**
     * Takes a header without a stream, for a game that hands over each
     * frame itself (decodeFrame()). `data` is the header's 128 bytes.
     */
    bool loadHeader(const uint8_t *data, size_t size);

    /** The picture's size in pixels. */
    ///@{
    int width() const { return _surface.w; }
    int height() const { return _surface.h; }
    ///@}
    /** How many frames the header says there are (not counting a loop frame after them). */
    int frameCount() const { return _frameCount; }
    /** The last frame decoded, from 0; -1 before the first. */
    int currentFrame() const { return _curFrame; }
    /** Time per frame in microseconds. */
    uint32_t frameMicros() const { return _frameMicros; }
    /** Whether the last frame has been decoded. */
    bool endOfVideo() const { return _curFrame + 1 >= _frameCount; }

    /** Reads and decodes the next frame from the stream. False at the end or on a data error. */
    bool decodeNextFrame();
    /** Decodes one frame chunk (`0xF1FA` and what's in it) onto the picture. False on a data error. */
    bool decodeFrame(const uint8_t *data, size_t size);
    /** The picture: CLUT8, in palette()'s colours. */
    const Surface &surface() const { return _surface; }
    /** 256 RGB entries, 8 bits per channel. */
    const uint8_t *palette() const { return _palette; }
    /** Whether the last frame decoded changed the palette. */
    bool paletteChanged() const { return _paletteChanged; }

    /** A frame chunk's type, and a FLIC file's two kinds. */
    ///@{
    static constexpr uint16_t kFrameType = 0xF1FA;
    static constexpr uint16_t kFli = 0xAF11;
    static constexpr uint16_t kFlc = 0xAF12;
    ///@}

private:
    bool decodeColor(const uint8_t *data, size_t size, int shift);
    bool decodeByteRun(const uint8_t *data, size_t size);
    bool decodeDeltaFli(const uint8_t *data, size_t size);
    bool decodeDeltaFlc(const uint8_t *data, size_t size);

    std::unique_ptr<SeekableReadStream> _stream;
    int _frameCount = 0;
    int _curFrame = -1;
    uint32_t _frameMicros = 0;
    int64_t _nextFramePos = 0;
    Surface _surface;
    uint8_t _palette[768] = {};
    bool _paletteChanged = false;
};

} // namespace reluva::flic
