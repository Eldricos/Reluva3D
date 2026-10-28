// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's two road maps (city/sf.bai,
// city/london.bai); see docs/formats/age/bai.md.

#include "engines/age/bai.h"

#include <cstring>

namespace reluva::age {

namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool need(uint64_t n) {
        if (ok && uint64_t(end - p) < n)
            ok = false;
        return ok;
    }
    void skip(uint64_t n) {
        if (need(n))
            p += n;
    }
    uint16_t u16() {
        if (!need(2))
            return 0;
        const uint16_t v = static_cast<uint16_t>(p[0] | p[1] << 8);
        p += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        p += 4;
        return v;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    Vec3 vec3() {
        const float x = f32(), y = f32(), z = f32();
        return {x, y, z};
    }
    std::vector<Vec3> row(size_t n) {
        std::vector<Vec3> out;
        if (!need(uint64_t(n) * 12))
            return out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
            out.push_back(vec3());
        return out;
    }
};

void readSide(Reader& r, size_t n, AiRoadSide& s) {
    const uint16_t lanes = r.u16();
    s.trams = r.u16();
    s.trains = r.u16();
    r.u16(); // 1 throughout
    s.kind = r.u16();
    // Distances along each lane and the sidewalk (worked out again from
    // the points when they're needed).
    r.skip(uint64_t(lanes + 1) * n * 4);
    for (uint16_t i = 0; i < lanes; ++i)
        s.laneOffsets.push_back(r.f32());
    // Widths and offsets not used yet, then room for eight more floats
    // (left as the allocator filled them where unused).
    r.skip(44);
    for (uint16_t i = 0; i < lanes && r.ok; ++i)
        s.lanes.push_back(r.row(n));
    s.sidewalk = r.row(n);
    for (int i = 0; i < s.trams + s.trains && r.ok; ++i)
        s.tracks.push_back(r.row(n));
    s.curb = r.row(n);
    s.outerEdge = r.row(n);
}

} // namespace

Error readBai(const std::vector<uint8_t>& data, AiRoadMap& out) {
    out = AiRoadMap{};
    if (data.size() < 8 || std::memcmp(data.data(), "CAI1", 4) != 0)
        return Error::fail("not a road map (no CAI1)");
    Reader r{data.data() + 4, data.data() + data.size()};
    const uint16_t junctions = r.u16();
    const uint16_t roads = r.u16();
    out.roads.resize(roads);
    for (uint16_t i = 0; i < roads && r.ok; ++i) {
        AiRoad& road = out.roads[i];
        road.id = r.u16();
        const uint16_t n = r.u16();
        road.flags = r.u16();
        const uint16_t blocks = r.u16();
        for (uint16_t b = 0; b < blocks; ++b)
            road.blocks.push_back(r.u16());
        road.halfWidth = r.f32();
        road.speedLimit = r.f32();
        for (AiRoadSide& s : road.sides)
            readSide(r, n, s);
        r.skip(uint64_t(n) * 4); // distances along the middle line
        road.middle = r.row(n);
        r.skip(uint64_t(n) * 4 * 12); // each point's axes: across, up, back, ahead
        for (AiRoadEnd& e : road.ends) {
            e.junction = r.u32();
            r.u16(); // unused
            e.control = r.u16();
            r.u16();
            e.slot = r.u32();
            e.a = r.vec3();
            e.b = r.vec3();
        }
        if (r.ok && (road.id != i || n < 2))
            return Error::fail("road " + std::to_string(i) + " out of step");
    }
    out.junctions.resize(junctions);
    for (uint16_t i = 0; i < junctions && r.ok; ++i) {
        AiJunction& j = out.junctions[i];
        j.id = r.u16();
        j.block = r.u16();
        j.center = r.vec3();
        const uint16_t count = r.u16();
        for (uint16_t k = 0; k < count && r.ok; ++k)
            j.roads.push_back(r.u32());
    }
    // A table of each PSDL block's roads follows; not needed.
    if (!r.ok)
        return Error::fail("truncated road map");
    for (const AiRoad& road : out.roads)
        for (const AiRoadEnd& e : road.ends)
            if (e.junction >= junctions)
                return Error::fail("road " + std::to_string(road.id) + " ends at a junction that isn't there");
    for (const AiJunction& j : out.junctions)
        for (uint32_t k : j.roads)
            if (k >= roads)
                return Error::fail("junction " + std::to_string(j.id) + " lists a road that isn't there");
    return Error::success();
}

} // namespace reluva::age
