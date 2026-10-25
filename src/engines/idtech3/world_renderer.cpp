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
#include <optional>
#include <tuple>

namespace reluva::idtech3 {

namespace {

// Shaders sorting up to "see through" (5) are opaque: the surfaces
// dynamic lights reach.
constexpr float kLastOpaqueSort = 5;

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
    culling_ = false;
    viewCluster_ = -2;
    pvsSurfaces_.clear();
    viewMask_ = {};
    surfaceRanges_.clear();
    allIndices_.clear();
    for (gfx::TextureId t : lightmaps_)
        r_.destroyTexture(t);
    lightmaps_.clear();
    fogs_.clear();
    batches_.clear();
    litSurfaces_.clear();
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
                shaders_.image(sky.skyFarBox + "_" + kSuffix[kImageForSide[side]], true, !sky.noMipmaps,
                               !sky.noPicmip);
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
    using GroupKey = std::tuple<float, int, int, int, int>;
    std::map<GroupKey, std::vector<uint32_t>> groups; // (sort, material, lightmap, fog, model)
    // Opaque surfaces, for dynamic lights: their group, where their
    // triangles start in it and how many indices, and their bounds.
    struct Pending {
        GroupKey group;
        uint32_t offset = 0, count = 0;
        std::array<float, 3> mins{}, maxs{};
        int surface = -1;
    };
    std::vector<Pending> pending;
    // Every world surface's triangles in its group, to draw only those
    // that can be seen (setView).
    struct Placed {
        GroupKey group;
        uint32_t offset = 0, count = 0;
        std::array<float, 3> mins{}, maxs{};
    };
    std::vector<std::optional<Placed>> placed(bsp.surfaces.size());
    std::vector<BspVertex> patchVerts;
    std::vector<uint32_t> patchIndices;
    for (size_t mi = 0; mi < bsp.models.size(); ++mi) {
        const BspModel& model = bsp.models[mi];
        for (int si = model.firstSurface; si < model.firstSurface + model.numSurfaces; ++si) {
            const BspSurface& s = bsp.surfaces[size_t(si)];
            if (s.type != SurfaceType::Planar && s.type != SurfaceType::TriangleSoup && s.type != SurfaceType::Patch)
                continue;
            // Triangle soups are always lit by their vertices, as are
            // surfaces without a lightmap.
            const bool lightmapped = s.lightmap >= 0 && s.type != SurfaceType::TriangleSoup && !vertexLight_;
            const int materialIndex = shaders_.material(bsp.shaders[size_t(s.shader)].name,
                                                        lightmapped ? Lighting::Lightmap : Lighting::Vertex);
            const ShaderCache::Material& mat = shaders_.get(materialIndex);
            if (mat.skip)
                continue;
            const GroupKey key{mat.sort, materialIndex, lightmapped ? s.lightmap : -1, s.fog, int(mi)};
            std::vector<uint32_t>& out = groups[key];
            const auto offset = uint32_t(out.size());
            const uint32_t base = uint32_t(vertices.size());
            const BspVertex* v = &bsp.vertices[size_t(s.firstVertex)];
            if (s.type == SurfaceType::Patch) {
                patchVerts.clear();
                patchIndices.clear();
                // Curves cut finely enough to stray from round by at most
                // r_subdivisions units, as the original's.
                tessellatePatch(v, s.patchWidth, s.patchHeight,
                                patchLevel(v, s.patchWidth, s.patchHeight, subdivisions_), patchVerts, patchIndices);
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
            // Solid, unmoving surfaces take dynamic lights (the world's:
            // brush entities move).
            const bool deformed = mat.deforms[0].type != gfx::Deform::Type::None;
            if (mi == 0 && out.size() > offset) {
                Placed p{key, offset, uint32_t(out.size()) - offset, {1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
                for (size_t vi = base; vi < vertices.size(); ++vi)
                    for (size_t k = 0; k < 3; ++k) {
                        p.mins[k] = std::min(p.mins[k], vertices[vi].pos[k]);
                        p.maxs[k] = std::max(p.maxs[k], vertices[vi].pos[k]);
                    }
                placed[size_t(si)] = p;
            }
            if (mi == 0 && mat.sort <= kLastOpaqueSort && !deformed && out.size() > offset) {
                Pending p{key, offset, uint32_t(out.size()) - offset, {1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, si};
                for (size_t vi = base; vi < vertices.size(); ++vi)
                    for (size_t k = 0; k < 3; ++k) {
                        p.mins[k] = std::min(p.mins[k], vertices[vi].pos[k]);
                        p.maxs[k] = std::max(p.maxs[k], vertices[vi].pos[k]);
                    }
                pending.push_back(p);
            }
        }
    }

    std::vector<uint32_t> indices;
    std::map<GroupKey, uint32_t> groupStart;
    std::map<GroupKey, int> groupBatch;
    for (const auto& [key, list] : groups) {
        if (list.empty())
            continue;
        groupStart[key] = uint32_t(indices.size());
        groupBatch[key] = int(batches_.size());
        batches_.push_back({std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key),
                            uint32_t(indices.size()), uint32_t(list.size()), std::get<4>(key)});
        indices.insert(indices.end(), list.begin(), list.end());
    }
    triangles_ = indices.size() / 3;
    for (const Pending& p : pending)
        litSurfaces_.push_back({groupStart[p.group] + p.offset, p.count, p.mins, p.maxs, p.surface});
    surfaceRanges_.assign(bsp.surfaces.size(), {});
    for (size_t s = 0; s < placed.size(); ++s)
        if (placed[s])
            surfaceRanges_[s] = {groupBatch[placed[s]->group], groupStart[placed[s]->group] + placed[s]->offset,
                                 placed[s]->count, placed[s]->mins, placed[s]->maxs};
    // What visibility needs of the map, kept.
    visPlanes_ = bsp.planes;
    visNodes_ = bsp.nodes;
    visLeafs_ = bsp.leafs;
    visLeafSurfaces_ = bsp.leafSurfaces;
    visClusters_ = bsp.visClusters;
    visClusterBytes_ = bsp.visClusterBytes;
    visData_ = bsp.visData;
    allIndices_ = indices;

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
    if (skyMesh_ && !fastSky_) {
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

void WorldRenderer::drawDynamicLights(const std::vector<gfx::DynamicLight>& lights) const {
    if (lights.empty() || !mesh_)
        return;
    r_.setDynamicLights(lights);
    const size_t n = std::min<size_t>(lights.size(), gfx::kMaxDynamicLights);
    gfx::DrawCall call;
    call.mesh = mesh_;
    call.dynamicLights = true;
    // Over what's drawn: the result is what's there times (1 + the light).
    call.state.src = gfx::Blend::DstColor;
    call.state.dst = gfx::Blend::One;
    call.state.depthEqual = true;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::None;
    for (const LitSurface& s : litSurfaces_) {
        // Only those drawn this frame, from where they are in its list.
        uint32_t first = s.firstIndex;
        if (culling_) {
            if (s.surface < 0 || frameMark_[size_t(s.surface)] != frameStamp_)
                continue;
            first = frameFirst_[size_t(s.surface)];
        }
        bool reached = false;
        for (size_t i = 0; i < n && !reached; ++i) {
            const gfx::DynamicLight& l = lights[i];
            reached = true;
            for (size_t k = 0; k < 3; ++k)
                reached = reached && l.origin[k] + l.radius >= s.mins[k] && l.origin[k] - l.radius <= s.maxs[k];
        }
        if (!reached)
            continue;
        call.firstIndex = first;
        call.indexCount = s.indexCount;
        r_.draw(call);
    }
}

void WorldRenderer::drawSurfaces(float time, float sortMin, float sortMax, bool worldOnly) const {
    for (const Batch& b : batches_) {
        if (b.sort < sortMin || b.sort > sortMax || (worldOnly && b.model != 0))
            continue;
        gfx::DrawCall base;
        base.mesh = mesh_;
        base.firstIndex = b.firstIndex;
        base.indexCount = b.indexCount;
        // Only what can be seen (setView), when that's known.
        if (culling_ && b.model == 0) {
            if (!b.visibleCount)
                continue;
            base.firstIndex = b.visibleFirst;
            base.indexCount = b.visibleCount;
        }
        shaders_.draw(b.material, base, b.lightmap >= 0 ? lightmaps_[size_t(b.lightmap)] : 0, time);
        if (b.fog >= 0)
            shaders_.drawFog(b.material, base, fogs_[size_t(b.fog)].color, fogs_[size_t(b.fog)].distance, time);
    }
}

int WorldRenderer::leafAt(const std::array<float, 3>& p) const {
    if (visNodes_.empty())
        return visLeafs_.empty() ? -1 : 0;
    int node = 0;
    while (node >= 0) {
        const BspNode& n = visNodes_[size_t(node)];
        const BspPlane& pl = visPlanes_[size_t(n.plane)];
        node = n.children[p[0] * pl.normal[0] + p[1] * pl.normal[1] + p[2] * pl.normal[2] - pl.dist >= 0 ? 0 : 1];
    }
    return -1 - node;
}

void WorldRenderer::setView(const std::array<float, 3>& eye, const std::array<uint8_t, 32>& areaMask,
                            const gfx::Mat4& viewProj) {
    // As the original's renderer: the leaves whose cluster the eye's can
    // see (the map's vis data) and whose area a closed door doesn't hide
    // (the area mask, a bit set for each hidden one), and their surfaces,
    // worked out again when the cluster or the mask changes; then, each
    // frame the view moves, those of them inside the view. Outside the map
    // (no cluster), everything.
    if (!mesh_)
        return;
    const int leaf = leafAt(eye);
    const int cluster = leaf >= 0 && size_t(leaf) < visLeafs_.size() ? visLeafs_[size_t(leaf)].cluster : -1;
    bool changed = false;
    if (cluster != viewCluster_ || areaMask != viewMask_) {
        changed = true;
        viewCluster_ = cluster;
        viewMask_ = areaMask;
        pvsSurfaces_.clear();
        if (cluster >= 0 && !visData_.empty()) {
            if (visibleMark_.size() != surfaceRanges_.size())
                visibleMark_.assign(surfaceRanges_.size(), 0);
            ++visibleStamp_;
            const uint8_t* pvs = &visData_[size_t(cluster) * size_t(visClusterBytes_)];
            for (const BspLeaf& l : visLeafs_) {
                if (l.cluster < 0 || l.cluster >= visClusters_ || !(pvs[l.cluster >> 3] & (1 << (l.cluster & 7))))
                    continue;
                if (l.area >= 0 && l.area < 256 && (areaMask[size_t(l.area >> 3)] & (1 << (l.area & 7))))
                    continue; // behind a closed door
                for (int i = 0; i < l.numLeafSurfaces; ++i) {
                    const int32_t sf = visLeafSurfaces_[size_t(l.firstLeafSurface + i)];
                    if (sf >= 0 && size_t(sf) < visibleMark_.size() && visibleMark_[size_t(sf)] != visibleStamp_ &&
                        surfaceRanges_[size_t(sf)].batch >= 0) {
                        visibleMark_[size_t(sf)] = visibleStamp_;
                        pvsSurfaces_.push_back(sf);
                    }
                }
            }
        }
    }
    if (cluster < 0 || visData_.empty()) {
        // Everything, from the list as loaded.
        if (culling_) {
            r_.setMeshIndices(mesh_, allIndices_);
            culling_ = false;
        }
        return;
    }
    if (!changed && culling_ && viewProj == viewProj_)
        return;
    viewProj_ = viewProj;
    // The view's four sides (left, right, bottom, top) from its matrix's
    // rows: a point is inside where each plane's a x + b y + c z + d >= 0.
    // A box is out when its corner furthest along a plane's normal is.
    auto row = [&](int r) {
        return std::array<float, 4>{viewProj[size_t(r)], viewProj[size_t(4 + r)], viewProj[size_t(8 + r)],
                                    viewProj[size_t(12 + r)]};
    };
    const auto r0 = row(0), r1 = row(1), r3 = row(3);
    std::array<std::array<float, 4>, 4> planes{};
    for (size_t k = 0; k < 4; ++k) {
        planes[0][k] = r3[k] + r0[k];
        planes[1][k] = r3[k] - r0[k];
        planes[2][k] = r3[k] + r1[k];
        planes[3][k] = r3[k] - r1[k];
    }
    auto inside = [&](const SurfaceRange& r) {
        for (const auto& p : planes) {
            float d = p[3];
            for (size_t k = 0; k < 3; ++k)
                d += p[k] * (p[k] >= 0 ? r.maxs[k] : r.mins[k]);
            if (d < 0)
                return false;
        }
        return true;
    };
    if (frameMark_.size() != surfaceRanges_.size()) {
        frameMark_.assign(surfaceRanges_.size(), 0);
        frameFirst_.assign(surfaceRanges_.size(), 0);
    }
    ++frameStamp_;
    std::vector<std::vector<int32_t>> perBatch(batches_.size());
    for (int32_t sf : pvsSurfaces_)
        if (inside(surfaceRanges_[size_t(sf)]))
            perBatch[size_t(surfaceRanges_[size_t(sf)].batch)].push_back(sf);
    // Each batch's surfaces one after another: the frame's whole list.
    frameIndices_.clear();
    frameSurfaces_ = 0;
    for (size_t b = 0; b < batches_.size(); ++b) {
        batches_[b].visibleFirst = uint32_t(frameIndices_.size());
        for (int32_t sf : perBatch[b]) {
            const SurfaceRange& r = surfaceRanges_[size_t(sf)];
            frameMark_[size_t(sf)] = frameStamp_;
            frameFirst_[size_t(sf)] = uint32_t(frameIndices_.size());
            frameIndices_.insert(frameIndices_.end(), allIndices_.begin() + r.firstIndex,
                                 allIndices_.begin() + r.firstIndex + r.count);
            ++frameSurfaces_;
        }
        batches_[b].visibleCount = uint32_t(frameIndices_.size()) - batches_[b].visibleFirst;
    }
    if (frameIndices_.empty())
        frameIndices_.push_back(0); // a buffer can't be empty; nothing draws from it
    culling_ = !r_.setMeshIndices(mesh_, frameIndices_);
}

WorldRenderer::Visibility WorldRenderer::visibility() const {
    Visibility v;
    v.cluster = viewCluster_ < -1 ? -1 : viewCluster_;
    v.culling = culling_;
    for (const SurfaceRange& r : surfaceRanges_)
        v.total += r.batch >= 0;
    v.potentiallyVisible = culling_ ? pvsSurfaces_.size() : v.total;
    v.visible = culling_ ? frameSurfaces_ : v.total;
    for (uint8_t byte : viewMask_)
        for (int bit = 0; bit < 8; ++bit)
            v.hiddenAreas += (byte >> bit) & 1;
    return v;
}

void WorldRenderer::drawModel(int model, const gfx::Mat4& transform, float time, float sortMin, float sortMax) const {
    for (const Batch& b : batches_) {
        if (b.model != model || b.sort < sortMin || b.sort > sortMax)
            continue;
        gfx::DrawCall base;
        base.mesh = mesh_;
        base.model = transform;
        base.firstIndex = b.firstIndex;
        base.indexCount = b.indexCount;
        shaders_.draw(b.material, base, b.lightmap >= 0 ? lightmaps_[size_t(b.lightmap)] : 0, time);
        if (b.fog >= 0)
            shaders_.drawFog(b.material, base, fogs_[size_t(b.fog)].color, fogs_[size_t(b.fog)].distance, time);
    }
}

} // namespace reluva::idtech3
