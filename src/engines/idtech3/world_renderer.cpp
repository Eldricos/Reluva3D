// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the original renderer interprets shaders (the defaults, sort order,
// implicit shaders, sky box layout and cloud texture coordinates) was read
// from the GPL Quake III Arena renderer (tr_shader.c, tr_shade_calc.c,
// tr_sky.c, tr_bsp.c); docs/formats/idtech3/shader.md describes it. This is
// new code written from that description.

#include "engines/idtech3/world_renderer.h"
#include "engines/idtech3/patch.h"

#include "common/fs.h"
#include "common/image.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace reluva::idtech3 {

namespace {

constexpr int kPatchLevel = 6;   // quads per Bezier block, each way
constexpr float kPi = 3.14159265358979f;

// Sort keys, as the original numbers them.
constexpr float kSortPortal = 1, kSortSky = 2, kSortOpaque = 3, kSortDecal = 4, kSortSeeThrough = 5,
                kSortBlend = 9;

gfx::Blend blendFactor(const std::string& gl) {
    static const std::pair<const char*, gfx::Blend> names[] = {
        {"gl_zero", gfx::Blend::Zero},
        {"gl_one", gfx::Blend::One},
        {"gl_src_color", gfx::Blend::SrcColor},
        {"gl_one_minus_src_color", gfx::Blend::OneMinusSrcColor},
        {"gl_dst_color", gfx::Blend::DstColor},
        {"gl_one_minus_dst_color", gfx::Blend::OneMinusDstColor},
        {"gl_src_alpha", gfx::Blend::SrcAlpha},
        {"gl_one_minus_src_alpha", gfx::Blend::OneMinusSrcAlpha},
        {"gl_dst_alpha", gfx::Blend::DstAlpha},
        {"gl_one_minus_dst_alpha", gfx::Blend::OneMinusDstAlpha},
    };
    for (const auto& [name, b] : names)
        if (gl == name)
            return b;
    return gfx::Blend::One; // gl_src_alpha_saturate isn't supported
}

bool stageBlends(const ShaderStage& s) {
    if (s.blendSrc.empty())
        return false;
    return !(blendFactor(s.blendSrc) == gfx::Blend::One && blendFactor(s.blendDst) == gfx::Blend::Zero);
}

// Light values are stored at half brightness and doubled on loading, the
// way the original does in a window: a component over 255 scales the
// whole colour down, keeping its hue.
void brighten(uint8_t* rgb) {
    int c[3] = {rgb[0] * 2, rgb[1] * 2, rgb[2] * 2};
    const int top = std::max({c[0], c[1], c[2]});
    for (int i = 0; i < 3; ++i)
        rgb[i] = static_cast<uint8_t>(top > 255 ? c[i] * 255 / top : c[i]);
}

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
    brighten(g.color);
    return g;
}

std::string stripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    const size_t slash = name.find_last_of("/\\");
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? name.substr(0, dot) : name;
}

// An affine map of texture coordinates: (u, v) -> (r0 . (u, v, 1), r1 . (u, v, 1)).
struct Affine {
    std::array<float, 3> r0{1, 0, 0}, r1{0, 1, 0};
};

// `op` applied after `m`.
Affine then(const Affine& m, const Affine& op) {
    Affine r;
    r.r0 = {op.r0[0] * m.r0[0] + op.r0[1] * m.r1[0], op.r0[0] * m.r0[1] + op.r0[1] * m.r1[1],
            op.r0[0] * m.r0[2] + op.r0[1] * m.r1[2] + op.r0[2]};
    r.r1 = {op.r1[0] * m.r0[0] + op.r1[1] * m.r1[0], op.r1[0] * m.r0[1] + op.r1[1] * m.r1[1],
            op.r1[0] * m.r0[2] + op.r1[1] * m.r1[2] + op.r1[2]};
    return r;
}

