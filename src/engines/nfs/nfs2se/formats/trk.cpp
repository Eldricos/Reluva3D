// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/trk.h"

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
int16_t s16(const uint8_t *p) {
    return static_cast<int16_t>(le16(p));
}
int32_t s32(const uint8_t *p) {
    return static_cast<int32_t>(le32(p));
}

constexpr float kUnit = 65536.0f; // the 16.16 fixed point's 1.0

// A point of three 16.16 values.
Point fixedPoint(const uint8_t *p) {
    return {static_cast<float>(s32(p)) / kUnit, static_cast<float>(s32(p + 4)) / kUnit,
            static_cast<float>(s32(p + 8)) / kUnit};
}

// A vertex: three s16 in 256ths of the unit, from `base` (16.16).
Point vertexFrom(const uint8_t *p, const std::array<int32_t, 3> &base) {
    Point v;
    for (int c = 0; c < 3; ++c)
        v[size_t(c)] = static_cast<float>(base[size_t(c)] + 256 * s16(p + 2 * c)) / kUnit;
    return v;
}

Quad quad(const uint8_t *p) {
    Quad q;
    q.texture = le16(p);
    q.shades = le16(p + 2);
    std::memcpy(q.vertices.data(), p + 4, 4);
    return q;
}

Error bad(const std::string &why) {
    return Error::fail("Not a valid .trk file: " + why);
}

// The extrablocks read into fields, each record of a fixed size; one of
// another size fails.
Error readRecords(TrackBlock &block) {
    for (const ExtraBlock &x : block.extra) {
        const uint8_t *p = x.data.data();
        auto fits = [&](size_t size) { return x.data.size() >= size * x.count; };
        if (x.id == 6) {
            if (!fits(8))
                return Error::fail("its road rows run past their extrablock");
            for (uint16_t i = 0; i < x.count; ++i, p += 8)
                block.roadRows.push_back({le16(p), p[2], p[3], le16(p + 4), le16(p + 6)});
        } else if (x.id == 9) {
            if (!fits(4))
                return Error::fail("its road lines run past their extrablock");
            for (uint16_t i = 0; i < x.count; ++i, p += 4)
                block.roadLines.push_back({p[0], p[1], p[2], p[3]});
        } else if (x.id == 11) {
            if (!fits(20))
                return Error::fail("its obstacles run past their extrablock");
            for (uint16_t i = 0; i < x.count; ++i, p += 20)
                block.obstacles.push_back({fixedPoint(p), le16(p + 12), le16(p + 14), p[16], p[18], p[19]});
        } else if (x.id == 17) {
            if (!fits(16))
                return Error::fail("its sounds run past their extrablock");
            for (uint16_t i = 0; i < x.count; ++i, p += 16)
                block.sounds.push_back({fixedPoint(p), le16(p + 12), le16(p + 14)});
        }
    }
    return Error::success();
}

} // namespace

Error readMeshes(const uint8_t *p, const uint8_t *end, uint16_t count, std::vector<Mesh> &out) {
    out.clear();
    for (uint16_t i = 0; i < count; ++i) {
        if (end - p < 8)
            return Error::fail("a mesh runs past its block");
        const uint32_t size = le32(p);
        const uint16_t nv = le16(p + 4);
        const uint16_t nq = le16(p + 6);
        if (size < 8u + 6u * nv + 8u * nq || static_cast<size_t>(end - p) < size)
            return Error::fail("a mesh runs past its block");
        Mesh m;
        for (uint16_t v = 0; v < nv; ++v)
            m.vertices.push_back(vertexFrom(p + 8 + 6 * v, {0, 0, 0}));
        for (uint16_t q = 0; q < nq; ++q) {
            m.quads.push_back(quad(p + 8 + 6 * nv + 8 * q));
            for (uint8_t v : m.quads.back().vertices)
                if (v >= nv)
                    return Error::fail("a mesh's polygon uses a vertex it hasn't");
        }
        out.push_back(std::move(m));
        p += size;
    }
    return Error::success();
}

