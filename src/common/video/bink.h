// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::bink {

// A Bink movie (RAD Game Tools' video format, .bik; docs/formats/bink.md),
// revisions f to i: its frames decoded one after another to YUV 4:2:0
// planes (RGBA on request), with its first audio track's samples.
class Movie {
public:
    // Reads the header and frame index (the file is kept).
    Error open(std::vector<uint8_t> file);

    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t frameCount() const { return uint32_t(frames_.size()); }
    // Frames a second, as a fraction.
    uint32_t rateNumerator() const { return rateNum_; }
    uint32_t rateDenominator() const { return rateDen_; }
    char revision() const { return revision_; }

    // The first audio track: none if 0.
    int audioRate() const { return audio_.empty() ? 0 : audio_[0].rate; }
    int audioChannels() const { return audio_.empty() ? 0 : audio_[0].outChannels; }

    // The next frame: its picture into the planes and its sound (the first
    // track's, interleaved) appended to `sound` if given. False at the end.
    Error nextFrame(bool& more, std::vector<int16_t>* sound = nullptr);
    int frameNumber() const { return next_ - 1; } // the last one decoded
    // The first audio track whole (interleaved), read ahead of the frames
    // (which then skip it).
    Error decodeAllAudio(std::vector<int16_t>& sound);

    // The last frame decoded's planes: Y full size, U and V half (alpha 3);
    // their pitches.
    const std::vector<uint8_t>& decodedPlane(int i) const { return old_[size_t(i)]; }
    int pitch(int i) const { return i == 0 || i == 3 ? lumaPitch_ : chromaPitch_; }
    // As RGBA, width x height, rows top first (BT.601, studio range).
    void toRgba(std::vector<uint8_t>& out) const;

    // --- Internals (public for the decoder's helpers) -----------------------
    struct Bits {
        const uint8_t* data = nullptr;
        size_t size = 0, pos = 0; // in bits
        uint32_t get(int n);
        uint32_t bit() { return get(1); }
        void skip(size_t n) { pos += n; }
        void align32() { pos = (pos + 31) & ~size_t(31); }
        bool done() const { return pos >= size; }
    };
    // A code tree chosen (0-15) and the symbols it gives, reordered.
    struct Tree {
        int index = 0;
        std::array<uint8_t, 16> symbols{};
    };

private:
    struct Frame {
        uint32_t offset = 0, size = 0;
        bool key = false;
    };
    struct Audio {
        int rate = 0, outChannels = 1, channels = 1;
        bool dct = false;
        int frameLength = 0, overlap = 0, blockSize = 0, bandCount = 0;
        float root = 0;
        std::vector<int> bands;
        bool first = true;
        std::vector<float> coeffs[2];
        std::vector<int16_t> previous;
    };
    // A bundle: one kind of value for the blocks, decoded a row at a time.
    struct Bundle {
        std::vector<int16_t> data;
        size_t decoded = 0, read = 0; // positions in data
        bool finished = false;        // no more for this plane
        int countBits[2] = {};        // luma, chroma
        int bits = 0;
        Tree tree;
    };
    enum Source { BlockTypes, SubBlockTypes, Colors, Pattern, XOff, YOff, IntraDC, InterDC, Run, SourceCount };

    void decodePlane(Bits& b, int plane, bool chroma);
    void readTree(Bits& b, Tree& t);
    int symbol(Bits& b, const Tree& t);
    uint32_t bundleCount(Bits& b, Bundle& bundle);
    void readBundle(Bits& b, Source s);
    int value(Source s);
    void readDCTCoefficients(Bits& b, int32_t block[64], bool intra);
    void readResidue(Bits& b, int32_t block[64], int masks);
    void decodeAudio(Audio& a, Bits& b, std::vector<int16_t>* sound);
    void readAudioCoefficients(Audio& a, Bits& b, float* coeffs);

    std::vector<uint8_t> file_;
    char revision_ = 0;
    int width_ = 0, height_ = 0;
    uint32_t rateNum_ = 0, rateDen_ = 1;
    bool alpha_ = false, swapChroma_ = false;
    std::vector<Frame> frames_;
    std::vector<Audio> audio_;
    int next_ = 0;
    int lumaPitch_ = 0, chromaPitch_ = 0, lumaRows_ = 0, chromaRows_ = 0;
    std::array<std::vector<uint8_t>, 4> cur_, old_;
    std::array<Bundle, SourceCount> bundles_;
    std::array<Tree, 16> colorHigh_;
    int lastColorHigh_ = 0;
};

} // namespace reluva::bink
