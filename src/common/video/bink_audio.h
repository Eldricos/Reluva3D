// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <complex>
#include <cstdint>
#include <vector>

// Bink audio, the DCT form (the one Murder on the Orient Express's movies
// use), decoding a BinkFile's audio packets to 16-bit samples. See
// docs/formats/bink.md: the MultimediaWiki's "Bink Audio", with FFmpeg's
// decoder as the reference for what that leaves open.

namespace reluva::video {

class BinkAudioDecoder {
public:
    Error init(int sampleRate, int channels, bool dct);

    // Decodes a packet, appending its samples (interleaved when stereo).
    Error decode(const uint8_t* data, size_t size, std::vector<int16_t>& out);

    int sampleRate() const { return rate_; }
    int channels() const { return channels_; }

private:
    void inverseDct(std::vector<float>& coeffs, std::vector<float>& out);

    int rate_ = 0, channels_ = 0;
    int frameLength_ = 0, overlap_ = 0, bandCount_ = 0;
    float root_ = 0;
    float quant_[96]{};
    std::vector<int> bands_;
    std::vector<std::vector<float>> previous_; // each channel's last overlap
    bool first_ = true;
    // The transform's tables: twiddles for the DCT, roots for the FFT.
    std::vector<std::complex<float>> twiddle_, roots_;
};

} // namespace reluva::video
