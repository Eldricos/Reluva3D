// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layouts are in docs/common/audio/ea-streams.md, first worked out on
// Need for Speed III's banks and music.

#include "audio/ea_stream.h"

#include "audio/ea_audio.h"

#include <cstring>
#include <string>
#include <type_traits>

namespace reluva::audio {

namespace {

uint32_t le32(const uint8_t *p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// The tags known (ea-streams.md).
constexpr int kChannels = 0x82;
constexpr int kCompression = 0x83;
constexpr int kRate = 0x84;
constexpr int kSamples = 0x85;
constexpr int kLoopStart = 0x86;
constexpr int kLoopEnd = 0x87;
constexpr int kDataOffset = 0x88;
// Bytes that open a section of tags rather than being one.
bool isSection(uint8_t b) {
    return b == 0xfc || b == 0xfd || b == 0xfe;
}

} // namespace

Error parseSoundHeader(const uint8_t *data, size_t size, size_t at, SoundHeader &out, size_t *end) {
    out = {};
    if (at + 4 > size || std::memcmp(data + at, "PT\0\0", 4) != 0)
        return Error::fail("no PT header");
    // Tags: a byte, a length byte, the value big-endian; 0xFF ends them.
    size_t p = at + 4;
    while (true) {
        if (p >= size)
            return Error::fail("a PT header runs past the end");
        const uint8_t tag = data[p++];
        if (tag == 0xff)
            break;
        if (isSection(tag))
            continue;
        if (p >= size || p + 1 + data[p] > size)
            return Error::fail("a PT header runs past the end");
        const uint8_t length = data[p++];
        uint32_t value = 0;
        for (uint8_t i = 0; i < length; ++i)
            value = value << 8 | data[p + i];
        p += length;
        out.tags[tag] = value;
    }
    if (end)
        *end = p;
    auto get = [&](int tag, auto &field) {
        if (auto it = out.tags.find(tag); it != out.tags.end())
            field = static_cast<std::remove_reference_t<decltype(field)>>(it->second);
    };
    get(kChannels, out.channels);
    get(kCompression, out.compression);
    get(kRate, out.rate);
    get(kSamples, out.samples);
    get(kLoopStart, out.loopStart);
    get(kLoopEnd, out.loopEnd);
    get(kDataOffset, out.dataOffset);
    if (out.channels < 1 || out.channels > 2)
        return Error::fail("a sound with " + std::to_string(out.channels) + " channels");
    if (out.rate <= 0)
        return Error::fail("a sound with no sample rate");
    return Error::success();
}

Error EaSoundStream::open(std::vector<uint8_t> data) {
    _data = std::move(data);
    _segments.clear();
    auto bad = [](const std::string &why) { return Error::fail("Not a valid EA sound stream: " + why); };
    // Chunks: 4-character tag, u32 size (the 8 bytes included).
    size_t at = 0;
    Segment *current = nullptr;
    while (at + 8 <= _data.size()) {
        const uint32_t size = le32(&_data[at + 4]);
        if (size < 8 || at + size > _data.size())
            return bad("a chunk runs past the end");
        const char *tag = reinterpret_cast<const char *>(&_data[at]);
        if (std::memcmp(tag, "SCHl", 4) == 0) {
            Segment s;
            s.offset = static_cast<uint32_t>(at);
            if (Error err = parseSoundHeader(_data.data(), at + size, at + 8, s.header))
                return bad(err.message);
            _segments.push_back(std::move(s));
            current = &_segments.back();
        } else if (std::memcmp(tag, "SCDl", 4) == 0) {
            if (!current)
                return bad("data before a header");
            current->blocks.push_back({static_cast<uint32_t>(at + 8), size - 8});
        } else if (std::memcmp(tag, "SCEl", 4) == 0) {
            current = nullptr;
        }
        // SCCl (the block count) and anything else: nothing to keep.
        at += size;
    }
    if (at != _data.size())
        return bad("it ends inside a chunk");
    return Error::success();
}

int EaSoundStream::segmentAt(uint32_t offset) const {
    for (size_t i = 0; i < _segments.size(); ++i)
        if (_segments[i].offset == offset)
            return static_cast<int>(i);
    return -1;
}

Error EaSoundStream::decode(size_t index, Sound &out) const {
    out = {};
    if (index >= _segments.size())
        return Error::fail("no segment " + std::to_string(index));
    const Segment &s = _segments[index];
    out.rate = s.header.rate;
    out.channels = s.header.channels;
    if (s.header.channels != 2)
        return Error::fail("only stereo music is known");
    out.samples.reserve(size_t(s.header.samples) * 2);
    for (const auto &[offset, size] : s.blocks) {
        const uint8_t *p = _data.data() + offset;
        if (s.header.compression == SoundHeader::kEaXa) {
            out.format = kFormatEaXa;
            if (Error err = decodeEaXaStereoBlock(p, size, out.samples))
                return err;
        } else if (s.header.compression == SoundHeader::kPcm16) {
            // u32 samples, then interleaved 16-bit stereo.
            out.format = 1;
            if (size < 4)
                return Error::fail("a PCM block is too short");
            const uint32_t n = le32(p);
            if (4 + 4ull * n > size)
                return Error::fail("a PCM block is shorter than its samples");
            for (uint32_t i = 0; i < 2 * n; ++i)
                out.samples.push_back(static_cast<int16_t>(p[4 + 2 * i] | p[5 + 2 * i] << 8));
        } else {
            return Error::fail("music compression " + std::to_string(s.header.compression) + " isn't known");
        }
    }
    return Error::success();
}

} // namespace reluva::audio
