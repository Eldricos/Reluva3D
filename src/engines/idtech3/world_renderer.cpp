// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the original lays out its sky box and cloud texture coordinates, and
// how it treats surfaces' lightmaps, was read from the GPL Quake III Arena
// renderer (tr_sky.c, tr_bsp.c); docs/formats/idtech3/shader.md describes
// it. This is new code written from that description.

#include "engines/idtech3/world_renderer.h"
#include "engines/idtech3/patch.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace reluva::idtech3 {

namespace {

constexpr int kPatchLevel = 6; // quads per Bezier block, each way

gfx::Vertex toGfx(const BspVertex& v) {
    gfx::Vertex g;
    for (int i = 0; i < 3; ++i) {
        g.pos[i] = v.pos[size_t(i)];
        g.normal[i] = v.normal[size_t(i)];
    }
    for (int i = 0; i < 2; ++i) {
        g.uv[i] = v.st[size_t(i)];
        g.uv2[i] = v.lightmap[size_t(i)];
    }
    for (int i = 0; i < 4; ++i)
        g.color[i] = v.color[size_t(i)];
    brightenLight(g.color);
    return g;
}

} // namespace

void brightenLight(uint8_t* rgb) {
    int c[3] = {rgb[0] * 2, rgb[1] * 2, rgb[2] * 2};
    const int top = std::max({c[0], c[1], c[2]});
    for (int i = 0; i < 3; ++i)
        rgb[i] = static_cast<uint8_t>(top > 255 ? c[i] * 255 / top : c[i]);
}

void WorldRenderer::clear() {
    for (gfx::MeshId m : {mesh_, skyMesh_})
        if (m)
            r_.destroyMesh(m);
    mesh_ = skyMesh_ = 0;
    for (gfx::TextureId t : lightmaps_)
        r_.destroyTexture(t);
    lightmaps_.clear();
    fogs_.clear();
    batches_.clear();
    skySides_ = {};
    skyMaterial_ = -1;
    triangles_ = 0;
}

