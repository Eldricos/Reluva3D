// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/bound.h"

#include <cstdlib>
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

} // namespace reluva::age
