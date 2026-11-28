// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/render/scene_gpu.h"

#include <cmath>
#include <numeric>

namespace reluva::sith {

namespace {

using Scene = x86::DirectX::Scene;

// Direct3D's numbers (d3dtypes.h).
constexpr uint32_t kCmpLess = 2;
constexpr uint32_t kCmpEqual = 3;
constexpr uint32_t kCmpGreater = 5;
constexpr uint32_t kCmpGreaterEqual = 7;
constexpr uint32_t kOpSelectArg1 = 2;
constexpr uint32_t kOpSelectArg2 = 3;
constexpr uint32_t kArgTexture = 2;
constexpr uint32_t kAddressWrap = 1;

// Screen space to clip space for a viewport over (x0, y0) to (x0 + w,
// y0 + h). A vertex goes in as (x / rhw, y / rhw, 1 / rhw), so that clip w
// is 1 / rhw and the GPU interpolates perspective-correctly as Direct3D
// does; the depth comes from the vertex (DrawCall::screenDepth).
gfx::Mat4 screenToClip(int x0, int y0, int w, int h) {
    gfx::Mat4 m{};
    m[0 * 4 + 0] = 2.0f / float(w);
    m[1 * 4 + 1] = -2.0f / float(h); // clip +y is up
    m[2 * 4 + 0] = -(2.0f * float(x0) / float(w) + 1);
    m[2 * 4 + 1] = 2.0f * float(y0) / float(h) + 1;
    m[2 * 4 + 2] = 0.5f;
    m[2 * 4 + 3] = 1;
    return m;
}

gfx::Blend blendFactor(uint32_t d3d, gfx::Blend otherwise) {
    switch (d3d) {
    case 1:
        return gfx::Blend::kZero;
    case 2:
        return gfx::Blend::kOne;
    case 3:
        return gfx::Blend::kSrcColor;
    case 4:
        return gfx::Blend::kOneMinusSrcColor;
    case 5:
        return gfx::Blend::kSrcAlpha;
    case 6:
        return gfx::Blend::kOneMinusSrcAlpha;
    case 7:
        return gfx::Blend::kDstAlpha;
    case 8:
        return gfx::Blend::kOneMinusDstAlpha;
    case 9:
        return gfx::Blend::kDstColor;
    case 10:
        return gfx::Blend::kOneMinusDstColor;
    default:
        return otherwise;
    }
}

// How much brighter or darker a gamma ramp (Scene::gamma) makes the
// picture, as the renderer's one power (setGamma: each colour to 1 /
// gamma): the power that best fits the three, by least squares on their
// logarithms. A ramp that isn't a power is drawn as the nearest one.
float rampGamma(const std::vector<uint16_t> &ramp) {
    double xy = 0;
    double xx = 0;
    for (size_t c = 0; c < 3 && ramp.size() >= 768; ++c)
        for (int i = 1; i < 255; ++i) {
            const double y = double(ramp[c * 256 + size_t(i)]) / 65535;
            if (y <= 0)
                continue;
            const double x = std::log(double(i) / 255);
            xy += x * std::log(y);
            xx += x * x;
        }
    const double power = xx > 0 ? xy / xx : 1;
    return power > 0.01 ? float(1 / power) : 1.0f;
}

// Whether a scene's depths run the other way from the renderer's (nearer
// is greater, as JK.EXE has them: z is 1/w over 2): by the first depth
// test's comparison.
bool depthReversed(const Scene &scene) {
    for (const Scene::Step &s : scene.steps)
        if (s.count && s.zTest && s.zFunc != 8)
            return s.zFunc == kCmpGreater || s.zFunc == kCmpGreaterEqual;
    return false;
}

} // namespace

void SceneGpu::setRenderer(gfx::Renderer3D *renderer) {
    if (_renderer && _renderer != renderer) {
        _renderer->setGamma(1); // as the next user expects it
        for (const auto &[key, made] : _textures)
            _renderer->destroyTexture(made.id);
        if (_mesh)
            _renderer->destroyMesh(_mesh);
    }
    _textures.clear();
    _mesh = 0;
    _indexCapacity = 0;
    _renderer = renderer;
}

gfx::TextureId SceneGpu::texture(const std::shared_ptr<const Scene::Texture> &t, const Variant &v) {
    const auto key = std::make_pair(static_cast<const void *>(t.get()), v);
    if (auto it = _textures.find(key); it != _textures.end()) {
        if (it->second.source.lock() == t) {
            it->second.used = _frame;
            return it->second.id;
        }
        _renderer->destroyTexture(it->second.id); // another texture since, at the same address
        _textures.erase(it);
    }
    if (t->width <= 0 || t->height <= 0 || t->argb.size() < size_t(t->width) * size_t(t->height))
        return 0;
    std::vector<uint8_t> rgba(t->argb.size() * 4);
    for (size_t i = 0; i < t->argb.size(); ++i) {
        const uint32_t c = t->argb[i];
        rgba[4 * i] = uint8_t(c >> 16);
        rgba[4 * i + 1] = uint8_t(c >> 8);
        rgba[4 * i + 2] = uint8_t(c);
        rgba[4 * i + 3] = v.keyed && i < t->keyed.size() && t->keyed[i] ? 0 : v.alpha ? uint8_t(c >> 24) : 255;
    }
    gfx::TextureParams params;
    params.clamp = !v.wrap;
    params.mipmaps = false;
    params.nearest = !v.linear;
    gfx::TextureId id = 0;
    if (_renderer->createTexture(t->width, t->height, rgba.data(), params, id))
        return 0;
    _textures[key] = {t, id, _frame};
    return id;
}

bool SceneGpu::upload(const Scene &scene) {
    const bool reversed = depthReversed(scene);
    _vertices.clear();
    _vertices.reserve(scene.vertices.size() + 6 * scene.steps.size());
    for (const Scene::Vertex &v : scene.vertices) {
        const float w = v.rhw > 0 ? 1 / v.rhw : 1;
        gfx::Vertex o{};
        // Direct3D's pixel centres are on whole numbers, the GPU's halfway.
        o.pos[0] = (v.x + 0.5f) * w;
        o.pos[1] = (v.y + 0.5f) * w;
        o.pos[2] = w;
        o.uv[0] = v.u;
        o.uv[1] = v.v;
        // Direct3D's specular colour and fog factor (its alpha), for the
        // renderer's (DrawCall::specularFromNormal, vertexFog).
        o.uv2[0] = float(v.specular >> 24) / 255;
        o.normal[0] = float((v.specular >> 16) & 255) / 255;
        o.normal[1] = float((v.specular >> 8) & 255) / 255;
        o.normal[2] = float(v.specular & 255) / 255;
        o.uv2[1] = reversed ? 1 - v.z : v.z;
        o.color[0] = uint8_t(v.diffuse >> 16);
        o.color[1] = uint8_t(v.diffuse >> 8);
        o.color[2] = uint8_t(v.diffuse);
        o.color[3] = uint8_t(v.diffuse >> 24);
        _vertices.push_back(o);
    }
    // A colour clear's two triangles, after the scene's own vertices.
    for (const Scene::Step &s : scene.steps) {
        if (!s.clearColor)
            continue;
        const float x0 = float(s.viewport[0]);
        const float y0 = float(s.viewport[1]);
        const float x1 = x0 + float(s.viewport[2]);
        const float y1 = y0 + float(s.viewport[3]);
        const float corners[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
        for (const auto &c : corners) {
            gfx::Vertex o{};
            o.pos[0] = c[0], o.pos[1] = c[1], o.pos[2] = 1;
            o.uv2[0] = 1;
            o.color[0] = uint8_t(s.color >> 16);
            o.color[1] = uint8_t(s.color >> 8);
            o.color[2] = uint8_t(s.color);
            o.color[3] = 255;
            _vertices.push_back(o);
        }
    }
    if (_vertices.empty())
        return false;
    // The indices are 0, 1, 2... (each triangle has vertices of its own),
    // set again only to grow.
    if (_vertices.size() > _indexCapacity) {
        std::vector<uint32_t> indices(_vertices.size() + _vertices.size() / 2);
        std::iota(indices.begin(), indices.end(), 0u);
        std::vector<gfx::Vertex> padded = _vertices;
        padded.resize(indices.size());
        if (_mesh)
            _renderer->destroyMesh(_mesh);
        _mesh = 0;
        const bool ok = !_renderer->createMesh(padded, indices, _mesh);
        _indexCapacity = ok ? indices.size() : 0;
        return ok;
    }
    return !_renderer->setMeshVertices(_mesh, _vertices);
}

void SceneGpu::draw(const Scene *scene, const Surface &overlay) {
    if (!_renderer)
        return;
    ++_frame;
    const int width = _renderer->width();
    const int height = _renderer->height();
    // The game's picture drawn at the renderer's size: its coordinates
    // stay the game's, its viewports' pixels are the renderer's.
    const int gameW = scene ? scene->width : overlay.w;
    const int gameH = scene ? scene->height : overlay.h;
    if (gameW <= 0 || gameH <= 0)
        return;
    const double sx = double(width) / gameW;
    const double sy = double(height) / gameH;
    const bool ok = scene && upload(*scene);
    // The display's gamma ramp, which the 2D (the overlay) went through
    // already, for the 3D under it.
    if (scene)
        _renderer->setGamma(scene->gamma.empty() ? 1.0f : rampGamma(scene->gamma));
    _renderer->beginFrame(screenToClip(0, 0, gameW, gameH), {0, 0, 0}, Color{0, 0, 0});
    if (ok) {
        const bool reversed = depthReversed(*scene);
        uint32_t clearAt = uint32_t(scene->vertices.size());
        for (const Scene::Step &s : scene->steps) {
            const int w = s.viewport[2];
            const int h = s.viewport[3];
            if (w <= 0 || h <= 0)
                continue;
            const int x0 = int(std::lround(s.viewport[0] * sx));
            const int y0 = int(std::lround(s.viewport[1] * sy));
            _renderer->setViewport(x0, y0, int(std::lround((s.viewport[0] + w) * sx)) - x0,
                                   int(std::lround((s.viewport[1] + h) * sy)) - y0);
            _renderer->setView(screenToClip(s.viewport[0], s.viewport[1], w, h), {0, 0, 0});
            gfx::DrawCall call;
            call.mesh = _mesh;
            call.rgbSource = gfx::ColorSource::kVertex;
            call.alphaSource = gfx::ColorSource::kVertex;
            call.state.cull = gfx::Cull::kNone; // culled already, by the device
            if (s.clearDepth)
                _renderer->clearDepth(); // to the far end, wherever the game cleared it to
            if (s.clearColor) {
                call.firstIndex = clearAt;
                call.indexCount = 6;
                clearAt += 6;
                call.state.depthTest = false;
                call.state.depthWrite = false;
                _renderer->draw(call);
                continue;
            }
            if (!s.count)
                continue;
            call.firstIndex = s.first;
            call.indexCount = s.count;
            call.screenDepth = true;
            // Stage 0 as texture times diffuse, either alone or both.
            const bool colorTexture = s.texture && (s.colorOp == kOpSelectArg1   ? (s.colorArg1 & 0xf) == kArgTexture
                                                    : s.colorOp == kOpSelectArg2 ? (s.colorArg2 & 0xf) == kArgTexture
                                                                                 : s.colorOp != 0);
            const bool colorDiffuse = !(s.colorOp == kOpSelectArg1 && (s.colorArg1 & 0xf) == kArgTexture) &&
                                      !(s.colorOp == kOpSelectArg2 && (s.colorArg2 & 0xf) == kArgTexture);
            const bool alphaTexture = s.texture && (s.alphaOp == kOpSelectArg1   ? (s.alphaArg1 & 0xf) == kArgTexture
                                                    : s.alphaOp == kOpSelectArg2 ? (s.alphaArg2 & 0xf) == kArgTexture
                                                                                 : s.alphaOp != 0 && s.alphaOp != 1);
            const bool alphaDiffuse = !(s.alphaOp == kOpSelectArg1 && (s.alphaArg1 & 0xf) == kArgTexture) &&
                                      !(s.alphaOp == kOpSelectArg2 && (s.alphaArg2 & 0xf) == kArgTexture);
            if (colorTexture || (alphaTexture && s.texture) || (s.colorKey && s.texture))
                call.texture = texture(s.texture, {s.colorKey, alphaTexture, s.addressU == kAddressWrap, s.linear});
            if (!colorDiffuse)
                call.rgbSource = gfx::ColorSource::kConstant;
            if (!alphaDiffuse)
                call.alphaSource = gfx::ColorSource::kConstant;
            gfx::DrawState &st = call.state;
            st.depthTest = s.zTest && s.zFunc != 8;
            st.depthWrite = s.zWrite;
            st.depthLess = reversed ? s.zFunc == kCmpGreater : s.zFunc == kCmpLess;
            st.depthEqual = s.zFunc == kCmpEqual;
            if (s.blend) {
                st.src = blendFactor(s.srcBlend, gfx::Blend::kOne);
                st.dst = blendFactor(s.destBlend, gfx::Blend::kZero);
            }
            if (s.alphaTest && s.alphaFunc >= 1 && s.alphaFunc <= 8) {
                st.alphaTest = gfx::AlphaTest::kCompare;
                call.alphaFunc = gfx::Compare(s.alphaFunc - 1);
                call.alphaRef = s.alphaRef;
            }
            call.specularFromNormal = s.specular;
            // Table fog (by depth) isn't drawn: no game on this path uses it.
            if (s.fog && s.fogTable == 0) {
                call.vertexFog = true;
                call.tableFogColor = {float((s.fogColor >> 16) & 255) / 255, float((s.fogColor >> 8) & 255) / 255,
                                      float(s.fogColor & 255) / 255};
            }
            // A texel keyed out isn't drawn at all.
            if (s.colorKey && call.texture && st.alphaTest == gfx::AlphaTest::kNone)
                st.alphaTest = gfx::AlphaTest::kGt0;
            _renderer->draw(call);
        }
    }
    // The 2D enlarged to the renderer's size, each of its pixels to the
    // nearest: as sharp as the game drew it.
    const Surface *over = &overlay;
    if (overlay.w != width || overlay.h != height) {
        _scaledOverlay.create(width, height, PixelFormat::kRGBA32);
        for (int y = 0; y < height; ++y) {
            const auto *src = reinterpret_cast<const uint32_t *>(overlay.row(y * overlay.h / height));
            auto *dst = reinterpret_cast<uint32_t *>(_scaledOverlay.row(y));
            for (int x = 0; x < width; ++x)
                dst[x] = src[x * overlay.w / width];
        }
        over = &_scaledOverlay;
    }
    _renderer->endFrame(over);
    // Textures not drawn with for a while: the game has let go of them, or
    // will make them again.
    for (auto it = _textures.begin(); it != _textures.end();)
        if (_frame - it->second.used > 120 || it->second.source.expired()) {
            _renderer->destroyTexture(it->second.id);
            it = _textures.erase(it);
        } else {
            ++it;
        }
}

} // namespace reluva::sith
