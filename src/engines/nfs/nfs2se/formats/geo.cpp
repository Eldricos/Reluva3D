// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/geo.h"

#include <cstring>

namespace reluva::nfs::nfs2se {

namespace {

uint32_t le32(const uint8_t *p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
int16_t s16(const uint8_t *p) {
    return static_cast<int16_t>(p[0] | p[1] << 8);
}

// The version, 32 words (memory addresses, never read), 8 bytes of 0.
constexpr size_t kHeaderSize = 140;
// A part's: vertex and polygon counts, its position, four u16, and three
// u64 that are 0, 1 and 1 in every retail model.
constexpr size_t kPartHeaderSize = 52;

} // namespace

Error parseCarModel(const std::vector<uint8_t> &data, CarModel &out) {
    out = {};
    auto bad = [](const std::string &why) { return Error::fail("Not a valid .geo file: " + why); };
    if (data.size() < kHeaderSize)
        return bad("too short");
    const uint8_t *d = data.data();
    out.version = le32(d);
    size_t at = kHeaderSize;
    for (CarModel::Part &part : out.parts) {
        if (at + kPartHeaderSize > data.size())
            return bad("a part runs past the end");
        const uint32_t nv = le32(d + at);
        const uint32_t np = le32(d + at + 4);
        for (int c = 0; c < 3; ++c)
            part.position[size_t(c)] = static_cast<float>(static_cast<int32_t>(le32(d + at + 8 + 4 * c))) / 65536;
        for (int c = 0; c < 4; ++c)
            part.unknown[size_t(c)] = static_cast<uint16_t>(d[at + 20 + 2 * c] | d[at + 21 + 2 * c] << 8);
        // An odd number of vertices is followed by 6 bytes, a vertex's worth.
        const uint64_t vertices = at + kPartHeaderSize;
        const uint64_t polygons = vertices + 6ull * nv + (nv % 2 ? 6 : 0);
        if (polygons + 12ull * np > data.size())
            return bad("a part runs past the end");
        for (uint32_t v = 0; v < nv; ++v) {
            const uint8_t *p = d + vertices + 6 * v;
            part.vertices.push_back({s16(p) / 256.0f, s16(p + 2) / 256.0f, s16(p + 4) / 256.0f});
        }
        for (uint32_t k = 0; k < np; ++k) {
            const uint8_t *p = d + polygons + 12 * k;
            CarModel::Polygon poly;
            poly.flags = le32(p);
            std::memcpy(poly.vertices.data(), p + 4, 4);
            for (uint8_t v : poly.vertices)
                if (v >= nv)
                    return bad("a polygon uses a vertex its part hasn't");
            poly.texture.assign(reinterpret_cast<const char *>(p + 8), 4);
            part.polygons.push_back(std::move(poly));
        }
        at = static_cast<size_t>(polygons + 12ull * np);
    }
    if (at != data.size())
        return bad(std::to_string(data.size() - at) + " bytes after the parts");
    return Error::success();
}

} // namespace reluva::nfs::nfs2se
