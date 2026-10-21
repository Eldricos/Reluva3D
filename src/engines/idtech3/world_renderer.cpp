// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/world_renderer.h"
#include "engines/idtech3/patch.h"

#include "common/fs.h"
#include "common/image.h"

#include <algorithm>
#include <map>
#include <utility>

namespace reluva::idtech3 {

namespace {

// How finely patches are cut: quads per Bezier block, each way.
constexpr int kPatchLevel = 6;

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
    return gfx::Blend::One; // e.g. gl_src_alpha_saturate, not supported yet
}

const Directive* findDirective(const std::vector<Directive>& list, const char* keyword) {
    for (const Directive& d : list)
        if (d.keyword == keyword)
            return &d;
    return nullptr;
}

gfx::Vertex toGfx(const BspVertex& v) {
    gfx::Vertex g;
    for (int i = 0; i < 3; ++i)
        g.pos[i] = v.pos[size_t(i)];
    for (int i = 0; i < 2; ++i) {
        g.uv[i] = v.st[size_t(i)];
        g.uv2[i] = v.lightmap[size_t(i)];
    }
    for (int i = 0; i < 4; ++i)
        g.color[i] = v.color[size_t(i)];
    return g;
}

// "textures/borg/floor.tga" -> "textures/borg/floor"
std::string stripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    const size_t slash = name.find_last_of("/\\");
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? name.substr(0, dot) : name;
}

} // namespace

void WorldRenderer::clear() {
    if (mesh_)
        r_.destroyMesh(mesh_);
    mesh_ = 0;
    for (gfx::TextureId t : lightmaps_)
        r_.destroyTexture(t);
    lightmaps_.clear();
    for (auto& [name, t] : images_)
        if (t != missingTexture_)
            r_.destroyTexture(t);
    images_.clear();
    if (missingTexture_)
        r_.destroyTexture(missingTexture_);
    missingTexture_ = 0;
    materials_.clear();
    batches_.clear();
    missing_ = 0;
    triangles_ = 0;
}

