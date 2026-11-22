// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::mdec {

/**
 * Pictures coded as the PlayStation's MDEC frames, version 2
 * (docs/common/video/mdec.md): 16x16 macroblocks in columns, each Cr, Cb
 * and four luma blocks of MPEG-1-like intra coding. Their size isn't in
 * the frame; the container gives it.
 */
class Decoder {
public:
    /**
     * Decodes one frame: its 8-byte header (a u16 count, 0x3800, the
     * quantiser scale, the version) and its bits, into a picture
     * `width` x `height` (multiples of 16). Fails on another version, a
     * code that isn't one, or a block that runs past its 64 coefficients.
     */
    Error decode(const uint8_t *frame, size_t size, int width, int height);

    /** The picture's size, in pixels. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    ///@}

    /** The quantiser scale of the frame decoded last. */
    int quantiserScale() const { return _scale; }

    /** The picture as RGBA8888. */
    void toSurface(Surface &out) const;

    /**
     * Plane access, for tests: luma (0) at full size, Cb (1) and Cr (2) at
     * half each way, each sample as the transform left it, clamped: luma
     * 0-255, chroma -128-127.
     */
    const std::vector<int16_t> &plane(int index) const { return _planes[index]; }

private:
    bool decodeBlock(float coefficients[64]);
    void putBlock(const float coefficients[64], int plane, int x, int y);

    int _width = 0;
    int _height = 0;
    int _scale = 0;
    std::vector<int16_t> _planes[3];
    // The bits: 16-bit little-endian words, each read from its top bit.
    const uint8_t *_bits = nullptr;
    size_t _bitCount = 0;
    size_t _bitPos = 0;
    uint32_t peek(int n) const;
    uint32_t read(int n);
    int readSigned(int n);
};

} // namespace reluva::mdec
