// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the original renderer interprets shaders (defaults, sort order,
// implicit shaders) was read from the GPL Quake III Arena renderer
// (tr_shader.c, tr_shade_calc.c); docs/engines/idtech3/formats/shader.md
// describes it. This is new code written from that description.

#include "engines/idtech3/render/shader_cache.h"

#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reluva::idtech3 {

namespace {

constexpr float kPi = 3.14159265358979f;

// Sort keys, as the original numbers them.
constexpr float kSortPortal = 1;
constexpr float kSortOpaque = 3;
constexpr float kSortDecal = 4;
constexpr float kSortSeeThrough = 5;
constexpr float kSortBlend = 9;

gfx::Blend blendFactor(const std::string &gl) {
    static const std::pair<const char *, gfx::Blend> kNames[] = {
        {"gl_zero", gfx::Blend::kZero},          {"gl_one", gfx::Blend::kOne},
        {"gl_src_color", gfx::Blend::kSrcColor}, {"gl_one_minus_src_color", gfx::Blend::kOneMinusSrcColor},
        {"gl_dst_color", gfx::Blend::kDstColor}, {"gl_one_minus_dst_color", gfx::Blend::kOneMinusDstColor},
        {"gl_src_alpha", gfx::Blend::kSrcAlpha}, {"gl_one_minus_src_alpha", gfx::Blend::kOneMinusSrcAlpha},
        {"gl_dst_alpha", gfx::Blend::kDstAlpha}, {"gl_one_minus_dst_alpha", gfx::Blend::kOneMinusDstAlpha},
    };
    for (const auto &[name, b] : kNames)
        if (gl == name)
            return b;
    return gfx::Blend::kOne; // gl_src_alpha_saturate isn't supported
}

bool stageBlends(const ShaderStage &s) {
    if (s.blendSrc.empty())
        return false;
    return !(blendFactor(s.blendSrc) == gfx::Blend::kOne && blendFactor(s.blendDst) == gfx::Blend::kZero);
}

std::string stripExtension(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    const size_t slash = name.find_last_of("/\\");
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? name.substr(0, dot) : name;
}

// An affine map of texture coordinates: (u, v) -> (r0 . (u, v, 1), r1 . (u, v, 1)).
struct Affine {
    std::array<float, 3> r0{1, 0, 0};
    std::array<float, 3> r1{0, 1, 0};
};

// `op` applied after `m`.
Affine then(const Affine &m, const Affine &op) {
    Affine r;
    r.r0 = {op.r0[0] * m.r0[0] + op.r0[1] * m.r1[0], op.r0[0] * m.r0[1] + op.r0[1] * m.r1[1],
            op.r0[0] * m.r0[2] + op.r0[1] * m.r1[2] + op.r0[2]};
    r.r1 = {op.r1[0] * m.r0[0] + op.r1[1] * m.r1[0], op.r1[0] * m.r0[1] + op.r1[1] * m.r1[1],
            op.r1[0] * m.r0[2] + op.r1[1] * m.r1[2] + op.r1[2]};
    return r;
}

// One tcMod at `time` as an affine map (turb is handled by the caller).
Affine tcModMatrix(const ShaderStage::TcMod &m, float time) {
    using T = ShaderStage::TcMod::Type;
    Affine a;
    switch (m.type) {
    case T::kScroll: {
        // Only the fraction matters, and keeps precision over long times.
        const float s = m.v[0] * time;
        const float t = m.v[1] * time;
        a.r0[2] = s - std::floor(s);
        a.r1[2] = t - std::floor(t);
        break;
    }
    case T::kScale:
        a.r0[0] = m.v[0];
        a.r1[1] = m.v[1];
        break;
    case T::kRotate: {
        // Around the texture's centre; positive degrees turn it clockwise.
        const float rad = -m.v[0] * time * kPi / 180;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        a.r0 = {c, -s, 0.5f - 0.5f * c + 0.5f * s};
        a.r1 = {s, c, 0.5f - 0.5f * s - 0.5f * c};
        break;
    }
    case T::kStretch: {
        const float w = gfx::evaluate(m.wave, time);
        const float p = std::fabs(w) > 1e-4f ? 1.0f / w : 1e4f;
        a.r0 = {p, 0, 0.5f - 0.5f * p};
        a.r1 = {0, p, 0.5f - 0.5f * p};
        break;
    }
    case T::kTransform:
        a.r0 = {m.v[0], m.v[2], m.v[4]};
        a.r1 = {m.v[1], m.v[3], m.v[5]};
        break;
    case T::kTurb:
    case T::kEntityTranslate:
        break;
    }
    return a;
}

gfx::Cull cullOf(ShaderDef::Cull c) {
    return c == ShaderDef::Cull::kFront  ? gfx::Cull::kFront
           : c == ShaderDef::Cull::kBack ? gfx::Cull::kBack
                                         : gfx::Cull::kNone;
}

} // namespace

