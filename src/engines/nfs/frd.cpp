// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/frd.h"

#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

// Reads little-endian values in order, failing (once) past the end.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : d_(data) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    bool has(size_t n) const { return ok_ && pos_ + n <= d_.size(); }

    const uint8_t* take(size_t n) {
        if (!has(n)) {
            ok_ = false;
            return nullptr;
        }
        const uint8_t* p = d_.data() + pos_;
        pos_ += n;
        return p;
    }
    uint32_t u32() {
        const uint8_t* p = take(4);
        return p ? p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                       static_cast<uint32_t>(p[3]) << 24
                 : 0;
    }
    uint16_t u16() {
        const uint8_t* p = take(2);
        return p ? static_cast<uint16_t>(p[0] | p[1] << 8) : 0;
    }
    uint8_t u8() {
        const uint8_t* p = take(1);
        return p ? *p : 0;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    Track::Vec3 vec3() {
        const float x = f32(), y = f32(), z = f32();
        return {x, y, z};
    }
    std::vector<uint8_t> bytes(size_t n) {
        const uint8_t* p = take(n);
        return p ? std::vector<uint8_t>(p, p + n) : std::vector<uint8_t>{};
    }
    // A count about to be followed by `size`-byte records: fails rather
    // than allocating for a count the rest of the file can't hold.
    uint32_t count(size_t size) {
        const uint32_t n = u32();
        if (ok_ && size && static_cast<uint64_t>(n) * size > d_.size() - pos_)
            ok_ = false;
        return ok_ ? n : 0;
    }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
    bool ok_ = true;
};

constexpr size_t kHeaderSize = 28;
constexpr size_t kPolygonSize = 14;
constexpr int kNeighbours = 300;

void readPolygons(Reader& r, uint32_t n, std::vector<Track::Polygon>& out) {
    out.resize(n);
    for (Track::Polygon& p : out) {
        for (uint16_t& v : p.vertices)
            v = r.u16();
        p.texture = r.u16();
        p.unknown = r.u16();
        p.flags = r.u8();
        p.extra = r.u8();
    }
}

} // namespace

