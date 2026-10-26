// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

// Bink video (revisions d to i; not b), decoding a BinkFile's video
// packets into YUV 4:2:0 planes and RGBA. See docs/formats/bink.md for
// where its workings come from: the published description of the
// format, with FFmpeg's decoder as the reference for what that leaves out
// (its tables are in third_party/ffmpeg_bink).

namespace reluva::video {

class BinkVideoDecoder {
public:
    Error init(int width, int height, char revision, uint32_t flags);

    // Decodes the next frame (frames depend on the one before).
    Error decode(const uint8_t* data, size_t size);

    int width() const { return width_; }
    int height() const { return height_; }
    // The last frame decoded as RGBA (BT.601, studio range), rows top to
    // bottom, width() x height(); alpha from the alpha plane if the video
    // has one, else 255.
    void toRgba(std::vector<uint8_t>& out) const;

    // A plane of the last frame: 0 Y, 1 U, 2 V, 3 alpha; stride() bytes per row.
    const uint8_t* plane(int i) const { return planes_[cur_][size_t(i)].data(); }
    int stride(int i) const { return strides_[size_t(i)]; }

private:
    struct Tree {
        int table = 0;     // which of the 16 Huffman trees
        uint8_t syms[16]{}; // what each of its leaves stands for
    };
    // One kind of value a plane's blocks draw from, decoded a row of
    // blocks ahead.
    struct Bundle {
        int lengthBits = 0; // bits in a run's count
        Tree tree;
        std::vector<int16_t> data;
        size_t decoded = 0, read = 0;
        bool finished = false; // a run of 0 ended it for the plane
    };
    class Bits;

    Error decodePlane(Bits& bits, int plane, bool chroma);
    Error readBundles(Bits& bits);
    int value(int bundle);
    Error dctCoefficients(Bits& bits, int32_t block[64], int& quantiser);
    void dequantise(int32_t block[64], const int32_t quant[64]) const;
    void residue(Bits& bits, int16_t block[64], int masks);

    int width_ = 0, height_ = 0;
    char revision_ = 0;
    bool alpha_ = false, swapPlanes_ = false, first_ = true;
    // Two frames (the one being decoded and the one before), four planes each.
    std::array<std::array<std::vector<uint8_t>, 4>, 2> planes_;
    std::array<int, 4> strides_{}, rows_{};
    int cur_ = 0;
    std::array<Bundle, 9> bundles_;
    std::array<Tree, 16> colourHigh_; // trees for the colours' high nibble, by the last one
    int lastColourHigh_ = 0;
    // The last block's coefficients read, as scan positions.
    int coefIndex_[64]{};
    int coefCount_ = 0;
};

} // namespace reluva::video
