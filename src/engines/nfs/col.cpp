// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/col.h"

#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
float f32(const uint8_t* p) {
    const uint32_t v = le32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
float fixed(const uint8_t* p) { return static_cast<float>(static_cast<int32_t>(le32(p))) / 65536.0f; }
TrackCollision::Vec3 unit(const uint8_t* p) {
    return {static_cast<int8_t>(p[0]) / 127.0f, static_cast<int8_t>(p[1]) / 127.0f, static_cast<int8_t>(p[2]) / 127.0f};
}

constexpr uint16_t kTextures = 2, kObjects = 7, kStructures = 8, kRoad = 15, kObjects2 = 18;
constexpr size_t kRoadPointSize = 36, kKeySize = 20;

Error readObjects(const uint8_t* p, const uint8_t* end, uint32_t count, std::vector<TrackCollision::Object>& out) {
    for (uint32_t i = 0; i < count; ++i) {
        if (end - p < 4)
            return Error::fail("an object runs past its block");
        const uint16_t size = le16(p);
        TrackCollision::Object o;
        o.type = p[2];
        o.structure = p[3];
        if (size < 4 || end - p < size)
            return Error::fail("an object runs past its block");
        if (o.type == 1 && size >= 16) {
            o.reference = {fixed(p + 4), fixed(p + 8), fixed(p + 12)};
        } else if (o.type == 3 && size >= 8) {
            const uint16_t keys = le16(p + 4);
            o.delay = static_cast<int16_t>(le16(p + 6));
            if (8 + keys * kKeySize > size)
                return Error::fail("an object's keys run past it");
            for (uint16_t k = 0; k < keys; ++k) {
                const uint8_t* q = p + 8 + k * kKeySize;
                TrackCollision::Object::Key key;
                for (int c = 0; c < 3; ++c)
                    key.position[c] = static_cast<int32_t>(le32(q + 4 * c));
                for (int c = 0; c < 4; ++c)
                    key.rotation[c] = static_cast<int16_t>(le16(q + 12 + 2 * c));
                o.keys.push_back(key);
            }
        }
        out.push_back(std::move(o));
        p += size;
    }
    return Error::success();
}

} // namespace

Error parseCollision(const std::vector<uint8_t>& data, TrackCollision& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .col file: " + why); };
    if (data.size() < 16 || std::memcmp(data.data(), "COLL", 4) != 0)
        return bad("no COLL header");
    const uint8_t* d = data.data();
    const uint32_t blocks = le32(d + 12);
    if (16 + 4ull * blocks > data.size())
        return bad("its block table runs past the end");

    // Each block: u32 size (header included), u16 id, u16 record count.
    for (uint32_t b = 0; b < blocks; ++b) {
        const uint64_t at = 16ull + le32(d + 16 + 4 * b);
        if (at + 8 > data.size())
            return bad("a block starts past the end");
        const uint32_t size = le32(d + at);
        const uint16_t id = le16(d + at + 4), count = le16(d + at + 6);
        if (size < 8 || at + size > data.size())
            return bad("a block runs past the end");
        const uint8_t* p = d + at + 8;
        const uint8_t* end = d + at + size;
        const size_t room = size - 8;

        if (id == kTextures) {
            if (8ull * count > room)
                return bad("its textures run past their block");
            out.textures.resize(count);
            for (uint16_t i = 0; i < count; ++i)
                std::memcpy(out.textures[i].data(), p + 8 * i, 8);
        } else if (id == kStructures) {
            // Each: u32 size, u16 vertices, u16 polygons, vertices (3 floats
            // and a u32), polygons (u16 texture, 4 vertex bytes).
            for (uint16_t i = 0; i < count; ++i) {
                if (end - p < 8)
                    return bad("a structure runs past its block");
                const uint32_t ssize = le32(p);
                const uint16_t nv = le16(p + 4), np = le16(p + 6);
                if (ssize < 8 + 16u * nv + 6u * np || static_cast<size_t>(end - p) < ssize)
                    return bad("a structure runs past its block");
                TrackCollision::Structure s;
                for (uint16_t v = 0; v < nv; ++v) {
                    const uint8_t* q = p + 8 + 16 * v;
                    s.vertices.push_back({f32(q), f32(q + 4), f32(q + 8)});
                    s.vertexUnknown.push_back(le32(q + 12));
                }
                for (uint16_t k = 0; k < np; ++k) {
                    const uint8_t* q = p + 8 + 16 * nv + 6 * k;
                    TrackCollision::Structure::Polygon poly;
                    poly.texture = le16(q);
                    for (int c = 0; c < 4; ++c) {
                        poly.vertices[c] = q[2 + c];
                        if (poly.vertices[c] >= nv)
                            return bad("a structure's polygon uses a vertex it hasn't");
                    }
                    s.polygons.push_back(poly);
                }
                out.structures.push_back(std::move(s));
                p += ssize;
            }
        } else if (id == kObjects || id == kObjects2) {
            if (Error err = readObjects(p, end, count, id == kObjects ? out.objects : out.objects2))
                return bad(err.message);
        } else if (id == kRoad) {
            if (kRoadPointSize * count > room)
                return bad("its road runs past its block");
            for (uint16_t i = 0; i < count; ++i) {
                const uint8_t* q = p + kRoadPointSize * i;
                TrackCollision::RoadPoint r;
                r.position = {fixed(q), fixed(q + 4), fixed(q + 8)};
                r.unknown = le32(q + 12);
                r.normal = unit(q + 16);
                r.forward = unit(q + 20);
                r.right = unit(q + 24);
                r.extra = {q[19], q[23], q[27]};
                r.leftWall = fixed(q + 28);
                r.rightWall = fixed(q + 32);
                out.road.push_back(r);
            }
        }
        // Other blocks aren't known; they're skipped.
    }
    for (const TrackCollision::Object& o : out.objects)
        if (o.structure >= out.structures.size())
            return bad("an object uses a structure that isn't there");
    return Error::success();
}

} // namespace reluva::nfs