Error parseTrack(const std::vector<uint8_t>& data, Track& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .frd track: " + why); };
    Reader r(data);
    r.take(kHeaderSize);
    const uint32_t blocks = r.count(84) + 1; // stored as one less
    if (!r.ok())
        return bad("too short");

    // The blocks: geometry, neighbours and their records.
    out.blocks.resize(blocks);
    for (Track::Block& b : out.blocks) {
        b.centre = r.vec3();
        for (Track::Vec3& c : b.corners)
            c = r.vec3();
        const uint32_t vertices = r.u32();
        b.highResVertices = r.u32();
        b.lowResVertices = r.u32();
        b.mediumResVertices = r.u32();
        b.duplicateVertices = r.u32();
        b.objectVertices = r.u32();
        if (!r.has(16ull * vertices))
            return bad("a block's vertices run past the end");
        b.vertices.resize(vertices);
        for (Track::Vec3& v : b.vertices)
            v = r.vec3();
        b.colours.resize(vertices);
        for (uint32_t& c : b.colours)
            c = r.u32();
        for (int i = 0; i < kNeighbours; ++i) {
            const auto block = static_cast<int16_t>(r.u16());
            r.u16();
            if (block >= 0)
                b.neighbours.push_back(block);
        }
        b.startPosition = r.u32();
        const uint32_t positions = r.u32(), polygons = r.u32(), road = r.u32(), refs = r.u32(), polyObjects = r.u32(),
                       sounds = r.u32(), lights = r.u32();
        if (!r.has(8ull * positions + 8ull * polygons + 12ull * road + 20ull * refs + 20ull * polyObjects +
                   16ull * sounds + 16ull * lights))
            return bad("a block's records run past the end");
        b.positions = r.bytes(8ull * positions);
        b.polygonRoad = r.bytes(8ull * polygons);
        b.virtualRoad = r.bytes(12ull * road);
        b.objectRefs = r.bytes(20ull * refs);
        b.polygonObjects = r.bytes(20ull * polyObjects);
        b.sounds = r.bytes(16ull * sounds);
        b.lights = r.bytes(16ull * lights);
        if (!r.ok())
            return bad("a block runs past the end");
    }

    // Each block's polygons: seven lists, each a count, then (if not 0)
    // the count again and the polygons; then four object layers, each a
    // polygon total, then (if not 0) the objects.
    for (size_t bi = 0; bi < out.blocks.size(); ++bi) {
        Track::Block& b = out.blocks[bi];
        for (auto& list : b.polygons) {
            const uint32_t n = r.count(kPolygonSize);
            if (n) {
                if (r.u32() != n)
                    return bad("block " + std::to_string(bi) + "'s polygon counts disagree");
                readPolygons(r, n, list);
            }
        }
        for (auto& layer : b.objectLayers) {
            if (!r.u32())
                continue;
            const uint32_t objects = r.count(4);
            layer.resize(objects);
            for (Track::BlockObject& o : layer) {
                o.type = r.u32();
                if (o.type == 1)
                    readPolygons(r, r.count(kPolygonSize), o.polygons);
            }
        }
        auto inside = [&](const std::vector<Track::Polygon>& list) {
            for (const Track::Polygon& p : list)
                for (uint16_t v : p.vertices)
                    if (v >= b.vertices.size())
                        return false;
            return true;
        };
        for (const auto& list : b.polygons)
            if (!inside(list))
                return bad("block " + std::to_string(bi) + " has a polygon outside its vertices");
        for (const auto& layer : b.objectLayers)
            for (const Track::BlockObject& o : layer)
                if (!inside(o.polygons))
                    return bad("block " + std::to_string(bi) + " has an object polygon outside its vertices");
        if (!r.ok())
            return bad("the polygons run past the end");
    }

    // Objects placed on the track: four groups per block and one more.
    out.extraObjects.resize(4ull * blocks + 1);
    for (auto& group : out.extraObjects) {
        group.resize(r.count(28));
        for (Track::ExtraObject& o : group) {
            o.type = r.u32();
            if (o.type == 4) {
                r.take(8);
                o.reference = r.vec3();
                r.take(4);
            } else if (o.type == 3) {
                r.take(8 + 12 + 8);
                const uint16_t keys = r.u16();
                o.keyDelay = static_cast<int16_t>(r.u16());
                o.keys.resize(keys);
                for (auto& k : o.keys) {
                    for (int32_t& p : k.position)
                        p = static_cast<int32_t>(r.u32());
                    for (int16_t& q : k.rotation)
                        q = static_cast<int16_t>(r.u16());
                }
            } else {
                return bad("an object of unknown type " + std::to_string(o.type));
            }
            const uint32_t vertices = r.count(16);
            o.vertices.resize(vertices);
            for (Track::Vec3& v : o.vertices)
                v = r.vec3();
            o.colours.resize(vertices);
            for (uint32_t& c : o.colours)
                c = r.u32();
            readPolygons(r, r.count(kPolygonSize), o.polygons);
            for (const Track::Polygon& p : o.polygons)
                for (uint16_t v : p.vertices)
                    if (v >= o.vertices.size())
                        return bad("an object has a polygon outside its vertices");
            if (!r.ok())
                return bad("the objects run past the end");
        }
    }

    // The texture table, to the end of the file.
    out.textures.resize(r.count(47));
    for (Track::Texture& t : out.textures) {
        t.width = r.u16();
        t.height = r.u16();
        t.unknown1 = r.u32();
        for (float& c : t.corners)
            c = r.f32();
        t.unknown2 = r.u32();
        t.lane = r.u8() != 0;
        t.picture = r.u16();
    }
    if (!r.ok())
        return bad("the texture table runs past the end");
    if (r.pos() != data.size())
        return bad(std::to_string(data.size() - r.pos()) + " bytes left after the texture table");
    for (const Track::Block& b : out.blocks)
        for (const auto& list : b.polygons)
            for (const Track::Polygon& p : list)
                if (p.texture >= out.textures.size())
                    return bad("a polygon uses a texture past the table");
    return Error::success();
}

} // namespace reluva::nfs
