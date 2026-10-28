// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's two cities; see
// docs/formats/age/psdl.md.

#include "engines/age/psdl.h"

#include <cstring>

namespace reluva::age {

namespace {

constexpr uint32_t kVersion = 2;
constexpr uint16_t kLastAttribute = 0x80; // marks a block's last drawn attribute

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool need(uint64_t n) {
        if (ok && uint64_t(end - p) < n)
            ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? *p++ : 0; }
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
};

// Attributes whose subtype 0 means the count follows as a word.
bool counted(uint8_t type) {
    switch (type) {
    case psdlattr::RoadStrip:
    case psdlattr::SidewalkStrip:
    case psdlattr::RectStrip:
    case psdlattr::RoadFan:
    case psdlattr::Fan:
    case psdlattr::DividedRoad:
    case psdlattr::Tunnel:
    case psdlattr::Roof: return true;
    default: return false;
    }
}

} // namespace

size_t psdlAttributeLength(uint8_t type, uint16_t count) {
    switch (type) {
    case psdlattr::RoadStrip: return size_t(count) * 4;
    case psdlattr::SidewalkStrip:
    case psdlattr::RectStrip: return size_t(count) * 2;
    case psdlattr::Sliver:
    case psdlattr::Crosswalk:
    case psdlattr::FacadeBound: return 4;
    case psdlattr::RoadFan:
    case psdlattr::Fan:
    case psdlattr::Roof: return size_t(count) + 2;
    case psdlattr::DividedRoad: return size_t(count) * 6 + 2;
    case psdlattr::Tunnel: return count;
    case psdlattr::Texture: return 1;
    case psdlattr::Facade: return 6;
    default: return 0;
    }
}

std::string Psdl::texture(uint32_t index) const {
    return index >= 1 && index <= textures.size() ? textures[index - 1] : std::string();
}

Error readPsdl(const std::vector<uint8_t>& data, Psdl& out) {
    out = Psdl{};
    Reader r{data.data(), data.data() + data.size()};
    if (data.size() < 8 || std::memcmp(data.data(), "PSD0", 4) != 0)
        return Error::fail("not a city (no PSD0 header)");
    r.p += 4;
    if (const uint32_t version = r.u32(); version != kVersion)
        return Error::fail("unknown city version " + std::to_string(version));

    const uint32_t vertices = r.u32();
    if (!r.need(uint64_t(vertices) * 12))
        return Error::fail("vertices run past the end of the file");
    out.vertices.resize(vertices);
    for (Vec3& v : out.vertices)
        v = r.vec3();
    const uint32_t heights = r.u32();
    if (!r.need(uint64_t(heights) * 4))
        return Error::fail("heights run past the end of the file");
    out.heights.resize(heights);
    for (float& h : out.heights)
        h = r.f32();

    // Counts here and for the blocks are one more than what's stored:
    // index 0 means none.
    const uint32_t textures = r.u32();
    for (uint32_t i = 1; i < textures && r.ok; ++i) {
        const size_t n = r.u8();
        if (!r.need(n))
            break;
        std::string name(reinterpret_cast<const char*>(r.p), n);
        r.p += n;
        if (const size_t zero = name.find('\0'); zero != std::string::npos)
            name.resize(zero);
        out.textures.push_back(std::move(name));
    }
    const uint32_t blocks = r.u32();
    out.firstRoadBlock = r.u32();
    if (!r.ok || blocks == 0)
        return Error::fail("truncated before the blocks");
    out.blocks.resize(blocks);
    for (uint32_t b = 1; b < blocks && r.ok; ++b) {
        PsdlBlock& block = out.blocks[b];
        const uint32_t perimeter = r.u32(), words = r.u32();
        if (!r.need(uint64_t(perimeter) * 4 + uint64_t(words) * 2))
            break;
        block.perimeter.resize(perimeter);
        for (auto& [vertex, neighbour] : block.perimeter) {
            vertex = r.u16();
            neighbour = r.u16();
        }
        const uint8_t* end = r.p + size_t(words) * 2;
        bool last = false;
        while (r.p < end) {
            const uint16_t header = r.u16();
            PsdlAttribute a;
            a.type = header >> 3 & 0xf;
            a.subtype = header & 7;
            a.count = a.subtype;
            if (a.subtype == 0 && counted(a.type))
                a.count = r.u16();
            if (a.type > psdlattr::Roof)
                return Error::fail("block " + std::to_string(b) + ": unknown attribute type " +
                                   std::to_string(a.type));
            const size_t n = psdlAttributeLength(a.type, a.count);
            if (size_t(end - r.p) < n * 2)
                return Error::fail("block " + std::to_string(b) + ": an attribute runs past the block");
            a.words.resize(n);
            for (uint16_t& w : a.words)
                w = r.u16();
            // After the attribute marked last, only stray texture
            // attributes follow (98 blocks of the two cities).
            if (last && a.type != psdlattr::Texture)
                return Error::fail("block " + std::to_string(b) + ": attributes after the last");
            last = last || (header & kLastAttribute);
            block.attributes.push_back(std::move(a));
        }
        if (r.p != end)
            return Error::fail("block " + std::to_string(b) + ": an attribute runs past the block");
    }
    if (!r.ok)
        return Error::fail("truncated in the blocks");

    // A byte of flags for each block, another byte each, then the city's
    // bounds, centre and radius, then its roads.
    if (!r.need(uint64_t(blocks) * 2 + 40))
        return Error::fail("truncated after the blocks");
    for (PsdlBlock& block : out.blocks)
        block.flags = r.u8();
    for (PsdlBlock& block : out.blocks)
        block.extra = r.u8();
    out.min = r.vec3();
    out.max = r.vec3();
    out.center = r.vec3();
    out.radius = r.f32();
    if (!r.need(4))
        return Error::success(); // no roads (a city made for testing)
    const uint32_t roads = r.u32();
    for (uint32_t i = 0; i < roads; ++i) {
        PsdlRoad road;
        if (!r.need(6))
            return Error::fail("truncated in the roads");
        road.flags = r.u16();
        road.extra = r.u16();
        road.lanes[0] = r.u8();
        road.lanes[1] = r.u8();
        const size_t lanes = size_t(road.lanes[0]) + road.lanes[1];
        if (!r.need(lanes * 4 + 2 + 16 + 1))
            return Error::fail("truncated in the roads");
        for (size_t k = 0; k < lanes; ++k)
            road.lanePositions.push_back(r.f32());
        road.ends[0] = r.u8();
        road.ends[1] = r.u8();
        for (uint16_t& v : road.vertices)
            v = r.u16();
        const size_t n = r.u8();
        if (!r.need(n * 2))
            return Error::fail("truncated in the roads");
        for (size_t k = 0; k < n; ++k)
            road.blocks.push_back(int16_t(r.u16()));
        out.roads.push_back(std::move(road));
    }
    if (r.p != r.end)
        return Error::fail(std::to_string(r.end - r.p) + " bytes left after the roads");
    return Error::success();
}

} // namespace reluva::age
