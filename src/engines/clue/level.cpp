// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/level.h"

#include <cstring>

namespace reluva::clue {

namespace {

uint16_t u16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t u32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(u16(d, o)) | uint32_t(u16(d, o + 2)) << 16; }

// A NUL-terminated name in a fixed field; what follows the NUL is whatever
// memory held.
std::string fixedName(const std::vector<uint8_t>& d, size_t o, size_t size) {
    const char* p = reinterpret_cast<const char*>(d.data() + o);
    return std::string(p, strnlen(p, size));
}

constexpr size_t kObjectName = 32;
constexpr size_t kTitle = 40;

bool readTimings(const std::vector<uint8_t>& d, size_t& p, size_t end, std::vector<Timing>& out) {
    if (end - p < 2)
        return false;
    const size_t n = u16(d, p);
    p += 2;
    if ((end - p) / 7 < n)
        return false;
    for (size_t i = 0; i < n; ++i, p += 7) {
        Timing t;
        t.time = u16(d, p);
        t.state = u16(d, p + 3);
        std::memcpy(t.raw.data(), d.data() + p, 7);
        out.push_back(t);
    }
    return true;
}

} // namespace

const ObjectDef::State* ObjectDef::state(uint16_t id) const {
    for (const State& s : states)
        if (s.id == id)
            return &s;
    return nullptr;
}

Error parseObject(const std::vector<uint8_t>& r, ObjectDef& out) {
    out = {};
    if (r.size() < 44)
        return Error::fail("object record cut short");
    // Its length, which the archive's directory gives as 2 more.
    const size_t end = u16(r, 0);
    if (end > r.size() || end < 44)
        return Error::fail("object record's length is out of range");
    if (r[2] != 1)
        return Error::fail("object version " + std::to_string(r[2]) + " isn't known");
    out.name = fixedName(r, 3, kObjectName);
    out.type = r[35];
    out.unknown = u32(r, 36);
    const size_t n = u16(r, 40);
    size_t p = 42;
    if ((end - p) / (2 + kObjectName) < n)
        return Error::fail(out.name + ": states cut short");
    for (size_t i = 0; i < n; ++i, p += 2 + kObjectName)
        out.states.push_back({u16(r, p), fixedName(r, p + 2, kObjectName)});
    if (!readTimings(r, p, end, out.timings))
        return Error::fail(out.name + ": timings cut short");
    if (p != end)
        return Error::fail(out.name + ": " + std::to_string(end - p) + " bytes left over");
    return Error::success();
}

Error parseLevel(const DataDrive& drive, Level& out) {
    out = {};
    const DataDrive::Entry* world = drive.find("/World.dat");
    const DataDrive::Entry* objects = drive.find("/Objects.dat");
    if (!world || !objects)
        return Error::fail("not a level: no /World.dat or /Objects.dat");
    if (const DataDrive::Entry* e = drive.find("/Info.dat"))
        out.info = drive.read(*e);
    if (const DataDrive::Entry* e = drive.find("/Workspace.dat"))
        out.workspace = drive.read(*e);

    // World.dat: a title, then the names of the objects placed.
    const std::vector<uint8_t> w = drive.read(*world);
    if (w.size() < kTitle + 2)
        return Error::fail("World.dat cut short");
    out.title = fixedName(w, 0, kTitle);
    const size_t names = u16(w, kTitle);
    size_t p = kTitle + 2;
    for (size_t i = 0; i < names; ++i) {
        if (p >= w.size() || w.size() - p - 1 < w[p])
            return Error::fail("World.dat's names cut short");
        out.objects.emplace_back(reinterpret_cast<const char*>(w.data() + p + 1), w[p]);
        p += 1 + w[p];
    }

    // Objects.dat: a count, then each placement with its own length. The
    // directory's size for it runs past the archive, so the count says
    // where it ends.
    const std::vector<uint8_t> o = drive.read(*objects);
    if (o.size() < 2)
        return Error::fail("Objects.dat cut short");
    const size_t count = u16(o, 0);
    p = 2;
    for (size_t i = 0; i < count; ++i) {
        if (o.size() - p < 2)
            return Error::fail("Objects.dat cut short at placement " + std::to_string(i));
        const size_t len = u16(o, p);
        if (len < 12 || len > o.size() - p)
            return Error::fail("placement " + std::to_string(i) + "'s length is out of range");
        const size_t end = p + len;
        size_t q = p + 2;
        Placement pl;
        pl.flags = o[q++];
        if (pl.flags & 0x02) {
            pl.tag = u16(o, q);
            q += 2;
        }
        if (end - q < 9)
            return Error::fail("placement " + std::to_string(i) + " cut short");
        pl.object = u16(o, q);
        pl.x = int16_t(u16(o, q + 2));
        pl.y = int16_t(u16(o, q + 4));
        pl.id = int16_t(u16(o, q + 6));
        pl.layer = o[q + 8];
        q += 9;
        if ((pl.flags & 0x20) && !readTimings(o, q, end, pl.timings))
            return Error::fail("placement " + std::to_string(i) + "'s timings cut short");
        if (q != end)
            return Error::fail("placement " + std::to_string(i) + " has " + std::to_string(end - q) +
                               " bytes left over");
        if (pl.object >= out.objects.size())
            return Error::fail("placement " + std::to_string(i) + "'s object " + std::to_string(pl.object) +
                               " is out of range");
        out.placements.push_back(std::move(pl));
        p = end;
    }
    return Error::success();
}

} // namespace reluva::clue
