// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/bink_file.h"

#include <string>

namespace reluva::video {

namespace {

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }

} // namespace

Error BinkFile::open(std::vector<uint8_t> data) {
    *this = BinkFile();
    if (data.size() < 44 || data[0] != 'B' || data[1] != 'I' || data[2] != 'K')
        return Error::fail("Not a Bink file");
    revision_ = char(data[3]);
    const uint32_t frameCount = rd32(data, 8);
    largestFrame_ = rd32(data, 12);
    width_ = int(rd32(data, 20));
    height_ = int(rd32(data, 24));
    fpsNum_ = rd32(data, 28);
    fpsDen_ = rd32(data, 32);
    flags_ = rd32(data, 36);
    const uint32_t trackCount = rd32(data, 40);
    if (width_ <= 0 || height_ <= 0 || width_ > 32767 || height_ > 32767 || trackCount > 256 ||
        frameCount > 1000000)
        return Error::fail("Bink header out of range");
    // The tracks' settings: for each, two bytes then the channel count;
    // then for each, the rate and flags; then for each, an ID.
    size_t o = 44;
    if (o + trackCount * 12 + (uint64_t(frameCount) + 1) * 4 > data.size())
        return Error::fail("Bink file too short for its header");
    tracks_.resize(trackCount);
    for (BinkAudioTrack& t : tracks_) {
        t.channels = rd16(data, o + 2);
        o += 4;
    }
    for (BinkAudioTrack& t : tracks_) {
        t.sampleRate = rd16(data, o);
        t.flags = rd16(data, o + 2);
        // The flags are authoritative for stereo (bit 13); bit 12 picks
        // the DCT form.
        t.channels = (t.flags & (1u << 13)) ? 2 : 1;
        t.dct = t.flags & (1u << 12);
        o += 4;
    }
    for (BinkAudioTrack& t : tracks_) {
        t.id = rd32(data, o);
        o += 4;
    }
    // The frame index: one more entry than frames, the last the end;
    // bit 0 marks a keyframe.
    frames_.resize(frameCount);
    uint32_t next = rd32(data, o) & ~1u;
    for (uint32_t i = 0; i < frameCount; i++) {
        const uint32_t entry = rd32(data, o + 4 * size_t(i));
        const uint32_t start = entry & ~1u;
        uint32_t end = rd32(data, o + 4 * size_t(i + 1)) & ~1u;
        if (i + 1 == frameCount && (end < start || end > data.size()))
            end = uint32_t(data.size());
        if (start != next || end < start || end > data.size())
            return Error::fail("Bink frame " + std::to_string(i) + " out of range");
        frames_[i] = {start, end - start, (entry & 1) != 0};
        next = end;
    }
    data_ = std::move(data);
    return Error::success();
}

Error BinkFile::frame(size_t index, Frame& out) const {
    out = Frame();
    if (index >= frames_.size())
        return Error::fail("No such Bink frame");
    const Index& f = frames_[index];
    out.keyframe = f.keyframe;
    size_t o = f.offset;
    const size_t end = size_t(f.offset) + f.size;
    for (size_t t = 0; t < tracks_.size(); t++) {
        if (o + 4 > end)
            return Error::fail("Bink frame " + std::to_string(index) + " ends in its audio");
        // The packet's size, counting the sample count that starts it.
        const uint32_t size = rd32(data_, o);
        o += 4;
        if (size == 0) {
            out.audio.push_back({});
            out.audioBytes.push_back(0);
            continue;
        }
        if (size < 4 || o + size > end)
            return Error::fail("Bink frame " + std::to_string(index) + "'s audio packet out of range");
        out.audioBytes.push_back(rd32(data_, o));
        out.audio.push_back({data_.data() + o + 4, size - 4});
        o += size;
    }
    out.video = {data_.data() + o, end - o};
    return Error::success();
}

} // namespace reluva::video
