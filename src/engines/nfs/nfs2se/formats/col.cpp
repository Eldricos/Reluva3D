// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/col.h"

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

constexpr uint16_t kTextures = 2;
constexpr uint16_t kMeshes = 8;
constexpr uint16_t kPlacements = 7;
constexpr uint16_t kRoad = 15;
constexpr size_t kTextureSize = 10;
constexpr size_t kRoadPointSize = 36;

struct Block {
    uint16_t id;
    uint16_t count;
    const uint8_t *begin;
    const uint8_t *end;
};

// The file's blocks, or an empty list with `why` set.
std::vector<Block> blocks(const std::vector<uint8_t> &data, std::string &why) {
    std::vector<Block> out;
    if (data.size() < 16 || std::memcmp(data.data(), "COLL", 4) != 0) {
        why = "no COLL header";
        return {};
    }
    const uint8_t *d = data.data();
    const uint32_t count = le32(d + 12);
    if (16 + 4ull * count > data.size()) {
        why = "its block table runs past the end";
        return {};
    }
    for (uint32_t b = 0; b < count; ++b) {
        const uint64_t at = 16ull + le32(d + 16 + 4 * b);
        if (at + 8 > data.size() || le32(d + at) < 8 || at + le32(d + at) > data.size()) {
            why = "a block runs past the end";
            return {};
        }
        out.push_back({le16(d + at + 4), le16(d + at + 6), d + at + 8, d + at + le32(d + at)});
    }
    return out;
}

} // namespace

bool isNfs2Collision(const std::vector<uint8_t> &data) {
    std::string why;
    for (const Block &b : blocks(data, why))
        if (b.id == kTextures)
            return b.count > 0 && static_cast<size_t>(b.end - b.begin) / b.count == kTextureSize;
    return false;
}

Error parseCollision(const std::vector<uint8_t> &data, Collision &out) {
    out = {};
    auto bad = [](const std::string &why) { return Error::fail("Not a valid .col file: " + why); };
    std::string why;
    const std::vector<Block> list = blocks(data, why);
    if (list.empty())
        return bad(why.empty() ? "no blocks" : why);
    for (const Block &b : list) {
        const size_t room = static_cast<size_t>(b.end - b.begin);
        if (b.id == kTextures) {
            if (kTextureSize * b.count > room)
                return bad("its textures run past their block");
            for (uint16_t i = 0; i < b.count; ++i) {
                const uint8_t *p = b.begin + kTextureSize * i;
                Collision::Texture t;
                t.picture = le16(p);
                t.unknown = le16(p + 2);
                std::memcpy(t.unknown4.data(), p + 4, 3);
                std::memcpy(t.unknown7.data(), p + 7, 3);
                out.textures.push_back(t);
            }
        } else if (b.id == kMeshes) {
            if (Error err = readMeshes(b.begin, b.end, b.count, out.meshes))
                return bad(err.message);
        } else if (b.id == kRoad) {
            if (kRoadPointSize * b.count > room)
                return bad("its road runs past its block");
            for (uint16_t i = 0; i < b.count; ++i) {
                const uint8_t *p = b.begin + kRoadPointSize * i;
                Collision::RoadPoint r;
                for (int c = 0; c < 3; ++c)
                    r.position[size_t(c)] = static_cast<float>(static_cast<int32_t>(le32(p + 4 * c))) / 65536.0f;
                std::memcpy(r.up.data(), p + 12, 3);
                std::memcpy(r.forward.data(), p + 15, 3);
                std::memcpy(r.across.data(), p + 18, 3);
                r.block = le16(p + 22);
                for (int w = 0; w < 4; ++w)
                    r.unknown[size_t(w)] = le16(p + 24 + 2 * w);
                r.unknown32 = le32(p + 32);
                out.road.push_back(r);
            }
        }
    }
    // The placements after the meshes they name.
    for (const Block &b : list)
        if (b.id == kPlacements)
            if (Error err = readPlacements(b.begin, b.end, b.count, out.meshes.size(), out.placements))
                return bad(err.message);
    return Error::success();
}

} // namespace reluva::nfs::nfs2se
