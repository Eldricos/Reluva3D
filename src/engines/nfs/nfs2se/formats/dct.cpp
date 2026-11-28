// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/dct.h"

#include <cstring>
#include <string>

namespace reluva::nfs::nfs2se {

namespace {

uint16_t le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// A video chunk: the tag, its size, a u32 0, the picture's width and
// height (u16), then the MDEC frame.
constexpr size_t kChunkHeader = 16;

} // namespace

Error DctMovie::open(std::vector<uint8_t> data) {
    *this = {};
    auto bad = [](const std::string &why) { return Error::fail("Not a valid .dct file: " + why); };
    for (size_t at = 0; at + 8 <= data.size();) {
        const uint32_t size = le32(&data[at + 4]);
        if (size < 8 || at + size > data.size())
            return bad("a chunk runs past the end");
        if (std::memcmp(&data[at], "mTCD", 4) == 0) {
            if (size < kChunkHeader)
                return bad("a video chunk is too short");
            const int w = le16(&data[at + 12]);
            const int h = le16(&data[at + 14]);
            if (_frames.empty()) {
                _width = w;
                _height = h;
            } else if (w != _width || h != _height) {
                return bad("its frames' sizes differ");
            }
            _frames.push_back({static_cast<uint32_t>(at + kChunkHeader), size - static_cast<uint32_t>(kChunkHeader)});
        }
        at += size;
    }
    if (_frames.empty())
        return bad("no video chunks");
    _data = data;
    // The sound reader keeps the SCHl stream's chunks and passes over others.
    if (Error err = _audio.open(std::move(data)))
        return bad(err.message);
    return Error::success();
}

double DctMovie::framesPerSecond() const {
    if (_audio.segments().empty())
        return 15;
    const auto &header = _audio.segments()[0].header;
    if (header.rate <= 0 || header.samples == 0)
        return 15;
    return static_cast<double>(_frames.size()) * header.rate / header.samples;
}

Error DctMovie::decodeFrame(mdec::Decoder &decoder, size_t index) const {
    if (index >= _frames.size())
        return Error::fail("no frame " + std::to_string(index));
    const Frame &f = _frames[index];
    return decoder.decode(_data.data() + f.offset, f.size, _width, _height);
}

Error DctMovie::decodeAudio(audio::Sound &out) const {
    if (_audio.segments().empty())
        return Error::fail("no sound");
    return _audio.decode(0, out);
}

} // namespace reluva::nfs::nfs2se
