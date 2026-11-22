// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/formats/mus.h"

#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

uint32_t be32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
           p[3];
}

constexpr size_t kMapHeader = 12;
constexpr size_t kSectionSize = 28;
constexpr size_t kEventSize = 16;

} // namespace

Error parseMusicMap(const std::vector<uint8_t> &data, MusicMap &out) {
    out = {};
    auto bad = [](const std::string &why) { return Error::fail("Not a valid Pathfinder map: " + why); };
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
        const uint8_t *r = &data[kMapHeader + i * kSectionSize];
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
    for (const Route &r : sections[section].routes)
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