Error readPlacements(const uint8_t *p, const uint8_t *end, uint16_t count, size_t meshes, std::vector<Placement> &out) {
    for (uint16_t i = 0; i < count; ++i) {
        if (end - p < 4)
            return Error::fail("a placement runs past its block");
        const uint16_t size = le16(p);
        Placement o;
        o.type = p[2];
        o.mesh = p[3];
        if (size < 4 || end - p < size)
            return Error::fail("a placement runs past its block");
        if (o.mesh >= meshes)
            return Error::fail("a placement names a mesh there isn't");
        if ((o.type == 1 && size == 16) || (o.type == 4 && size == 20)) {
            o.position = fixedPoint(p + 4);
            if (o.type == 4)
                o.obstacle = le32(p + 16);
        } else if (o.type == 3 && size >= 8 && size == 8u + 20u * le16(p + 4)) {
            o.animationWord = le16(p + 6);
            for (uint16_t k = 0; k < le16(p + 4); ++k) {
                const uint8_t *q = p + 8 + 20 * k;
                Placement::Key key;
                key.position = fixedPoint(q);
                for (int c = 0; c < 4; ++c)
                    key.rest[size_t(c)] = s16(q + 12 + 2 * c);
                o.keys.push_back(key);
            }
        } else {
            return Error::fail("a placement of type " + std::to_string(o.type) + " and size " + std::to_string(size) +
                               " isn't known");
        }
        out.push_back(std::move(o));
        p += size;
    }
    return Error::success();
}

