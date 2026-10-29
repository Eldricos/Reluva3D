// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/mus.h"

#include "common/audio/ea_audio.h"

#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
uint32_t be32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | p[3];
}

constexpr size_t kMapHeader = 12, kSectionSize = 28, kEventSize = 16;

} // namespace

Error MusicStream::open(std::vector<uint8_t> data) {
    data_ = std::move(data);
    segments_.clear();
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .mus file: " + why); };
    // Chunks: 4-character tag, u32 size (the 8 bytes included).
    size_t at = 0;
    Segment* current = nullptr;
    while (at + 8 <= data_.size()) {
        const uint32_t size = le32(&data_[at + 4]);
        if (size < 8 || at + size > data_.size())
            return bad("a chunk runs past the end");
        const char* tag = reinterpret_cast<const char*>(&data_[at]);
        if (std::memcmp(tag, "SCHl", 4) == 0) {
            Segment s;
            s.offset = static_cast<uint32_t>(at);
            if (Error err = parseSoundHeader(data_.data(), at + size, at + 8, s.header))
                return bad(err.message);
            segments_.push_back(std::move(s));
            current = &segments_.back();
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
    if (at != data_.size())
        return bad("it ends inside a chunk");
    return Error::success();
}

int MusicStream::segmentAt(uint32_t offset) const {
    for (size_t i = 0; i < segments_.size(); ++i)
        if (segments_[i].offset == offset)
            return static_cast<int>(i);
    return -1;
}

Error MusicStream::decode(size_t index, audio::Sound& out) const {
    out = {};
    if (index >= segments_.size())
        return Error::fail("no segment " + std::to_string(index));
    const Segment& s = segments_[index];
    out.rate = s.header.rate;
    out.channels = s.header.channels;
    if (s.header.channels != 2)
        return Error::fail("only stereo music is known");
    out.samples.reserve(size_t(s.header.samples) * 2);
    for (const auto& [offset, size] : s.blocks) {
        const uint8_t* p = data_.data() + offset;
        if (s.header.compression == SoundHeader::kEaXa) {
            out.format = audio::kFormatEaXa;
            if (Error err = audio::decodeEaXaStereoBlock(p, size, out.samples))
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

Error parseMusicMap(const std::vector<uint8_t>& data, MusicMap& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid Pathfinder map: " + why); };
    if (data.size() < kMapHeader || std::memcmp(data.data(), "PFDx", 4) != 0)
        return bad("no PFDx header");
    out.version = data[4];
    out.start = data[5];
    const size_t sections = data[6];
    if (data[7] != kEventSize)
        return bad("event records of " + std::to_string(data[7]) + " bytes");
    const size_t events = be32(&data[8]);
    // Sections, then events, then each section's offset in the .mus.
    if (data.size() != kMapHeader + sections * (kSectionSize + 4) + events * kEventSize)
        return bad("its size doesn't fit its counts");
    for (size_t i = 0; i < sections; ++i) {
        const uint8_t* r = &data[kMapHeader + i * kSectionSize];
        MusicMap::Section s;
        std::memcpy(s.raw.data(), r, kSectionSize);
        s.part = r[0];
        // Up to eight routes from byte 4: low, high, next section.
        const int routes = r[1];
        if (routes > 8)
            return bad("a section with " + std::to_string(routes) + " routes");
        for (int k = 0; k < routes; ++k) {
            MusicMap::Route route{r[4 + 3 * k], r[5 + 3 * k], r[6 + 3 * k]};
            if (route.next >= static_cast<int>(sections))
                return bad("a route to a section that isn't there");
            s.routes.push_back(route);
        }
        out.sections.push_back(std::move(s));
    }
    const size_t eventsAt = kMapHeader + sections * kSectionSize;
    out.events.resize(events);
    for (size_t i = 0; i < events; ++i)
        std::memcpy(out.events[i].data(), &data[eventsAt + i * kEventSize], kEventSize);
    const size_t offsetsAt = eventsAt + events * kEventSize;
    for (size_t i = 0; i < sections; ++i)
        out.offsets.push_back(be32(&data[offsetsAt + 4 * i])); // big-endian, unlike the .mus
    return Error::success();
}

int MusicMap::next(int section, int value) const {
    if (section < 0 || section >= static_cast<int>(sections.size()))
        return -1;
    if (sections[section].routes.empty())
        return -1;
    for (const Route& r : sections[section].routes)
        if (value >= r.low && value <= r.high)
            return r.next;
    return section; // no route for the value: 0x4e6cd2 keeps the section
}

int MusicMap::event(int section, int e) const {
    if (section < 0 || section >= static_cast<int>(sections.size()) || e < 0 || e >= 16)
        return -1;
    const size_t part = static_cast<size_t>(sections[section].part);
    if (part >= events.size())
        return -1;
    const int to = events[part][static_cast<size_t>(e)];
    return to < static_cast<int>(sections.size()) ? to : -1;
}

} // namespace reluva::nfs
