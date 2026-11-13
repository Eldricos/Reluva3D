// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/render/thrash_gpu.h"

#include <numeric>

namespace reluva::nfs {

namespace {

// Screen space to clip space for a viewport over the rectangle (x0, y0) to
// (x0 + w, y0 + h). A vertex goes in as (x / rhw, y / rhw, 1 / rhw), so that
// clip w is 1 / rhw and the GPU interpolates perspective-correctly as the
// software renderer does; depth is the same throughout (no depth test).
gfx::Mat4 screenToClip(int x0, int y0, int w, int h) {
    gfx::Mat4 m{};
    m[0 * 4 + 0] = 2.0f / float(w);                    // x from X
    m[1 * 4 + 1] = -2.0f / float(h);                   // y from Y (clip +y is up)
    m[2 * 4 + 0] = -(2.0f * float(x0) / float(w) + 1); // ... and from W
    m[2 * 4 + 1] = 2.0f * float(y0) / float(h) + 1;
    m[2 * 4 + 2] = 0.5f; // depth 0.5
    m[2 * 4 + 3] = 1;    // clip w = W
    return m;
}

} // namespace

void ThrashGpu::setRenderer(gfx::Renderer3D *renderer) {
    if (_renderer && _renderer != renderer) {
        _renderer->setKeepFrame(false); // as the next user expects it
        _renderer->setFrameAlphaUnused(false);
        for (const auto &[key, id] : _gpuTextures)
            _renderer->destroyTexture(id);
        for (gfx::TextureId id : _retired)
            _renderer->destroyTexture(id);
        if (_mesh)
            _renderer->destroyMesh(_mesh);
    }
    _gpuTextures.clear();
    _retired.clear();
    _mesh = 0;
    _indexCapacity = 0;
    _renderer = renderer;
    // The game draws into a buffer kept from frame to frame, sometimes
    // adding to what's there (a loading screen's progress bar).
    // Nothing reads its alpha: blended and added polygons, which races
    // alternate, can share a pipeline.
    if (_renderer) {
        _renderer->setKeepFrame(true);
        _renderer->setFrameAlphaUnused(true);
    }
}

void ThrashGpu::updateTextures(const ThrashFrame &frame) {
    auto drop = [this](uint32_t handle) {
        for (bool wrap : {false, true})
            for (bool point : {false, true})
                if (auto it = _gpuTextures.find({handle, wrap, point}); it != _gpuTextures.end()) {
                    _retired.push_back(it->second);
                    _gpuTextures.erase(it);
                }
    };
    if (frame.resetTextures) {
        for (const auto &[handle, t] : _textures)
            drop(handle);
        _textures.clear();
    }
    for (const ThrashFrame::Texture &t : frame.textures) {
        drop(t.handle);
        _textures[t.handle] = t;
    }
}

gfx::TextureId ThrashGpu::texture(uint32_t handle, bool wrap, bool point) {
    if (auto it = _gpuTextures.find({handle, wrap, point}); it != _gpuTextures.end())
        return it->second;
    auto t = _textures.find(handle);
    if (t == _textures.end() || t->second.rgba.empty())
        return 0;
    std::vector<uint8_t> rgba(t->second.rgba.size() * 4);
    for (size_t i = 0; i < t->second.rgba.size(); ++i) {
        const uint32_t c = t->second.rgba[i];
        rgba[4 * i] = uint8_t(c >> 16);
        rgba[4 * i + 1] = uint8_t(c >> 8);
        rgba[4 * i + 2] = uint8_t(c);
        rgba[4 * i + 3] = uint8_t(c >> 24);
    }
    gfx::TextureParams params;
    params.clamp = !wrap;
    params.mipmaps = false;
    params.nearest = point;
    gfx::TextureId id = 0;
    if (_renderer->createTexture(t->second.width, t->second.height, rgba.data(), params, id))
        return 0;
    _gpuTextures[{handle, wrap, point}] = id;
    return id;
}

void ThrashGpu::draw(const std::vector<const ThrashFrame *> &frames, const Surface *overlay) {
    if (!_renderer || frames.empty())
        return;
    // All the frames' vertices in the one mesh, each frame's after the last's.
    std::vector<uint32_t> base;
    _vertices.clear();
    for (const ThrashFrame *frame : frames) {
        base.push_back(uint32_t(_vertices.size()));
        for (size_t i = 0; i < frame->vertices.size(); ++i) {
            const ThrashFrame::Vertex &v = frame->vertices[i];
            // 1/w as given, for perspective; but a triangle whose three are
            // the same is flat on the screen and needs none (2D pictures
            // come with 1/65536, which would make w huge for nothing).
            const ThrashFrame::Vertex *tri = &frame->vertices[i - i % 3];
            const bool flat =
                i - i % 3 + 2 < frame->vertices.size() && tri[0].rhw == tri[1].rhw && tri[1].rhw == tri[2].rhw;
            const float w = flat || v.rhw <= 0 ? 1.0f : 1.0f / v.rhw;
            gfx::Vertex o{};
            o.pos[0] = v.x * w;
            o.pos[1] = v.y * w;
            o.pos[2] = w;
            o.uv[0] = v.u;
            o.uv[1] = v.v;
            o.uv2[0] = v.rhw > 0 ? 1.0f / v.rhw : 0.0f; // w, for fog
            o.color[0] = uint8_t(v.diffuse >> 16);
            o.color[1] = uint8_t(v.diffuse >> 8);
            o.color[2] = uint8_t(v.diffuse);
            o.color[3] = uint8_t(v.diffuse >> 24);
            _vertices.push_back(o);
        }
    }
    // Its indices are 0, 1, 2... (each triangle has vertices of its own),
    // set again only to grow.
    bool ok = !_vertices.empty();
    if (ok && _vertices.size() > _indexCapacity) {
        std::vector<uint32_t> indices(_vertices.size() + _vertices.size() / 2);
        std::iota(indices.begin(), indices.end(), 0u);
        std::vector<gfx::Vertex> padded = _vertices;
        padded.resize(indices.size());
        if (_mesh)
            _renderer->destroyMesh(_mesh);
        _mesh = 0;
        ok = !_renderer->createMesh(padded, indices, _mesh);
        _indexCapacity = ok ? indices.size() : 0;
    } else if (ok) {
        ok = !_renderer->setMeshVertices(_mesh, _vertices);
    }

    const ThrashFrame &first = *frames.front();
    _renderer->beginFrame(screenToClip(0, 0, first.width, first.height), {0, 0, 0}, Color{0, 0, 0});
    for (size_t f = 0; f < frames.size(); ++f) {
        const ThrashFrame &frame = *frames[f];
        // Textures as this frame has them: a draw already made keeps the
        // texture it was given, even one replaced here.
        updateTextures(frame);
        if (!ok)
            continue;
        for (const ThrashFrame::Batch &b : frame.batches) {
            const int w = b.clip[2] - b.clip[0];
            const int h = b.clip[3] - b.clip[1];
            if (!b.count || w <= 0 || h <= 0)
                continue;
            _renderer->setViewport(b.clip[0] * scale, b.clip[1] * scale, w * scale, h * scale);
            _renderer->setView(screenToClip(b.clip[0], b.clip[1], w, h), {0, 0, 0});
            gfx::DrawCall call;
            call.mesh = _mesh;
            call.firstIndex = base[f] + b.first;
            call.indexCount = b.count;
            call.texture = b.texture ? texture(b.texture, b.wrap, b.filter < 0 ? nearest : b.filter == 0) : 0;
            call.rgbSource = gfx::ColorSource::kVertex;
            call.alphaSource = gfx::ColorSource::kVertex;
            if (b.fog > 0) {
                call.tableFog = b.fog;
                call.tableFogColor = {float((b.fogColour >> 16) & 255) / 255, float((b.fogColour >> 8) & 255) / 255,
                                      float(b.fogColour & 255) / 255};
            }
            gfx::DrawState &s = call.state;
            s.depthTest = false;
            s.depthWrite = false;
            // Cull 1 leaves out triangles turning counter-clockwise as shown,
            // which the renderer takes as front faces; cull 2 the others.
            s.cull = b.cull == 1 ? gfx::Cull::kFront : b.cull == 2 ? gfx::Cull::kBack : gfx::Cull::kNone;
            // As the software renderer: what's translucent is blended over
            // what's drawn (opaque texels and vertices replace it); the other
            // modes when blending is on.
            s.src = gfx::Blend::kSrcAlpha;
            s.dst = gfx::Blend::kOneMinusSrcAlpha;
            if (b.blend && b.blendMode == 1)
                s.dst = gfx::Blend::kOne;
            else if (b.blend && b.blendMode == 2)
                s.src = gfx::Blend::kZero;
            else if (b.blend && b.blendMode == 3) {
                s.src = gfx::Blend::kDstColor;
                s.dst = gfx::Blend::kZero;
            }
            // Texels of alpha 0 are left out (the colour key).
            s.alphaTest = call.texture ? gfx::AlphaTest::kGt0 : gfx::AlphaTest::kNone;
            _renderer->draw(call);
        }
    }
    // An overlay the game's size (a movie's frame), enlarged to the frame's.
    if (overlay && (overlay->w != _renderer->width() || overlay->h != _renderer->height()) && overlay->w > 0 &&
        overlay->h > 0) {
        _scaledOverlay.create(_renderer->width(), _renderer->height(), PixelFormat::kRGBA32);
        for (int y = 0; y < _scaledOverlay.h; ++y) {
            const uint32_t *src = reinterpret_cast<const uint32_t *>(overlay->row(y * overlay->h / _scaledOverlay.h));
            uint32_t *dst = reinterpret_cast<uint32_t *>(_scaledOverlay.row(y));
            for (int x = 0; x < _scaledOverlay.w; ++x)
                dst[x] = src[x * overlay->w / _scaledOverlay.w];
        }
        overlay = &_scaledOverlay;
    }
    _renderer->endFrame(overlay);
    for (gfx::TextureId id : _retired)
        _renderer->destroyTexture(id);
    _retired.clear();
}

} // namespace reluva::nfs
