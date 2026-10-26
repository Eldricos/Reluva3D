// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

// Bink files (.bik), RAD Game Tools' video container, as described in
// "Description of the Bink File Format" (Mike Melanson; on the
// MultimediaWiki as "Bink Container"): a header, the audio tracks'
// settings, an index of frames, then the frames, each holding a packet
// per audio track and the video packet. Decoding the packets is separate.

namespace reluva::video {

struct BinkAudioTrack {
    uint32_t id = 0;
    int sampleRate = 0;
    int channels = 1;
    bool dct = false; // the DCT form of Bink Audio, else the FFT form
    uint16_t flags = 0;
};

class BinkFile {
public:
    // Reads the header and the frame index; the file's bytes are kept.
    Error open(std::vector<uint8_t> data);

    char revision() const { return revision_; } // 'b'...'i'
    int width() const { return width_; }
    int height() const { return height_; }
    // Frames per second, as a fraction.
    uint32_t fpsNumerator() const { return fpsNum_; }
    uint32_t fpsDenominator() const { return fpsDen_; }
    double fps() const { return fpsDen_ ? double(fpsNum_) / fpsDen_ : 0; }
    uint32_t flags() const { return flags_; }
    bool hasAlpha() const { return flags_ & (1u << 20); }
    bool grayscale() const { return flags_ & (1u << 17); }
    uint32_t largestFrame() const { return largestFrame_; }
    const std::vector<BinkAudioTrack>& audioTracks() const { return tracks_; }
    size_t frameCount() const { return frames_.size(); }

    struct Packet {
        const uint8_t* data = nullptr;
        size_t size = 0;
    };
    struct Frame {
        bool keyframe = false;
        // One per audio track (empty if the frame has none for it), with
        // how many bytes of 16-bit output it decodes to (the format
        // description calls it a sample count; over a whole file it adds
        // up to duration x rate x channels x 2).
        std::vector<Packet> audio;
        std::vector<uint32_t> audioBytes;
        Packet video;
    };
    Error frame(size_t index, Frame& out) const;

private:
    std::vector<uint8_t> data_;
    char revision_ = 0;
    int width_ = 0, height_ = 0;
    uint32_t fpsNum_ = 0, fpsDen_ = 1, flags_ = 0, largestFrame_ = 0;
    std::vector<BinkAudioTrack> tracks_;
    struct Index {
        uint32_t offset = 0, size = 0;
        bool keyframe = false;
    };
    std::vector<Index> frames_;
};

} // namespace reluva::video
