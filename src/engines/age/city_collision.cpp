// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_collision.h"

#include "engines/age/bound.h"

#include <algorithm>
#include <map>
#include <memory>

namespace reluva::age {

size_t buildCityCollision(const SearchSet& files, const Psdl& psdl, const CityMesh& mesh,
                          const std::vector<Instance>& instances, CollisionWorld& out) {
    out = CollisionWorld{};
    for (const CityMesh::Batch& b : mesh.batches)
        for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
            auto at = [&](size_t k) {
                const gfx::Vertex& v = mesh.vertices[b.indices[i + k]];
                return Vec3{v.pos[0], v.pos[1], v.pos[2]};
            };
            out.add(at(0), at(1), at(2));
        }

    // Facade bounds: a value, a height, the left and right vertices.
    const size_t lastVertex = psdl.vertices.empty() ? 0 : psdl.vertices.size() - 1;
    for (const PsdlBlock& block : psdl.blocks)
        for (const PsdlAttribute& a : block.attributes) {
            if (a.type != psdlattr::FacadeBound || a.words.size() < 4 || psdl.vertices.empty())
                continue;
            const Vec3 l = psdl.vertices[std::min<size_t>(a.words[2], lastVertex)];
            const Vec3 r = psdl.vertices[std::min<size_t>(a.words[3], lastVertex)];
            const float top = a.words[1] < psdl.heights.size() ? psdl.heights[a.words[1]] : std::max(l[1], r[1]) + 10;
            const float bottom = std::min(l[1], r[1]) - 1;
            const Vec3 a0{l[0], bottom, l[2]}, b0{r[0], bottom, r[2]}, b1{r[0], top, r[2]}, a1{l[0], top, l[2]};
            out.add(a0, b0, b1);
            out.add(a0, b1, a1);
        }

    // The placed models' bounds, read once a model.
    std::map<std::string, std::unique_ptr<Bound>> bounds;
    size_t used = 0;
    for (const Instance& inst : instances) {
        auto it = bounds.find(inst.model);
        if (it == bounds.end()) {
            std::unique_ptr<Bound> b;
            std::vector<uint8_t> data;
            auto loaded = std::make_unique<Bound>();
            if (!files.read("bound/" + inst.model + "_BOUND.bnd", data) &&
                !readBound(std::string(data.begin(), data.end()), *loaded))
                b = std::move(loaded);
            it = bounds.emplace(inst.model, std::move(b)).first;
        }
        if (!it->second)
            continue;
        ++used;
        const Bound& b = *it->second;
        const auto& m = inst.matrix;
        auto place = [&](const Vec3& p) {
            return Vec3{m[0] * p[0] + m[3] * p[1] + m[6] * p[2] + m[9], m[1] * p[0] + m[4] * p[1] + m[7] * p[2] + m[10],
                        m[2] * p[0] + m[5] * p[1] + m[8] * p[2] + m[11]};
        };
        for (const BoundPolygon& p : b.polygons) {
            CollisionSurface surface;
            if (p.material < b.materials.size())
                surface = {b.materials[p.material].friction, b.materials[p.material].elasticity};
            const Vec3 v0 = place(b.vertices[p.vertices[0]]), v1 = place(b.vertices[p.vertices[1]]),
                       v2 = place(b.vertices[p.vertices[2]]);
            out.add(v0, v1, v2, surface);
            if (p.count == 4)
                out.add(v0, v2, place(b.vertices[p.vertices[3]]), surface);
        }
    }
    out.build();
    return used;
}

} // namespace reluva::age
