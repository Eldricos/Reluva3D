// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/bound.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace reluva::age {

Error readBound(std::string_view text, Bound& out) {
    out = Bound{};
    std::istringstream in{std::string(text)};
    std::string line;
    size_t lineNo = 0;
    BoundMaterial* material = nullptr; // inside an mtl block
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream words(line);
        std::string word;
        if (!(words >> word))
            continue;
        const std::string where = "line " + std::to_string(lineNo);
        if (material) {
            if (word == "}") {
                material = nullptr;
            } else if (word == "elasticity:") {
                words >> material->elasticity;
            } else if (word == "friction:") {
                words >> material->friction;
            }
            continue; // effect: and sound: aren't used yet
        }
        if (word == "v") {
            Vec3 v{};
            if (!(words >> v[0] >> v[1] >> v[2]))
                return Error::fail(where + ": bad vertex");
            out.vertices.push_back(v);
        } else if (word == "mtl") {
            out.materials.push_back({});
            material = &out.materials.back();
            words >> material->name;
        } else if (word == "quad" || word == "tri") {
            BoundPolygon p;
            p.count = word == "quad" ? 4 : 3;
            for (uint32_t i = 0; i < p.count; ++i)
                if (!(words >> p.vertices[i]))
                    return Error::fail(where + ": bad polygon");
            words >> p.material;
            out.polygons.push_back(p);
        }
        // version:, verts:, materials:, edges: and polys: give counts the
        // lines themselves say.
    }
    if (material)
        return Error::fail("a material isn't closed");
    for (const BoundPolygon& p : out.polygons)
        for (uint32_t i = 0; i < p.count; ++i)
            if (p.vertices[i] >= out.vertices.size())
                return Error::fail("a polygon names vertex " + std::to_string(p.vertices[i]) + " of " +
                                   std::to_string(out.vertices.size()));
    return Error::success();
}

Error readBinaryBound(const std::vector<uint8_t>& data, Bound& out) {
    out = Bound{};
    auto u32 = [&](size_t at) {
        return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 |
               uint32_t(data[at + 3]) << 24;
    };
    auto f32 = [&](size_t at) {
        const uint32_t v = u32(at);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    if (data.size() < 13 || data[0] != 1)
        return Error::fail("not a binary bound (version 1)");
    const uint64_t vertices = u32(1), materials = u32(5), polygons = u32(9);
    if (13 + vertices * 12 + materials * 104 + polygons * 10 != data.size())
        return Error::fail("binary bound's size doesn't match its counts");
    size_t at = 13;
    for (uint64_t i = 0; i < vertices; ++i, at += 12)
        out.vertices.push_back({f32(at), f32(at + 4), f32(at + 8)});
    for (uint64_t i = 0; i < materials; ++i, at += 104) {
        BoundMaterial m;
        m.name.assign(reinterpret_cast<const char*>(data.data() + at),
                      strnlen(reinterpret_cast<const char*>(data.data() + at), 32));
        m.elasticity = f32(at + 32);
        m.friction = f32(at + 36);
        out.materials.push_back(m);
    }
    for (uint64_t i = 0; i < polygons; ++i, at += 10) {
        BoundPolygon p;
        for (size_t k = 0; k < 4; ++k)
            p.vertices[k] = uint32_t(data[at + 2 * k]) | uint32_t(data[at + 2 * k + 1]) << 8;
        // No quad of the 8,259 in the .bnd files ends at vertex 0.
        p.count = p.vertices[3] == 0 ? 3 : 4;
        p.material = uint32_t(data[at + 8]) | uint32_t(data[at + 9]) << 8;
        out.polygons.push_back(p);
    }
    for (const BoundPolygon& p : out.polygons)
        for (uint32_t i = 0; i < p.count; ++i)
            if (p.vertices[i] >= out.vertices.size())
                return Error::fail("a polygon names vertex " + std::to_string(p.vertices[i]) + " of " +
                                   std::to_string(out.vertices.size()));
    return Error::success();
}

Error loadBound(const SearchSet& files, const std::string& model, Bound& out) {
    std::vector<uint8_t> data;
    if (!files.read("bound/" + model + "_BOUND.bnd", data))
        return readBound(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), out);
    if (!files.read("bound/" + model + "_BOUND.bbnd", data))
        return readBinaryBound(data, out);
    return Error::fail("no bound for " + model);
}

} // namespace reluva::age