// One tcMod at `time` as an affine map (turb is handled by the caller).
Affine tcModMatrix(const ShaderStage::TcMod& m, float time) {
    using T = ShaderStage::TcMod::Type;
    Affine a;
    switch (m.type) {
    case T::Scroll: {
        // Only the fraction matters, and keeps precision over long times.
        const float s = m.v[0] * time, t = m.v[1] * time;
        a.r0[2] = s - std::floor(s);
        a.r1[2] = t - std::floor(t);
        break;
    }
    case T::Scale:
        a.r0[0] = m.v[0];
        a.r1[1] = m.v[1];
        break;
    case T::Rotate: {
        // Around the texture's centre; positive degrees turn it clockwise.
        const float rad = -m.v[0] * time * kPi / 180;
        const float c = std::cos(rad), s = std::sin(rad);
        a.r0 = {c, -s, 0.5f - 0.5f * c + 0.5f * s};
        a.r1 = {s, c, 0.5f - 0.5f * s - 0.5f * c};
        break;
    }
    case T::Stretch: {
        const float w = gfx::evaluate(m.wave, time);
        const float p = std::fabs(w) > 1e-4f ? 1.0f / w : 1e4f;
        a.r0 = {p, 0, 0.5f - 0.5f * p};
        a.r1 = {0, p, 0.5f - 0.5f * p};
        break;
    }
    case T::Transform:
        a.r0 = {m.v[0], m.v[2], m.v[4]};
        a.r1 = {m.v[1], m.v[3], m.v[5]};
        break;
    case T::Turb:
    case T::EntityTranslate:
        break;
    }
    return a;
}

gfx::Cull cullOf(ShaderDef::Cull c) {
    return c == ShaderDef::Cull::Front ? gfx::Cull::Front : c == ShaderDef::Cull::Back ? gfx::Cull::Back
                                                                                       : gfx::Cull::None;
}

} // namespace

void WorldRenderer::clear() {
    for (gfx::MeshId m : {mesh_, skyMesh_})
        if (m)
            r_.destroyMesh(m);
    mesh_ = skyMesh_ = 0;
    for (gfx::TextureId t : lightmaps_)
        r_.destroyTexture(t);
    lightmaps_.clear();
    for (auto& [name, t] : images_)
        if (t != missingTexture_)
            r_.destroyTexture(t);
    images_.clear();
    for (gfx::TextureId t : {missingTexture_, whiteTexture_})
        if (t)
            r_.destroyTexture(t);
    missingTexture_ = whiteTexture_ = 0;
    materials_.clear();
    materialIndex_.clear();
    implicit_.clear();
    fogs_.clear();
    batches_.clear();
    unsupported_.clear();
    skySides_ = {};
    skyMaterial_ = -1;
    missing_ = 0;
    triangles_ = 0;
}

