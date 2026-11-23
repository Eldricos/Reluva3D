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
 * laid across it whole, its corners in the quad's order taking the
 * picture's corners in an order of the caller's
 * (docs/engines/nfs/games/nfs2se/formats/trk.md, geo.md).
 */
class QuadMesh {
public:
    /** A texture's quads, as a range of the mesh's indices. */
    struct Run {
        int texture = 0; // the caller's number for it
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    /** Which picture corner each of a quad's vertices takes, in turn. */
    enum class Corners {
        kTopLeftFirst,    // top left, top right, bottom right, bottom left: a guess, for the tracks
        kBottomLeftFirst, // bottom left, bottom right, top right, top left: the cars' (0x4736a0)
    };

    /** Adds a quad with the texture `texture`; points as the files give them (x, height, y). */
    void add(int texture, const std::array<Point, 4> &corners, Corners order = Corners::kTopLeftFirst) {
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
        constexpr float kTopFirst[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        constexpr float kBottomFirst[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const auto &uv = order == Corners::kTopLeftFirst ? kTopFirst : kBottomFirst;
        for (int k = 0; k < 4; ++k) {
            gfx::Vertex v{};
            for (int i = 0; i < 3; ++i) {
                v.pos[i] = corners[size_t(k)][size_t(i)];
                v.normal[i] = n[i];
            }
            v.uv[0] = uv[k][0];
            v.uv[1] = uv[k][1];
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
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
