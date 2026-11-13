// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Electronic Arts' audio codecs of the late 1990s, used in many of its games
// (docs/formats/nfs/bnk.md).

namespace reluva::audio {

// EA-XA ADPCM: CD-XA's prediction filters, 28 samples in a 15-byte frame
// per channel. The frame's first byte holds the filter (high nibble) and the
// shift (low nibble); then 14 bytes of 4-bit residuals, high nibble first.
struct EaXaChannel {
    int previous = 0, beforePrevious = 0;
};
constexpr size_t kEaXaFrameBytes = 15;
constexpr int kEaXaFrameSamples = 28;

// Decodes one frame into `count` (at most 28) samples, `stride` apart.
void decodeEaXaFrame(const uint8_t* frame, EaXaChannel& channel, int16_t* out, int count, int stride = 1);

// A mono stream of frames, `samples` long; fails if the data is short.
Error decodeEaXa(const uint8_t* data, size_t size, size_t samples, std::vector<int16_t>& out);

// A stereo data block of EA's SCxl streams (SCDl): u32 samples, each
// channel's starting history (s16 previous, s16 the one before), then per
// 28 samples a filter byte and a shift byte (left in the high nibbles) and
// 28 bytes of residuals (left high nibble, right low). Appends the
// interleaved samples to `out`.
Error decodeEaXaStereoBlock(const uint8_t* data, size_t size, std::vector<int16_t>& out);

// EA MicroTalk ("UTalk", "UTK"), a linear-predictive speech codec: mono,
// 22,050 Hz, 432 samples a frame. The stream starts with a few header bits
// and then holds frames back to back, bits read from each byte's lowest
// first.
class MicroTalkDecoder {
public:
    static constexpr int kFrameSamples = 432;

    MicroTalkDecoder(const uint8_t* data, size_t size);
    MicroTalkDecoder(const MicroTalkDecoder&) = delete; // points into its own state
    MicroTalkDecoder& operator=(const MicroTalkDecoder&) = delete;
    // The next frame's samples (unrounded). Past the end of the data the
    // decoder reads zeros, as the original does.
    const float* decodeFrame();
    // Whether the decoder has read past the end of its data.
    bool overrun() const { return overrun_; }

private:
    int readByte();
    int readBits(int count);
    void parseHeader();
    void decodeExcitation(bool multipulse, float* out, int stride);
    void synthesise(int offset, int blocks);

    const uint8_t* p_;
    const uint8_t* end_;
    bool overrun_ = false;
    unsigned bitsValue_ = 0;
    int bitsCount_ = 0;
    bool headerParsed_ = false, reducedBandwidth_ = false;
    int multipulseThreshold_ = 0;
    // The decoder's state in one block, in this order. The adaptive
    // codebook (the last frame's 324 samples) is read on into the frame
    // being decoded, which follows it; and a pitch lag longer than its
    // history reads into the arrays before it, which the decoded audio must
    // match the original's for.
    float state_[64 + 12 + 12 + 324 + kFrameSamples] = {};
    float* const fixedGains_ = state_;
    float* const reflection_ = state_ + 64;
    float* const history_ = state_ + 76;
    float* const adaptive_ = state_ + 88;
    float* const frame_ = state_ + 88 + 324;
};

// A mono MicroTalk stream, `samples` long, rounded to 16 bits.
Error decodeMicroTalk(const uint8_t* data, size_t size, size_t samples, std::vector<int16_t>& out);

} // namespace reluva::audio
