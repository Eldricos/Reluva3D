// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/gfx3d.h"
#include "engines/nfs/nfs2se/formats/trk.h"

#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * NFS II's quads gathered into one mesh, by texture, for the viewer's 3D
 * pages. The files give a quad no texture coordinates: its picture is
 * laid across it whole, each vertex taking a corner of it
 * (docs/engines/nfs/games/nfs2se/formats/col.md, geo.md).
 */
class QuadMesh {
public:
    /** A texture's quads, as a range of the mesh's indices. */
    struct Run {
        int texture = 0; // the caller's number for it
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    /**
     * Adds a quad with the texture `texture`; points as the files give them
     * (x, height, y). `pictureCorners`: the corner each vertex takes, 0
     * bottom left, 1 bottom right, 2 top right, 3 top left (col.h).
     * `greys`: each vertex's colour, a grey, 255 for white.
     */
    void add(int texture, const std::array<Point, 4> &corners, const std::array<uint8_t, 4> &pictureCorners,
             const std::array<uint8_t, 4> &greys = {255, 255, 255, 255}) {
        const auto base = static_cast<uint32_t>(_vertices.size());
        const Point &a = corners[0];
        const Point &b = corners[1];
        const Point &c = corners[2];
        // The normal of the quad's first triangle, for the lighting.
        float n[3] = {(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
                      (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                      (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (float &v : n)
            v = length > 0 ? v / length : 0;
        constexpr float kUv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int k = 0; k < 4; ++k) {
            gfx::Vertex v{};
            for (int i = 0; i < 3; ++i) {
                v.pos[i] = corners[size_t(k)][size_t(i)];
                v.normal[i] = n[i];
            }
            v.uv[0] = kUv[pictureCorners[size_t(k)] & 3][0];
            v.uv[1] = kUv[pictureCorners[size_t(k)] & 3][1];
            v.color[0] = v.color[1] = v.color[2] = greys[size_t(k)];
            v.color[3] = 255;
            _vertices.push_back(v);
        }
        auto &indices = _byTexture[texture];
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    /** How many quads were added. */
    size_t quads() const { return _vertices.size() / 4; }

    /** Makes the mesh; `runs` gets each texture's range of it. Empty: no mesh, and success. */
    Error build(gfx::Renderer3D &renderer, gfx::MeshId &mesh, std::vector<Run> &runs) const {
        runs.clear();
        mesh = 0;
        if (_vertices.empty())
            return Error::success();
        std::vector<uint32_t> indices;
        for (const auto &[texture, list] : _byTexture) {
            runs.push_back({texture, static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(list.size())});
            indices.insert(indices.end(), list.begin(), list.end());
        }
        return renderer.createMesh(_vertices, indices, mesh);
    }

private:
    std::vector<gfx::Vertex> _vertices;
    std::map<int, std::vector<uint32_t>> _byTexture;
};

} // namespace reluva::nfs::nfs2se