ShaderCache::ShaderCache(gfx::Renderer3D &renderer, const SearchSet &files, const ShaderLibrary &shaders)
    : _r(renderer), _files(files), _shaders(shaders) {
}

void ShaderCache::clear() {
    for (auto &[name, t] : _images)
        if (t != _missingTexture)
            _r.destroyTexture(t);
    _images.clear();
    for (gfx::TextureId t : {_missingTexture, _whiteTexture})
        if (t)
            _r.destroyTexture(t);
    _missingTexture = _whiteTexture = 0;
    _materials.clear();
    _byName.clear();
    _implicit.clear();
    _unsupported.clear();
    _missing = 0;
}

gfx::TextureId ShaderCache::white() {
    if (!_whiteTexture) {
        const uint8_t px[4] = {255, 255, 255, 255};
        gfx::TextureParams p;
        p.mipmaps = false;
        _r.createTexture(1, 1, px, p, _whiteTexture);
    }
    return _whiteTexture;
}

gfx::TextureId ShaderCache::image(const std::string &name, bool clamp, bool mipmaps, bool picmip) {
    const std::string key = normalizeArchiveName(stripExtension(name)) + (clamp ? "|clamp" : "") +
                            (mipmaps ? "" : "|nomip") + (picmip ? "|picmip" : "");
    if (auto it = _images.find(key); it != _images.end())
        return it->second;

    if (!_missingTexture) {
        // Grey checks for images that can't be found.
        uint8_t px[8 * 8 * 4];
        for (int i = 0; i < 64; ++i) {
            const uint8_t v = ((i % 8) / 4 + (i / 8) / 4) % 2 ? 150 : 110;
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
            px[i * 4 + 3] = 255;
        }
        gfx::TextureParams p;
        p.nearest = true;
        _r.createTexture(8, 8, px, p, _missingTexture);
    }
    gfx::TextureId id = _missingTexture;
    std::vector<uint8_t> data;
    image::Image img;
    if (const std::string file = findImage(_files, name);
        !file.empty() && !_files.read(file, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        params.clamp = clamp;
        params.mipmaps = mipmaps;
        if (mipmaps) {
            // The settings' filter, for textures in the world.
            params.mipmaps = _textureOptions.mipmaps;
            params.nearest = _textureOptions.nearest;
            params.mipNearest = _textureOptions.mipNearest;
            params.anisotropic = _textureOptions.anisotropic;
        }
        // Texture detail: halved (each 2x2 pixels averaged), down to a
        // pixel at least.
        for (int i = 0; picmip && i < _textureOptions.picmip && (img.w > 1 || img.h > 1); ++i) {
            const int w = std::max(1, img.w / 2);
            const int h = std::max(1, img.h / 2);
            std::vector<uint8_t> half(size_t(w) * size_t(h) * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 4; ++c) {
                        int sum = 0;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx) {
                                const int sx = std::min(img.w - 1, x * 2 + dx);
                                const int sy = std::min(img.h - 1, y * 2 + dy);
                                sum += img.rgba[(size_t(sy) * size_t(img.w) + size_t(sx)) * 4 + size_t(c)];
                            }
                        half[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = uint8_t((sum + 2) / 4);
                    }
            img.rgba = std::move(half);
            img.w = w;
            img.h = h;
        }
        gfx::TextureId t = 0;
        if (!_r.createTexture(img.w, img.h, img.rgba.data(), params, t))
            id = t;
    }
    if (id == _missingTexture)
        ++_missing;
    _images[key] = id;
    return id;
}

ShaderCache::Stage ShaderCache::makeStage(const ShaderStage &def, const ShaderDef &shader) {
    Stage st;
    st.def = &def;
    st.fps = def.animFps;
    if (def.lightmap) {
        st.lightmap = true;
    } else if (def.whiteImage || def.maps.empty()) {
        st.frames = {white()};
    } else {
        for (const std::string &m : def.maps)
            st.frames.push_back(image(m, def.clamp, !shader.noMipmaps, !shader.noPicmip));
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
    st.state.cull = cullOf(shader.cull);
    st.state.depthBias = shader.polygonOffset;

    using T = ShaderStage::TcGen;
    switch (def.tcGen) {
    case T::kDefault:
        st.texGen = def.lightmap ? gfx::TexGen::kUv2 : gfx::TexGen::kUv;
        break;
    case T::kBase:
        st.texGen = gfx::TexGen::kUv;
        break;
    case T::kLightmap:
        st.texGen = gfx::TexGen::kUv2;
        break;
    case T::kEnvironment:
        st.texGen = gfx::TexGen::kEnvironment;
        break;
    case T::kVector:
        st.texGen = gfx::TexGen::kVector;
        break;
    }
    return st;
}

ShaderCache::Material ShaderCache::makeMaterial(const ShaderDef &def, Lighting lighting, bool sky) {
    Material m;
    m.lighting = lighting;
    for (const std::string &u : def.unknown)
        note("unknown directive " + u);
    // Sky surfaces show the sky drawn behind everything; fog volumes'
    // surfaces only mark the volume.
    if ((def.sky && !sky) || def.fog || def.hasSurfaceParm("nodraw") || def.stages.empty()) {
        m.skip = true;
        return m;
    }
    // A mirror or portal: the view through it is drawn first (SceneDrawer),
    // this over it; how far off it's drawn, the alphaGen portal range.
    if (def.portal) {
        m.portal = true;
        for (const ShaderStage &s : def.stages)
            if (s.alphaGen == ShaderStage::AlphaGen::kPortal)
                m.portalRange = std::max(m.portalRange, s.portalRange);
    }

    for (const ShaderStage &s : def.stages)
        m.stages.push_back(makeStage(s, def));

    size_t used = 0;
    for (const ShaderDeform &d : def.deforms) {
        using T = ShaderDeform::Type;
        gfx::Deform g;
        if (d.type == T::kWave) {
            g.type = gfx::Deform::Type::kWave;
            g.wave = d.wave;
            // Without a frequency the whole surface moves as one.
            g.spread = d.wave.frequency == 0 ? 0 : d.spread;
        } else if (d.type == T::kBulge) {
            g.type = gfx::Deform::Type::kBulge;
            g.width = d.width;
            g.height = d.height;
            g.speed = d.speed;
        } else if (d.type == T::kMove) {
            g.type = gfx::Deform::Type::kMove;
            g.wave = d.wave;
            g.vector = d.vector;
        } else if (d.type == T::kNormal) {
            g.type = gfx::Deform::Type::kNormal;
            g.wave = d.wave; // amplitude and frequency
            if (d.wave.amplitude == 0)
                continue;
        } else if (d.type == T::kAutosprite || d.type == T::kAutosprite2) {
            // Quads turned to face the camera, each frame (WorldRenderer).
            m.autosprite = d.type == T::kAutosprite ? 1 : 2;
            continue;
        } else {
            static const char *const kNames[] = {
                "wave", "normal", "bulge", "move", "autosprite", "autosprite2", "text", "projectionShadow", "?"};
            note(std::string("deformVertexes ") + kNames[size_t(d.type)]);
            continue;
        }
        if (used < m.deforms.size())
            m.deforms[used++] = g;
        else
            note("more than two deformVertexes");
    }

    // Sort order: as given, or as the original decides it.
    if (def.sort > 0) {
        m.sort = def.sort;
    } else if (def.portal) {
        m.sort = kSortPortal;
    } else if (def.polygonOffset) {
        m.sort = kSortDecal;
    } else {
        m.sort = kSortOpaque;
        if (stageBlends(def.stages.front()))
            for (const Stage &s : m.stages)
                if (stageBlends(*s.def)) {
                    m.sort = s.state.depthWrite ? kSortSeeThrough : kSortBlend;
                    break;
                }
    }
    return m;
}

int ShaderCache::material(const ShaderDef &def, Lighting lighting, bool sky) {
    if (lighting == Lighting::kTwoD && !(def.noMipmaps && def.noPicmip)) {
        // Pictures (menus, the HUD) are registered without mipmaps, so the
        // texture detail and filter settings leave them alone.
        // The copy is kept: the material's stages point into it.
        ShaderDef &flat = _implicit.emplace_back(def);
        flat.noMipmaps = flat.noPicmip = true;
        _materials.push_back(makeMaterial(flat, lighting, sky));
        return static_cast<int>(_materials.size()) - 1;
    }
    _materials.push_back(makeMaterial(def, lighting, sky));
    return static_cast<int>(_materials.size()) - 1;
}

int ShaderCache::material(const std::string &name, Lighting lighting) {
    const auto key = std::make_pair(normalizeArchiveName(name), lighting);
    if (auto it = _byName.find(key); it != _byName.end())
        return it->second;
    const ShaderDef *def = _shaders.find(name);
    if (!def) {
        // The original's implicit shaders: the image times the lightmap,
        // the vertex colours, or the light.
        ShaderDef &d = _implicit.emplace_back();
        d.name = name;
        ShaderStage tex;
        tex.maps = {name};
        if (lighting == Lighting::kLightmap) {
            ShaderStage lm;
            lm.lightmap = true;
            lm.rgbGen = ShaderStage::RgbGen::kIdentity;
            tex.blendSrc = "gl_dst_color";
            tex.blendDst = "gl_zero";
            tex.rgbGen = ShaderStage::RgbGen::kIdentity;
            d.stages = {lm, tex};
        } else if (lighting == Lighting::kTwoD) {
            // Pictures: blended by their alpha, coloured by the vertices,
            // the image clamped and without mipmaps.
            tex.blendSrc = "gl_src_alpha";
            tex.blendDst = "gl_one_minus_src_alpha";
            tex.rgbGen = ShaderStage::RgbGen::kVertex;
            tex.alphaGen = ShaderStage::AlphaGen::kVertex;
            tex.clamp = true;
            d.noMipmaps = true;
            d.stages = {tex};
        } else if (lighting == Lighting::kEffect) {
            // An effect's image: coloured and blended by the entity's colour.
            tex.blendSrc = "gl_src_alpha";
            tex.blendDst = "gl_one_minus_src_alpha";
            tex.rgbGen = ShaderStage::RgbGen::kVertex;
            tex.alphaGen = ShaderStage::AlphaGen::kVertex;
            d.stages = {tex};
        } else {
            tex.rgbGen = lighting == Lighting::kVertex ? ShaderStage::RgbGen::kExactVertex
                                                       : ShaderStage::RgbGen::kLightingDiffuse;
            d.stages = {tex};
        }
        def = &d;
    }
    const int index = material(*def, lighting);
    _byName[key] = index;
    return index;
}

void ShaderCache::draw(int material, const gfx::DrawCall &base, gfx::TextureId lightmap, float time, bool sky,
                       const std::array<float, 4> *entity) const {
    const std::array<float, 4> ent = entity ? *entity : std::array<float, 4>{1, 1, 1, 1};
    const Material &m = _materials[size_t(material)];
    for (const Stage &st : m.stages) {
        const ShaderStage &d = *st.def;
        gfx::DrawCall call = base;
        call.time = time;
        call.deforms = m.deforms;
        call.state = st.state;
        if (st.lightmap)
            call.texture = lightmap ? lightmap : _whiteTexture;
        else if (st.fps > 0 && st.frames.size() > 1)
            call.texture = st.frames[size_t(std::fmod(std::floor(time * st.fps), float(st.frames.size())))];
        else
            call.texture = st.frames.front();

        call.texGen = st.texGen;
        call.texVector0 = d.tcVectors[0];
        call.texVector1 = d.tcVectors[1];
        // tcMods in order; a turb splits them into before and after.
        Affine pre;
        Affine post;
        bool turbSeen = false;
        for (const ShaderStage::TcMod &mod : d.tcMods) {
            if (mod.type == ShaderStage::TcMod::Type::kTurb && !turbSeen) {
                turbSeen = true;
                call.turbAmplitude = mod.wave.amplitude;
                call.turbPhase = mod.wave.phase + time * mod.wave.frequency;
                continue;
            }
            Affine &target = turbSeen ? post : pre;
            target = then(target, tcModMatrix(mod, time));
        }
        call.preRow0 = pre.r0;
        call.preRow1 = pre.r1;
        call.postRow0 = post.r0;
        call.postRow1 = post.r1;

        // Models have no vertex colours: whatever asks for light gets the light.
        const gfx::ColorSource lit =
            m.lighting == Lighting::kDiffuse ? gfx::ColorSource::kLighting : gfx::ColorSource::kVertex;
        using R = ShaderStage::RgbGen;
        call.rgbSource = gfx::ColorSource::kConstant;
        call.alphaSource = gfx::ColorSource::kConstant;
        call.color = {1, 1, 1, 1};
        switch (d.rgbGen) {
        case R::kVertex:
        case R::kExactVertex:
        case R::kLightingDiffuse:
            call.rgbSource = lit;
            break;
        case R::kOneMinusVertex:
            call.rgbSource = gfx::ColorSource::kOneMinusVertex;
            break;
        case R::kWave: {
            const float v = std::clamp(gfx::evaluate(d.rgbWave, time), 0.0f, 1.0f);
            call.color = {v, v, v, 1};
            break;
        }
        case R::kConst:
            call.color = {d.rgbConst[0], d.rgbConst[1], d.rgbConst[2], 1};
            break;
        case R::kEntity:
            call.color = {ent[0], ent[1], ent[2], 1};
            break;
        case R::kOneMinusEntity:
            call.color = {1 - ent[0], 1 - ent[1], 1 - ent[2], 1};
            break;
        default:
            break; // identity: white
        }
        // Lit by its vertices (no lightmap, or the vertex lighting
        // setting): a lightmap stage is the vertex colours instead.
        if (st.lightmap && !lightmap && m.lighting == Lighting::kVertex)
            call.rgbSource = gfx::ColorSource::kVertex;
        using A = ShaderStage::AlphaGen;
        switch (d.alphaGen) {
        case A::kVertex:
            if (m.lighting != Lighting::kDiffuse) // models: opaque
                call.alphaSource = gfx::ColorSource::kVertex;
            break;
        case A::kOneMinusVertex:
            call.alphaSource = gfx::ColorSource::kOneMinusVertex;
            break;
        case A::kWave:
            call.color[3] = std::clamp(gfx::evaluate(d.alphaWave, time), 0.0f, 1.0f);
            break;
        case A::kConst:
            call.color[3] = d.alphaConst;
            break;
        case A::kEntity:
            call.color[3] = ent[3];
            break;
        case A::kOneMinusEntity:
            call.color[3] = 1 - ent[3];
            break;
        case A::kPortal:
            call.fogDistance = d.portalRange;
            break; // see-through near, the stage's own far off
        default:
            break; // identity, lightingSpecular: opaque
        }

        if (m.lighting == Lighting::kTwoD) {
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::kNone;
        }
        if (m.lighting == Lighting::kEffect)
            call.state.cull = gfx::Cull::kNone;
        if (sky) {
            call.followCamera = true;
            call.texGen = gfx::TexGen::kUv2; // the cloud coordinates
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::kNone;
            call.deforms = {};
        }
        _r.draw(call);
    }
}

void ShaderCache::drawFog(int material, const gfx::DrawCall &base, const std::array<float, 3> &color, float distance,
                          const std::array<float, 4> &plane, float time) const {
    const Material &m = _materials[size_t(material)];
    if (m.stages.empty() || distance <= 0)
        return;
    gfx::DrawCall call = base;
    call.texture = _whiteTexture;
    call.texGen = gfx::TexGen::kUv;
    call.rgbSource = call.alphaSource = gfx::ColorSource::kConstant;
    call.color = {color[0], color[1], color[2], 1};
    call.fogDepth = std::max(distance, 1.0f);
    call.fogPlane = plane;
    call.time = time;
    call.deforms = m.deforms;
    call.state = {};
    call.state.src = gfx::Blend::kSrcAlpha;
    call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
    call.state.depthWrite = false;
    call.state.depthEqual = m.sort <= kSortOpaque;
    call.state.cull = m.stages.front().state.cull;
    call.state.depthBias = m.stages.front().state.depthBias;
    _r.draw(call);
}

} // namespace reluva::idtech3