gfx::TextureId WorldRenderer::image(const std::string& name, bool clamp, const SearchSet& files) {
    const std::string key = normalizeArchiveName(stripExtension(name)) + (clamp ? "|clamp" : "");
    if (auto it = images_.find(key); it != images_.end())
        return it->second;

    gfx::TextureId id = missingTexture_;
    std::vector<uint8_t> data;
    image::Image img;
    if (const std::string file = findImage(files, name);
        !file.empty() && !files.read(file, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        params.clamp = clamp;
        gfx::TextureId t = 0;
        if (!r_.createTexture(img.w, img.h, img.rgba.data(), params, t))
            id = t;
    }
    if (id == missingTexture_)
        ++missing_;
    images_[key] = id;
    return id;
}

WorldRenderer::Material WorldRenderer::makeMaterial(const std::string& shaderName, const SearchSet& files,
                                                    const ShaderLibrary& shaders) {
    Material m;
    const ShaderDef* def = shaders.find(shaderName);
    if (!def) {
        // No script: the image of the same name, lightmapped.
        m.texture = image(shaderName, false, files);
        return m;
    }
    if (def->sky || def->hasSurfaceParm("nodraw") || def->stages.empty()) {
        m.skip = true;
        return m;
    }
    if (const Directive* cull = findDirective(def->directives, "cull"); cull && !cull->args.empty()) {
        const std::string mode = fs::toLower(cull->args[0]);
        if (mode == "none" || mode == "disable" || mode == "twosided")
            m.cull = gfx::Cull::None;
        else if (mode == "back" || mode == "backside" || mode == "backsided")
            m.cull = gfx::Cull::Back;
    }

    m.lightmapped = std::any_of(def->stages.begin(), def->stages.end(), [](const ShaderStage& s) { return s.lightmap; });
    const ShaderStage* imageStage = nullptr;
    for (const ShaderStage& s : def->stages)
        if (!s.lightmap && !s.maps.empty()) {
            imageStage = &s;
            break;
        }
    if (imageStage) {
        m.texture = image(imageStage->maps.front(), imageStage->clamp, files);
        if (const Directive* af = findDirective(imageStage->directives, "alphafunc"); af && !af->args.empty()) {
            const std::string f = fs::toLower(af->args[0]);
            m.alphaTest = f == "gt0" ? gfx::AlphaTest::Gt0 : f == "lt128" ? gfx::AlphaTest::Lt128
                                                         : f == "ge128" ? gfx::AlphaTest::Ge128
                                                                        : gfx::AlphaTest::None;
        }
    }

    // A first stage that blends with what's behind makes the whole shader
    // translucent (glass, effects); lightmaps don't apply.
    const ShaderStage& first = def->stages.front();
    if (!first.lightmap && !first.blendSrc.empty()) {
        const gfx::Blend src = blendFactor(first.blendSrc), dst = blendFactor(first.blendDst);
        if (!(src == gfx::Blend::One && dst == gfx::Blend::Zero)) {
            m.translucent = true;
            m.src = src;
            m.dst = dst;
            m.depthWrite = findDirective(first.directives, "depthwrite") != nullptr;
            m.lightmapped = false;
        }
    }
    return m;
}

Error WorldRenderer::load(const Bsp& bsp, const SearchSet& files, const ShaderLibrary& shaders) {
    clear();
    {
        // Grey checks for images that can't be found.
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
    }

    constexpr size_t kLightmapPixels = size_t(kLightmapSize) * kLightmapSize;
    std::vector<uint8_t> rgba(kLightmapPixels * 4);
    for (size_t l = 0; l < bsp.lightmapCount(); ++l) {
        const uint8_t* src = bsp.lightmaps.data() + l * kLightmapPixels * 3;
        for (size_t i = 0; i < kLightmapPixels; ++i) {
            rgba[i * 4] = src[i * 3];
            rgba[i * 4 + 1] = src[i * 3 + 1];
            rgba[i * 4 + 2] = src[i * 3 + 2];
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

    for (const BspShader& s : bsp.shaders)
        materials_.push_back(makeMaterial(s.name, files, shaders));

    // Every surface's triangles, grouped by what they're drawn with.
    std::vector<gfx::Vertex> vertices;
    std::map<std::pair<int, int>, std::vector<uint32_t>> groups; // (material, lightmap) -> indices
    std::vector<BspVertex> patchVerts;
    std::vector<uint32_t> patchIndices;
    for (const BspModel& model : bsp.models)
        for (int si = model.firstSurface; si < model.firstSurface + model.numSurfaces; ++si) {
            const BspSurface& s = bsp.surfaces[size_t(si)];
            const Material& mat = materials_[size_t(s.shader)];
            if (mat.skip)
                continue;
            const int lightmap = mat.lightmapped && s.lightmap >= 0 ? s.lightmap : -1;
            std::vector<uint32_t>& out = groups[{s.shader, lightmap}];
            const uint32_t base = uint32_t(vertices.size());
            const BspVertex* v = &bsp.vertices[size_t(s.firstVertex)];
            if (s.type == SurfaceType::Planar || s.type == SurfaceType::TriangleSoup) {
                for (int i = 0; i < s.numVertices; ++i)
                    vertices.push_back(toGfx(v[i]));
                for (int i = 0; i + 2 < s.numIndices; i += 3)
                    for (int k = 0; k < 3; ++k)
                        out.push_back(base + uint32_t(bsp.indices[size_t(s.firstIndex + i + k)]));
            } else if (s.type == SurfaceType::Patch) {
                patchVerts.clear();
                patchIndices.clear();
                tessellatePatch(v, s.patchWidth, s.patchHeight, kPatchLevel, patchVerts, patchIndices);
                for (const BspVertex& pv : patchVerts)
                    vertices.push_back(toGfx(pv));
                for (uint32_t i : patchIndices)
                    out.push_back(base + i);
            }
        }

    // Opaque batches first, then translucent ones, which blend over them.
    std::vector<uint32_t> indices;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& [key, list] : groups) {
            if (list.empty() || materials_[size_t(key.first)].translucent != (pass == 1))
                continue;
            batches_.push_back({key.first, key.second, uint32_t(indices.size()), uint32_t(list.size())});
            indices.insert(indices.end(), list.begin(), list.end());
        }
    triangles_ = indices.size() / 3;
    if (indices.empty())
        return Error::fail("The map has nothing to draw");
    return r_.createMesh(vertices, indices, mesh_);
}

void WorldRenderer::draw() const {
    for (const Batch& b : batches_) {
        const Material& m = materials_[size_t(b.material)];
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.texture = m.texture;
        call.state.src = m.src;
        call.state.dst = m.dst;
        call.state.depthWrite = m.depthWrite;
        call.state.cull = m.cull;
        call.state.alphaTest = m.alphaTest;
        // Lit surfaces without a lightmap carry their light in their vertex
        // colours. Light values are stored at half brightness, as in the
        // original's windowed mode: doubled here and in the lightmap pass.
        if (m.lightmapped && b.lightmap < 0) {
            call.useVertexColor = true;
            call.color = {2, 2, 2, 1};
        }
        r_.draw(call);

        if (b.lightmap >= 0) {
            gfx::DrawCall lm = call;
            lm.texture = lightmaps_[size_t(b.lightmap)];
            lm.useUv2 = true;
            lm.useVertexColor = false;
            lm.color = {1, 1, 1, 1};
            // result = dst * src + src * dst: twice the product.
            lm.state.src = gfx::Blend::DstColor;
            lm.state.dst = gfx::Blend::SrcColor;
            lm.state.depthEqual = true;
            lm.state.depthWrite = false;
            lm.state.alphaTest = gfx::AlphaTest::None;
            r_.draw(lm);
        }
    }
}

} // namespace reluva::idtech3