Error parseTrack(const std::vector<uint8_t> &data, Track &out) {
    out = {};
    const uint8_t *d = data.data();
    const size_t n = data.size();
    if (n < 32 || std::memcmp(d, "TRAC", 4) != 0)
        return bad("no TRAC header");
    for (int i = 0; i < 5; ++i)
        out.header[size_t(i)] = le32(d + 4 + 4 * i);
    const uint32_t superblocks = le32(d + 24);
    const uint32_t blocks = le32(d + 28);
    // Then the superblocks' offsets, each block's reference point, and each
    // block's superblock (u16).
    const uint64_t tables = 32 + 4ull * superblocks;
    if (tables + 12ull * blocks + 2ull * blocks > n)
        return bad("its tables run past the end");
    std::vector<std::array<int32_t, 3>> references(blocks);
    for (uint32_t b = 0; b < blocks; ++b)
        for (int c = 0; c < 3; ++c)
            references[b][size_t(c)] = s32(d + tables + 12 * b + 4 * c);
    for (uint32_t b = 0; b < blocks; ++b)
        out.superblockOf.push_back(le16(d + tables + 12ull * blocks + 2 * b));

    // The blocks' records, before their vertices can be made absolute: the
    // first of each block's are the next block's.
    struct Raw {
        const uint8_t *at;
    };
    std::vector<Raw> raws;
    for (uint32_t s = 0; s < superblocks; ++s) {
        const uint64_t at = le32(d + 32 + 4 * s);
        if (at + 12 > n)
            return bad("a superblock starts past the end");
        const uint32_t size = le32(d + at);
        const uint32_t count = le32(d + at + 4);
        if (at + size > n || 12 + 4ull * count > size)
            return bad("a superblock runs past the end");
        for (uint32_t k = 0; k < count; ++k) {
            const uint64_t b = at + le32(d + at + 12 + 4 * k);
            if (b + 88 > at + size || b + le32(d + b) > at + size)
                return bad("a block runs past its superblock");
            raws.push_back({d + b});
        }
    }
    if (raws.size() != blocks)
        return bad(std::to_string(raws.size()) + " blocks in the superblocks, " + std::to_string(blocks) +
                   " in the header");

    for (uint32_t b = 0; b < blocks; ++b) {
        const uint8_t *p = raws[b].at;
        const uint32_t size = le32(p);
        TrackBlock block;
        block.unknown = le16(p + 10);
        block.serial = le32(p + 12);
        for (int c = 0; c < 4; ++c)
            block.corners[size_t(c)] = fixedPoint(p + 16 + 12 * c);
        block.reference = {static_cast<float>(references[b][0]) / kUnit, static_cast<float>(references[b][1]) / kUnit,
                           static_cast<float>(references[b][2]) / kUnit};
        block.fromNext = le16(p + 68);
        // Counts by level, low to high detail.
        block.vertexCounts = {le16(p + 70), le16(p + 72), le16(p + 74)};
        const uint16_t firsts[3] = {le16(p + 76), le16(p + 80), le16(p + 84)};
        const uint16_t seconds[3] = {le16(p + 78), le16(p + 82), le16(p + 86)};
        const uint32_t vertices = block.fromNext + block.vertexCounts[TrackBlock::kHigh];
        uint64_t polygons = 0;
        for (int l = 0; l < 3; ++l)
            polygons += firsts[l] + seconds[l];
        const uint64_t polygonsAt = 88 + 6ull * vertices;
        const uint64_t tableAt = 64ull + le32(p + 64);
        // The extrablock table follows the polygons, on 4 bytes.
        if (tableAt != polygonsAt + 8 * polygons && tableAt != polygonsAt + 8 * polygons + 2)
            return bad("block " + std::to_string(b) + "'s polygons don't end where its extrablocks start");
        const uint16_t extras = le16(p + 8);
        if (tableAt + 4ull * extras > size)
            return bad("block " + std::to_string(b) + "'s extrablocks run past it");

        const auto &next = references[(b + 1) % blocks];
        for (uint32_t v = 0; v < vertices; ++v)
            block.vertices.push_back(vertexFrom(p + 88 + 6 * v, v < block.fromNext ? next : references[b]));
        const uint8_t *q = p + polygonsAt;
        for (int l = 0; l < 3; ++l) {
            const uint32_t limit = block.fromNext + block.vertexCounts[size_t(l)];
            for (int group = 0; group < 2; ++group)
                for (uint16_t k = 0; k < (group ? seconds[l] : firsts[l]); ++k, q += 8) {
                    const Quad poly = quad(q);
                    for (uint8_t v : poly.vertices)
                        if (v >= limit)
                            return bad("block " + std::to_string(b) + "'s polygon uses a vertex its level hasn't");
                    (group ? block.levels[size_t(l)].second : block.levels[size_t(l)].first).push_back(poly);
                }
        }

        for (uint16_t e = 0; e < extras; ++e) {
            const uint32_t at = le32(p + tableAt + 4 * e);
            if (at + 8ull > size || at + uint64_t(le32(p + at)) > size || le32(p + at) < 8)
                return bad("block " + std::to_string(b) + "'s extrablock runs past it");
            ExtraBlock x;
            x.id = le16(p + at + 4);
            x.count = le16(p + at + 6);
            x.data.assign(p + at + 8, p + at + le32(p + at));
            block.extra.push_back(std::move(x));
        }
        // The meshes first, which the placements name.
        for (const ExtraBlock &x : block.extra)
            if (x.id == 8)
                if (Error err = readMeshes(x.data.data(), x.data.data() + x.data.size(), x.count, block.meshes))
                    return bad("block " + std::to_string(b) + ": " + err.message);
        for (const ExtraBlock &x : block.extra)
            if (x.id == 7 || x.id == 18 || x.id == 19) {
                const size_t first = block.placements.size();
                if (Error err = readPlacements(x.data.data(), x.data.data() + x.data.size(), x.count,
                                               block.meshes.size(), block.placements))
                    return bad("block " + std::to_string(b) + ": " + err.message);
                for (size_t i = first; i < block.placements.size(); ++i)
                    block.placements[i].list = static_cast<uint8_t>(x.id);
            }
        if (Error err = readRecords(block))
            return bad("block " + std::to_string(b) + ": " + err.message);
        out.blocks.push_back(std::move(block));
    }
    return Error::success();
}

} // namespace reluva::nfs::nfs2se