gfx::TextureId WorldRenderer::image(const std::string& name, bool clamp, bool mipmaps, const SearchSet& files) {
    const std::string key =
        normalizeArchiveName(stripExtension(name)) + (clamp ? "|clamp" : "") + (mipmaps ? "" : "|nomip");
    if (auto it = images_.find(key); it != images_.end())
        return it->second;

    gfx::TextureId id = missingTexture_;
    std::vector<uint8_t> data;
    image::Image img;
    if (const std::string file = findImage(files, name);
        !file.empty() && !files.read(file, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        params.clamp = clamp;
        params.mipmaps = mipmaps;
        gfx::TextureId t = 0;
        if (!r_.createTexture(img.w, img.h, img.rgba.data(), params, t))
            id = t;
    }
    if (id == missingTexture_)
        ++missing_;
    images_[key] = id;
    return id;
}

WorldRenderer::Stage WorldRenderer::makeStage(const ShaderStage& def, const ShaderDef* shader,
                                              const SearchSet& files) {
    Stage st;
    st.def = &def;
    st.fps = def.animFps;
    if (def.lightmap) {
        st.lightmap = true;
    } else if (def.whiteImage || def.maps.empty()) {
        st.frames = {whiteTexture_};
    } else {
        const bool mipmaps = !(shader && shader->noMipmaps);
        for (const std::string& m : def.maps)
            st.frames.push_back(image(m, def.clamp, mipmaps, files));
    }

    const bool blends = stageBlends(def);
    if (blends) {
        st.state.src = blendFactor(def.blendSrc);
        st.state.dst = blendFactor(def.blendDst);
    }
    // Stages write depth unless they blend, or say depthWrite.
    st.state.depthWrite = !blends || def.depthWrite;
    st.state.depthEqual = def.depthEqual;
    st.state.alphaTest = def.alphaFunc;
    st.state.cull = shader ? cullOf(shader->cull) : gfx::Cull::Front;
    st.state.depthBias = shader && shader->polygonOffset;

    using T = ShaderStage::TcGen;
    switch (def.tcGen) {
    case T::Default: st.texGen = def.lightmap ? gfx::TexGen::Uv2 : gfx::TexGen::Uv; break;
    case T::Base: st.texGen = gfx::TexGen::Uv; break;
    case T::Lightmap: st.texGen = gfx::TexGen::Uv2; break;
    case T::Environment: st.texGen = gfx::TexGen::Environment; break;
    case T::Vector: st.texGen = gfx::TexGen::Vector; break;
    }
    return st;
}

WorldRenderer::Material WorldRenderer::makeMaterial(const std::string& name, bool lightmapped,
                                                    const SearchSet& files, const ShaderLibrary& shaders) {
    Material m;
    const ShaderDef* def = shaders.find(name);
    if (!def) {
        // The original's implicit shaders: the image times the lightmap, or
        // times the vertex colours on surfaces without one.
        ShaderDef& d = implicit_.emplace_back();
        d.name = name;
        if (lightmapped) {
            ShaderStage lm;
            lm.lightmap = true;
            lm.rgbGen = ShaderStage::RgbGen::Identity;
            ShaderStage tex;
            tex.maps = {name};
            tex.blendSrc = "gl_dst_color";
            tex.blendDst = "gl_zero";
            tex.rgbGen = ShaderStage::RgbGen::Identity;
            d.stages = {lm, tex};
        } else {
            ShaderStage tex;
            tex.maps = {name};
            tex.rgbGen = ShaderStage::RgbGen::ExactVertex;
            d.stages = {tex};
        }
        def = &d;
    }
    for (const std::string& u : def->unknown)
        note("unknown directive " + u);
    // Sky surfaces show the sky drawn behind everything; fog volumes'
    // surfaces only mark the volume.
    if (def->sky || def->fog || def->hasSurfaceParm("nodraw") || def->stages.empty()) {
        m.skip = true;
        return m;
    }
    if (def->portal)
        note("portal (drawn as a plain surface)");

    for (const ShaderStage& s : def->stages)
        m.stages.push_back(makeStage(s, def, files));

    size_t used = 0;
    for (const ShaderDeform& d : def->deforms) {
        using T = ShaderDeform::Type;
        gfx::Deform g;
        if (d.type == T::Wave) {
            g.type = gfx::Deform::Type::Wave;
            g.wave = d.wave;
            // Without a frequency the whole surface moves as one.
            g.spread = d.wave.frequency == 0 ? 0 : d.spread;
        } else if (d.type == T::Bulge) {
            g.type = gfx::Deform::Type::Bulge;
            g.width = d.width;
            g.height = d.height;
            g.speed = d.speed;
        } else if (d.type == T::Move) {
            g.type = gfx::Deform::Type::Move;
            g.wave = d.wave;
            g.vector = d.vector;
        } else if (d.type == T::Normal) {
            continue; // only changes lighting, which these surfaces don't compute
        } else {
            static const char* const names[] = {"wave", "normal", "bulge", "move", "autosprite", "autosprite2",
                                                "text", "projectionShadow", "?"};
            note(std::string("deformVertexes ") + names[size_t(d.type)]);
            continue;
        }
        if (used < m.deforms.size())
            m.deforms[used++] = g;
        else
            note("more than two deformVertexes");
    }

    // Sort order: as given, or as the original decides it.
    if (def->sort > 0) {
        m.sort = def->sort;
    } else if (def->portal) {
        m.sort = kSortPortal;
    } else if (def->polygonOffset) {
        m.sort = kSortDecal;
    } else {
        m.sort = kSortOpaque;
        if (stageBlends(def->stages.front()))
            for (const Stage& s : m.stages)
                if (stageBlends(*s.def)) {
                    m.sort = s.state.depthWrite ? kSortSeeThrough : kSortBlend;
                    break;
                }
    }
    return m;
}

int WorldRenderer::material(int shader, bool lightmapped, const Bsp& bsp, const SearchSet& files,
                            const ShaderLibrary& shaders) {
    const auto key = std::make_pair(shader, lightmapped);
    if (auto it = materialIndex_.find(key); it != materialIndex_.end())
        return it->second;
    materials_.push_back(makeMaterial(bsp.shaders[size_t(shader)].name, lightmapped, files, shaders));
    const int index = static_cast<int>(materials_.size()) - 1;
    materialIndex_[key] = index;
    return index;
}

void WorldRenderer::buildSky(const ShaderDef& sky, const SearchSet& files) {
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
                                               (x * x + y * y + z * z) * (2 * kWorldRadius * h + h * h))) /
                                (2 * len2);
                float c[3] = {x * p, y * p, z * p + kWorldRadius};
                const float cl = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
                v.uv2[0] = std::acos(c[0] / cl);
                v.uv2[1] = std::acos(c[1] / cl);
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
    cloudFirst_ = 0;
    cloudCount_ = skySideCount_ * 5; // every side but the bottom
    if (r_.createMesh(verts, indices, skyMesh_))
        return;
    if (!sky.skyFarBox.empty())
        for (int side = 0; side < 6; ++side)
            skySides_[size_t(side)] =
                image(sky.skyFarBox + "_" + kSuffix[kImageForSide[side]], true, !sky.noMipmaps, files);
}