void WorldRenderer::buildSky(const ShaderDef& sky) {
    // A box around the camera. Each side maps (s, t) in -1..1 to a direction
    // the way the original lays its sky box out; the far box's images go on
    // the sides in its order (rt, bk, lf, ft, up, dn). Cloud layers get the
    // original's texture coordinates: where the direction meets a dome
    // `cloudHeight` above the camera on a world of radius 4096.
    static const int kAxes[6][3] = {{3, -1, 2}, {-3, 1, 2}, {1, 3, 2}, {-1, -3, 2}, {-2, -1, 3}, {2, -1, -3}};
    static const char* const kSuffix[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    static const int kImageForSide[6] = {0, 2, 1, 3, 4, 5};
    constexpr int kSubdiv = 8;
    constexpr float kBox = 1024;
    constexpr float kWorldRadius = 4096;
    const float h = sky.cloudHeight;

    std::vector<gfx::Vertex> verts;
    std::vector<uint32_t> indices;
    for (int side = 0; side < 6; ++side) {
        const uint32_t base = uint32_t(verts.size());
        skySideFirst_[size_t(side)] = uint32_t(indices.size());
        for (int j = 0; j <= kSubdiv; ++j)
            for (int i = 0; i <= kSubdiv; ++i) {
                const float s = float(i) / kSubdiv * 2 - 1, t = float(j) / kSubdiv * 2 - 1;
                const float b[3] = {s * kBox, t * kBox, kBox};
                gfx::Vertex v{};
                for (int k = 0; k < 3; ++k) {
                    const int axis = kAxes[side][k];
                    v.pos[k] = axis < 0 ? -b[-axis - 1] : b[axis - 1];
                }
                // Image rows run top to bottom: t = +1 is the top.
                v.uv[0] = (s + 1) / 2;
                v.uv[1] = 1 - (t + 1) / 2;
                const float x = v.pos[0], y = v.pos[1], z = v.pos[2];
                const float len2 = x * x + y * y + z * z;
                const float p = (-2 * z * kWorldRadius +
                                 2 * std::sqrt(z * z * kWorldRadius * kWorldRadius +
                                               len2 * (2 * kWorldRadius * h + h * h))) /
                                (2 * len2);
                const float c[3] = {x * p, y * p, z * p + kWorldRadius};
                const float cl = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
                v.uv2[0] = std::acos(c[0] / cl);
                v.uv2[1] = std::acos(c[1] / cl);
                v.normal[2] = 1;
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                verts.push_back(v);
            }
        for (int j = 0; j < kSubdiv; ++j)
            for (int i = 0; i < kSubdiv; ++i) {
                const uint32_t a = base + uint32_t(j * (kSubdiv + 1) + i);
                indices.insert(indices.end(), {a, a + 1, a + kSubdiv + 1, a + 1, a + kSubdiv + 2, a + kSubdiv + 1});
            }
    }
    skySideCount_ = uint32_t(indices.size() / 6);
    cloudCount_ = skySideCount_ * 5; // every side but the bottom
    if (r_.createMesh(verts, indices, skyMesh_))
        return;
    if (!sky.skyFarBox.empty())
        for (int side = 0; side < 6; ++side)
            skySides_[size_t(side)] =
                shaders_.image(sky.skyFarBox + "_" + kSuffix[kImageForSide[side]], true, !sky.noMipmaps);
}

Error WorldRenderer::load(const Bsp& bsp, const ShaderLibrary& library) {
    clear();
    constexpr size_t kLightmapPixels = size_t(kLightmapSize) * kLightmapSize;
    std::vector<uint8_t> rgba(kLightmapPixels * 4);
    for (size_t l = 0; l < bsp.lightmapCount(); ++l) {
        const uint8_t* src = bsp.lightmaps.data() + l * kLightmapPixels * 3;
        for (size_t i = 0; i < kLightmapPixels; ++i) {
            std::copy_n(src + i * 3, 3, &rgba[i * 4]);
            brightenLight(&rgba[i * 4]);
            rgba[i * 4 + 3] = 255;
        }
        gfx::TextureParams p;
        p.clamp = true;
        p.mipmaps = false;
        gfx::TextureId t = 0;
        if (Error err = r_.createTexture(kLightmapSize, kLightmapSize, rgba.data(), p, t))
            return err;
        lightmaps_.push_back(t);
    }

    for (const BspFog& f : bsp.fogs) {
        Fog fog;
        if (const ShaderDef* def = library.find(f.shader); def && def->fog) {
            fog.color = def->fogColor;
            fog.distance = def->fogDistance;
        }
        fogs_.push_back(fog);
    }

    // Every surface's triangles, grouped by what they're drawn with and
    // ordered by sort key.
    std::vector<gfx::Vertex> vertices;
    std::map<std::tuple<float, int, int, int>, std::vector<uint32_t>> groups; // (sort, material, lightmap, fog)
    std::vector<BspVertex> patchVerts;
    std::vector<uint32_t> patchIndices;
    for (const BspModel& model : bsp.models)
        for (int si = model.firstSurface; si < model.firstSurface + model.numSurfaces; ++si) {
            const BspSurface& s = bsp.surfaces[size_t(si)];
            if (s.type != SurfaceType::Planar && s.type != SurfaceType::TriangleSoup && s.type != SurfaceType::Patch)
                continue;
            // Triangle soups are always lit by their vertices, as are
            // surfaces without a lightmap.
            const bool lightmapped = s.lightmap >= 0 && s.type != SurfaceType::TriangleSoup;
            const int mi = shaders_.material(bsp.shaders[size_t(s.shader)].name,
                                             lightmapped ? Lighting::Lightmap : Lighting::Vertex);
            const ShaderCache::Material& mat = shaders_.get(mi);
            if (mat.skip)
                continue;
            std::vector<uint32_t>& out = groups[{mat.sort, mi, lightmapped ? s.lightmap : -1, s.fog}];
            const uint32_t base = uint32_t(vertices.size());
            const BspVertex* v = &bsp.vertices[size_t(s.firstVertex)];
            if (s.type == SurfaceType::Patch) {
                patchVerts.clear();
                patchIndices.clear();
                tessellatePatch(v, s.patchWidth, s.patchHeight, kPatchLevel, patchVerts, patchIndices);
                for (const BspVertex& pv : patchVerts)
                    vertices.push_back(toGfx(pv));
                for (uint32_t i : patchIndices)
                    out.push_back(base + i);
            } else {
                for (int i = 0; i < s.numVertices; ++i)
                    vertices.push_back(toGfx(v[i]));
                for (int i = 0; i + 2 < s.numIndices; i += 3)
                    for (int k = 0; k < 3; ++k)
                        out.push_back(base + uint32_t(bsp.indices[size_t(s.firstIndex + i + k)]));
            }
        }

    std::vector<uint32_t> indices;
    for (const auto& [key, list] : groups) {
        if (list.empty())
            continue;
        batches_.push_back({std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key),
                            uint32_t(indices.size()), uint32_t(list.size())});
        indices.insert(indices.end(), list.begin(), list.end());
    }
    triangles_ = indices.size() / 3;

    // The sky, from the first sky shader the map uses.
    for (const BspShader& s : bsp.shaders)
        if (const ShaderDef* def = library.find(s.name); def && def->sky) {
            buildSky(*def);
            skyMaterial_ = def->stages.empty() ? -1 : shaders_.material(*def, Lighting::Vertex, true);
            break;
        }

    if (indices.empty())
        return Error::fail("The map has nothing to draw");
    return r_.createMesh(vertices, indices, mesh_);
}

void WorldRenderer::drawSky(float time) const {
    // Around the camera, before the rest, which covers it.
    if (skyMesh_) {
        for (size_t side = 0; side < 6; ++side) {
            if (!skySides_[side])
                continue;
            gfx::DrawCall call;
            call.mesh = skyMesh_;
            call.firstIndex = skySideFirst_[side];
            call.indexCount = skySideCount_;
            call.texture = skySides_[side];
            call.followCamera = true;
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
            r_.draw(call);
        }
        if (skyMaterial_ >= 0) {
            gfx::DrawCall clouds;
            clouds.mesh = skyMesh_;
            clouds.indexCount = cloudCount_;
            shaders_.draw(skyMaterial_, clouds, 0, time, true);
        }
    }
}

void WorldRenderer::drawSurfaces(float time, float sortMin, float sortMax) const {
    for (const Batch& b : batches_) {
        if (b.sort < sortMin || b.sort > sortMax)
            continue;
        gfx::DrawCall base;
        base.mesh = mesh_;
        base.firstIndex = b.firstIndex;
        base.indexCount = b.indexCount;
        shaders_.draw(b.material, base, b.lightmap >= 0 ? lightmaps_[size_t(b.lightmap)] : 0, time);
        if (b.fog >= 0)
            shaders_.drawFog(b.material, base, fogs_[size_t(b.fog)].color, fogs_[size_t(b.fog)].distance, time);
    }
}

} // namespace reluva::idtech3
