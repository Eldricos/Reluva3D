// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/bnk.h"

#include "common/audio/ea_audio.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>

namespace reluva::nfs {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// The tags known (bnk.md).
constexpr int kChannels = 0x82, kCompression = 0x83, kRate = 0x84, kSamples = 0x85, kLoopStart = 0x86,
              kLoopEnd = 0x87, kDataOffset = 0x88;
// Bytes that open a section of tags rather than being one.
bool isSection(uint8_t b) { return b == 0xfc || b == 0xfd || b == 0xfe; }

} // namespace

Error parseSoundHeader(const uint8_t* data, size_t size, size_t at, SoundHeader& out, size_t* end) {
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
    auto get = [&](int tag, auto& field) {
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

Error parseSoundBank(const std::vector<uint8_t>& data, SoundBank& out) {
    out = {};
    if (data.size() < 12 || std::memcmp(data.data(), "BNKl", 4) != 0)
        return Error::fail("Not a valid .bnk bank: no BNKl header");
    out.version = le16(&data[4]);
    const unsigned count = le16(&data[6]);
    // The headers' area: the slot table and the PT headers; the audio data
    // follows it.
    const size_t headers = std::min<size_t>(le32(&data[8]), data.size());
    if (12 + 4ull * count > data.size())
        return Error::fail("Not a valid .bnk bank: its slots run past the end");
    out.slots.resize(count);
    for (unsigned i = 0; i < count; ++i) {
        // Each slot's offset counts from the slot. Slots of 0 or -1 are
        // empty; a few others point into the audio data, at no header
        // (left over, by the look of them), and count as empty too.
        const uint32_t offset = le32(&data[12 + 4 * i]);
        const uint64_t at = 12ull + 4 * i + offset;
        if (offset == 0 || offset == 0xffffffff || at + 4 > headers || std::memcmp(&data[at], "PT\0\0", 4) != 0)
            continue;
        SoundHeader h;
        if (Error err = parseSoundHeader(data.data(), data.size(), static_cast<size_t>(at), h))
            return Error::fail("Not a valid .bnk bank: sound " + std::to_string(i) + ": " + err.message);
        out.slots[i] = std::move(h);
    }
    return Error::success();
}

Error decodeBankSound(const std::vector<uint8_t>& data, const SoundHeader& h, audio::Sound& out) {
    out = {};
    out.rate = h.rate;
    out.channels = h.channels;
    // The loop, its end as a frame past the last (the mixer clamps it to
    // the sound's length).
    if (h.loopEnd > h.loopStart) {
        out.loopStart = h.loopStart;
        out.loopEnd = size_t(h.loopEnd) + 1;
    }
    if (h.dataOffset > data.size())
        return Error::fail("the sound's data starts past the end of its bank");
    const uint8_t* p = data.data() + h.dataOffset;
    const size_t room = data.size() - h.dataOffset;
    switch (h.compression) {
    case SoundHeader::kPcm16: {
        // Some sounds' data overlaps the next's or the end; as much as is
        // there is used.
        const size_t frames = std::min<size_t>(h.samples, room / (2 * h.channels));
        out.format = 1;
        out.samples.resize(frames * h.channels);
        for (size_t i = 0; i < out.samples.size(); ++i)
            out.samples[i] = static_cast<int16_t>(le16(p + 2 * i));
        return Error::success();
    }
    case SoundHeader::kEaXa:
        if (h.channels != 1)
            return Error::fail("stereo EA-XA in a bank isn't known");
        out.format = audio::kFormatEaXa;
        return audio::decodeEaXa(p, room, h.samples, out.samples);
    case SoundHeader::kMicroTalk:
        if (h.channels != 1)
            return Error::fail("MicroTalk is mono only");
        out.format = audio::kFormatMicroTalk;
        return audio::decodeMicroTalk(p, room, h.samples, out.samples);
    default:
        return Error::fail("compression " + std::to_string(h.compression) + " isn't known");
    }
}

// 0x501b1f (the voice's base pitch) and 0x502cb0 (with the bend).
int patchCents(const SoundHeader& h, int key, int bend) {
    auto tag = [&](int t, int otherwise) {
        auto it = h.tags.find(t);
        return it == h.tags.end() ? otherwise : static_cast<int>(it->second);
    };
    int fine = tag(0x10, 0);
    if (fine >= 128 && fine < 256)
        fine -= 256; // a signed byte: 249 is -7
    const int root = tag(0x07, key);
    const int range = tag(0x0A, 0) * 100;
    const int bent = (std::clamp(bend, 0, 127) - 64) * range;
    return fine - (root - key) * 100 + (bent >= 0 ? bent / 64 : -((-bent + 63) / 64)); // sar 6: floored
}

int engineBend(int rpm, int redline, int gear) {
    const int over = redline + 1000 - (gear > 5 ? 500 : 0);
    if (over <= 0)
        return 0;
    return std::clamp(127 * rpm / over, 0, 100);
}

} // namespace reluva::nfs