Error WorldRenderer::load(const Bsp& bsp, const SearchSet& files, const ShaderLibrary& shaders) {
    clear();
    {
        uint8_t px[8 * 8 * 4];
        for (int i = 0; i < 64; ++i) {
            const uint8_t v = ((i % 8) / 4 + (i / 8) / 4) % 2 ? 150 : 110;
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
            px[i * 4 + 3] = 255;
        }
        gfx::TextureParams p;
        p.nearest = true;
        if (Error err = r_.createTexture(8, 8, px, p, missingTexture_))
            return err;
        const uint8_t white[4] = {255, 255, 255, 255};
        p.mipmaps = false;
        if (Error err = r_.createTexture(1, 1, white, p, whiteTexture_))
            return err;
    }

    constexpr size_t kLightmapPixels = size_t(kLightmapSize) * kLightmapSize;
    std::vector<uint8_t> rgba(kLightmapPixels * 4);
    for (size_t l = 0; l < bsp.lightmapCount(); ++l) {
        const uint8_t* src = bsp.lightmaps.data() + l * kLightmapPixels * 3;
        for (size_t i = 0; i < kLightmapPixels; ++i) {
            std::copy_n(src + i * 3, 3, &rgba[i * 4]);
            brighten(&rgba[i * 4]);
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
        if (const ShaderDef* def = shaders.find(f.shader); def && def->fog) {
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
            // Triangle soups are always lit by their vertices.
            const bool lightmapped = s.lightmap >= 0 && s.type != SurfaceType::TriangleSoup;
            const int mi = material(s.shader, lightmapped, bsp, files, shaders);
            const Material& mat = materials_[size_t(mi)];
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
        batches_.push_back({std::get<1>(key), std::get<2>(key), std::get<3>(key), uint32_t(indices.size()),
                            uint32_t(list.size())});
        indices.insert(indices.end(), list.begin(), list.end());
    }
    triangles_ = indices.size() / 3;

    // The sky, from the first sky shader the map uses.
    for (const BspShader& s : bsp.shaders)
        if (const ShaderDef* def = shaders.find(s.name); def && def->sky) {
            buildSky(*def, files);
            Material m;
            for (const ShaderStage& st : def->stages)
                m.stages.push_back(makeStage(st, def, files));
            m.sort = kSortSky;
            materials_.push_back(std::move(m));
            skyMaterial_ = static_cast<int>(materials_.size()) - 1;
            break;
        }

    if (indices.empty())
        return Error::fail("The map has nothing to draw");
    return r_.createMesh(vertices, indices, mesh_);
}

void WorldRenderer::drawStages(const Material& m, gfx::MeshId mesh, uint32_t first, uint32_t count, int lightmap,
                               bool sky, float time) const {
    for (const Stage& st : m.stages) {
        const ShaderStage& d = *st.def;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.firstIndex = first;
        call.indexCount = count;
        call.time = time;
        call.deforms = m.deforms;
        call.state = st.state;
        if (st.lightmap)
            call.texture = lightmap >= 0 ? lightmaps_[size_t(lightmap)] : whiteTexture_;
        else if (st.fps > 0 && st.frames.size() > 1)
            call.texture = st.frames[size_t(std::fmod(std::floor(time * st.fps), float(st.frames.size())))];
        else
            call.texture = st.frames.front();

        call.texGen = st.texGen;
        call.texVector0 = d.tcVectors[0];
        call.texVector1 = d.tcVectors[1];
        // tcMods in order; a turb splits them into before and after.
        Affine pre, post;
        bool turbSeen = false;
        for (const ShaderStage::TcMod& mod : d.tcMods) {
            if (mod.type == ShaderStage::TcMod::Type::Turb && !turbSeen) {
                turbSeen = true;
                call.turbAmplitude = mod.wave.amplitude;
                call.turbPhase = mod.wave.phase + time * mod.wave.frequency;
                continue;
            }
            Affine& target = turbSeen ? post : pre;
            target = then(target, tcModMatrix(mod, time));
        }
        call.preRow0 = pre.r0;
        call.preRow1 = pre.r1;
        call.postRow0 = post.r0;
        call.postRow1 = post.r1;

        using R = ShaderStage::RgbGen;
        switch (d.rgbGen) {
        case R::Vertex:
        case R::ExactVertex:
        case R::LightingDiffuse: // the vertex colours stand in for the light grid
            call.rgbSource = gfx::ColorSource::Vertex;
            break;
        case R::OneMinusVertex: call.rgbSource = gfx::ColorSource::OneMinusVertex; break;
        case R::Wave: {
            const float v = std::clamp(gfx::evaluate(d.rgbWave, time), 0.0f, 1.0f);
            call.color = {v, v, v, 1};
            break;
        }
        case R::Const: call.color = {d.rgbConst[0], d.rgbConst[1], d.rgbConst[2], 1}; break;
        case R::OneMinusEntity: call.color = {0, 0, 0, 1}; break;
        default: break; // identity: white
        }
        using A = ShaderStage::AlphaGen;
        switch (d.alphaGen) {
        case A::Vertex: call.alphaSource = gfx::ColorSource::Vertex; break;
        case A::OneMinusVertex: call.alphaSource = gfx::ColorSource::OneMinusVertex; break;
        case A::Wave: call.color[3] = std::clamp(gfx::evaluate(d.alphaWave, time), 0.0f, 1.0f); break;
        case A::Const: call.color[3] = d.alphaConst; break;
        case A::OneMinusEntity: call.color[3] = 0; break;
        default: break; // identity, entity, lightingSpecular, portal: opaque
        }

        if (sky) {
            call.followCamera = true;
            call.texGen = gfx::TexGen::Uv2; // the cloud coordinates
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
            call.deforms = {};
        }
        r_.draw(call);
    }
}

void WorldRenderer::draw(float time) const {
    // The sky first, around the camera; everything else covers it.
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
        if (skyMaterial_ >= 0)
            drawStages(materials_[size_t(skyMaterial_)], skyMesh_, cloudFirst_, cloudCount_, -1, true, time);
    }

    for (const Batch& b : batches_) {
        const Material& m = materials_[size_t(b.material)];
        drawStages(m, mesh_, b.firstIndex, b.indexCount, b.lightmap, false, time);
        // Fog over fogged surfaces, by distance from the camera.
        if (b.fog >= 0 && fogs_[size_t(b.fog)].distance > 0 && !m.stages.empty()) {
            const Fog& f = fogs_[size_t(b.fog)];
            gfx::DrawCall call;
            call.mesh = mesh_;
            call.firstIndex = b.firstIndex;
            call.indexCount = b.indexCount;
            call.texture = whiteTexture_;
            call.color = {f.color[0], f.color[1], f.color[2], 1};
            call.fogDistance = f.distance;
            call.time = time;
            call.deforms = m.deforms;
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::OneMinusSrcAlpha;
            call.state.depthWrite = false;
            call.state.depthEqual = m.sort <= kSortOpaque;
            call.state.cull = m.stages.front().state.cull;
            call.state.depthBias = m.stages.front().state.depthBias;
            r_.draw(call);
        }
    }
}

} // namespace reluva::idtech3
