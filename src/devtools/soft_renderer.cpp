// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "devtools/soft_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::tools {

namespace {

// A vertex after the model and view: clip position, then what's
// interpolated across the triangle.
struct ClipVertex {
    float x;
    float y;
    float z;
    float w;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    // where it is in the world (for the dynamic lights)
    float wx;
    float wy;
    float wz;
};

ClipVertex mix(const ClipVertex &p, const ClipVertex &q, float t) {
    auto l = [t](float a, float b) { return a + (b - a) * t; };
    return {l(p.x, q.x), l(p.y, q.y), l(p.z, q.z), l(p.w, q.w),   l(p.u, q.u),   l(p.v, q.v),  l(p.r, q.r),
            l(p.g, q.g), l(p.b, q.b), l(p.a, q.a), l(p.wx, q.wx), l(p.wy, q.wy), l(p.wz, q.wz)};
}

float factor(gfx::Blend f, float src, float dst, float srcA, float dstA) {
    switch (f) {
    case gfx::Blend::kZero:
        return 0;
    case gfx::Blend::kOne:
        return 1;
    case gfx::Blend::kSrcColor:
        return src;
    case gfx::Blend::kOneMinusSrcColor:
        return 1 - src;
    case gfx::Blend::kDstColor:
        return dst;
    case gfx::Blend::kOneMinusDstColor:
        return 1 - dst;
    case gfx::Blend::kSrcAlpha:
        return srcA;
    case gfx::Blend::kOneMinusSrcAlpha:
        return 1 - srcA;
    case gfx::Blend::kDstAlpha:
        return dstA;
    case gfx::Blend::kOneMinusDstAlpha:
        return 1 - dstA;
    }
    return 1;
}

} // namespace

SoftRenderer3D::SoftRenderer3D(int width, int height) : _w(width), _h(height) {
    _color.create(_w, _h, PixelFormat::kRGBA32);
    _frame.create(_w, _h, PixelFormat::kRGBA32);
    _depth.assign(size_t(_w) * size_t(_h), 1.0f);
    _stencil.assign(size_t(_w) * size_t(_h), 0);
    _vw = _w;
    _vh = _h;
}

Error SoftRenderer3D::createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                                    gfx::TextureId &out) {
    if (width <= 0 || height <= 0 || !rgba)
        return Error::fail("bad texture");
    Texture t;
    t.w = width;
    t.h = height;
    t.clamp = params.clamp;
    t.nearest = params.nearest;
    t.rgba.assign(rgba, rgba + size_t(width) * size_t(height) * 4);
    out = _nextId++;
    _textures[out] = std::move(t);
    return Error::success();
}

void SoftRenderer3D::destroyTexture(gfx::TextureId id) {
    _textures.erase(id);
}

Error SoftRenderer3D::updateTextures(const std::vector<gfx::TextureRegion> &regions) {
    for (const gfx::TextureRegion &r : regions) {
        auto it = _textures.find(r.texture);
        if (it == _textures.end() || !r.rgba)
            continue;
        Texture &t = it->second;
        for (int y = 0; y < r.h && r.y + y < t.h; ++y)
            std::copy_n(r.rgba + size_t(y) * size_t(r.w) * 4, size_t(std::min(r.w, t.w - r.x)) * 4,
                        t.rgba.begin() + (ptrdiff_t(r.y + y) * t.w + r.x) * 4);
    }
    return Error::success();
}

Error SoftRenderer3D::createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                                 gfx::MeshId &out) {
    out = _nextId++;
    Mesh &m = _meshes[out];
    m.vertexMesh = out;
    m.vertices = vertices;
    m.indices = indices;
    return Error::success();
}

Error SoftRenderer3D::createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t> &indices, gfx::MeshId &out) {
    if (!_meshes.count(vertices))
        return Error::fail("no such mesh");
    out = _nextId++;
    Mesh &m = _meshes[out];
    m.vertexMesh = vertices;
    m.indices = indices;
    return Error::success();
}

Error SoftRenderer3D::setMeshIndices(gfx::MeshId id, const std::vector<uint32_t> &indices) {
    auto it = _meshes.find(id);
    if (it == _meshes.end())
        return Error::fail("no such mesh");
    it->second.indices = indices;
    return Error::success();
}

void SoftRenderer3D::destroyMesh(gfx::MeshId id) {
    _meshes.erase(id);
}

void SoftRenderer3D::beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, Color clear) {
    _viewProj = viewProj;
    _eye = eye;
    _vx = _vy = 0;
    _vw = _w;
    _vh = _h;
    _depthRange = 1;
    _draws = _triangles = 0;
    if (!drawing) // nothing will be drawn or looked at: no need to clear
        return;
    for (int y = 0; y < _h; ++y) {
        uint8_t *p = _color.row(y);
        for (int x = 0; x < _w; ++x) {
            p[x * 4] = clear.r;
            p[x * 4 + 1] = clear.g;
            p[x * 4 + 2] = clear.b;
            p[x * 4 + 3] = 255;
        }
    }
    std::fill(_depth.begin(), _depth.end(), 1.0f);
    std::fill(_stencil.begin(), _stencil.end(), uint8_t(0));
    _stencilMode = StencilMode::kNone;
}

void SoftRenderer3D::setView(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye) {
    _viewProj = viewProj;
    _eye = eye;
}

void SoftRenderer3D::setViewport(int x, int y, int w, int h) {
    _vx = x;
    _vy = y;
    _vw = w > 0 ? w : _w;
    _vh = h > 0 ? h : _h;
}

void SoftRenderer3D::clearDepth() {
    std::fill(_depth.begin(), _depth.end(), 1.0f);
}

void SoftRenderer3D::clearDepthAndStencil() {
    clearDepth();
    std::fill(_stencil.begin(), _stencil.end(), uint8_t(0));
}

void SoftRenderer3D::draw(const gfx::DrawCall &call) {
    if (!drawing)
        return;
    auto mit = _meshes.find(call.mesh);
    if (mit == _meshes.end())
        return;
    const Mesh &mesh = mit->second;
    auto vit = _meshes.find(mesh.vertexMesh);
    if (vit == _meshes.end())
        return;
    const std::vector<gfx::Vertex> &verts = vit->second.vertices;
    const Texture *tex = nullptr;
    if (auto t = _textures.find(call.texture); call.texture && t != _textures.end())
        tex = &t->second;
    ++_draws;
    const gfx::Mat4 mvp = gfx::multiply(_viewProj, call.model);
    const gfx::Mat4 &m = call.model;
    auto vertexAt = [&](uint32_t index, ClipVertex &out) {
        const size_t i = size_t(int64_t(index) + call.vertexOffset);
        if (i >= verts.size())
            return false;
        const gfx::Vertex &v = verts[i];
        float p[3] = {v.pos[0], v.pos[1], v.pos[2]};
        float n[3] = {v.normal[0], v.normal[1], v.normal[2]};
        if (call.backLerp > 0) {
            const size_t j = size_t(int64_t(index) + call.oldVertexOffset);
            if (j < verts.size())
                for (int k = 0; k < 3; ++k) {
                    p[k] += (verts[j].pos[k] - p[k]) * call.backLerp;
                    n[k] += (verts[j].normal[k] - n[k]) * call.backLerp;
                }
        }
        // The waves along the normal and the moves (the GPU's deforms; not
        // the bulge).
        for (const gfx::Deform &d : call.deforms) {
            if (d.type == gfx::Deform::Type::kWave) {
                gfx::Wave w = d.wave;
                w.phase += (p[0] + p[1] + p[2]) * d.spread;
                const float by = gfx::evaluate(w, call.time);
                for (int k = 0; k < 3; ++k)
                    p[k] += n[k] * by;
            } else if (d.type == gfx::Deform::Type::kMove) {
                const float by = gfx::evaluate(d.wave, call.time);
                for (int k = 0; k < 3; ++k)
                    p[k] += d.vector[size_t(k)] * by;
            }
        }
        out.x = mvp[0] * p[0] + mvp[4] * p[1] + mvp[8] * p[2] + mvp[12];
        out.y = mvp[1] * p[0] + mvp[5] * p[1] + mvp[9] * p[2] + mvp[13];
        out.z = mvp[2] * p[0] + mvp[6] * p[1] + mvp[10] * p[2] + mvp[14];
        out.w = mvp[3] * p[0] + mvp[7] * p[1] + mvp[11] * p[2] + mvp[15];
        out.wx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        out.wy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        out.wz = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
        if (const float ww = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]; ww != 0 && ww != 1) {
            out.wx /= ww; // a projection (a planar shadow's)
            out.wy /= ww;
            out.wz /= ww;
        }
        const float *uv = call.texGen == gfx::TexGen::kUv2 ? v.uv2 : v.uv;
        // Through the pre and post rows (the turbulence between them left out).
        auto rows = [](const std::array<float, 3> &r0, const std::array<float, 3> &r1, float &u, float &w) {
            const float nu = r0[0] * u + r0[1] * w + r0[2];
            const float nw = r1[0] * u + r1[1] * w + r1[2];
            u = nu;
            w = nw;
        };
        out.u = uv[0];
        out.v = uv[1];
        rows(call.preRow0, call.preRow1, out.u, out.v);
        rows(call.postRow0, call.postRow1, out.u, out.v);
        std::array<float, 4> c = call.color;
        auto source = [&](gfx::ColorSource s, int k) {
            const float vc = float(v.color[k]) / 255.0f;
            switch (s) {
            case gfx::ColorSource::kVertex:
                return vc;
            case gfx::ColorSource::kOneMinusVertex:
                return 1 - vc;
            default:
                return 1.0f;
            }
        };
        if (call.rgbSource == gfx::ColorSource::kLighting) {
            float wn[3];
            for (int k = 0; k < 3; ++k)
                wn[k] = m[k] * n[0] + m[4 + k] * n[1] + m[8 + k] * n[2];
            const float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
            const float facing = len > 0
                                     ? std::max(0.0f, (wn[0] * call.lightDirection[0] + wn[1] * call.lightDirection[1] +
                                                       wn[2] * call.lightDirection[2]) /
                                                          len)
                                     : 0;
            if (call.vertexLightCount > 0) {
                const float inv = len > 0 ? 1 / len : 0;
                const float nx = wn[0] * inv;
                const float ny = wn[1] * inv;
                const float nz = wn[2] * inv;
                const float vx = out.wx - _eye[0];
                const float vy = out.wy - _eye[1];
                const float vz = out.wz - _eye[2];
                const float view2 = vx * vx + vy * vy + vz * vz;
                float sum[3] = {0, 0, 0};
                for (int i = 0; i < call.vertexLightCount && i < gfx::kMaxVertexLights; ++i) {
                    const gfx::VertexLight &l = call.vertexLights[size_t(i)];
                    const float dx = l.position[0] - out.wx;
                    const float dy = l.position[1] - out.wy;
                    const float dz = l.position[2] - out.wz;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (call.surfaceLighting) { // as surfaces are lit
                        float amount = 0;
                        if (l.cylinder) {
                            const float f = l.radius > 0 ? std::sqrt(dx * dx + dy * dy) / l.radius : 1;
                            amount = f < 1 ? 1 - f * f : 0;
                        } else if (l.radius > 0 && dist > 0 && dist < l.radius) {
                            const float f = dist / l.radius;
                            amount =
                                (1 - 3 * f * f + 2 * f * f * f) * std::max(0.0f, (dx * nx + dy * ny + dz * nz) / dist);
                        }
                        for (int k = 0; k < 3; ++k)
                            sum[k] += l.color[size_t(k)] * amount;
                        continue;
                    }
                    const float fall = l.radius > 0 ? 1 - dist / l.radius : 0;
                    if (fall <= 0 || dist <= 0)
                        continue;
                    const float nd = dx * nx + dy * ny + dz * nz;
                    const float cw = nd / dist + 1;
                    float amount = std::max(cw * cw - 1.5f, 0.0f);
                    const float mx = l.position[0] - 2 * nd * nx - out.wx;
                    const float my = l.position[1] - 2 * nd * ny - out.wy;
                    const float mz = l.position[2] - 2 * nd * nz - out.wz;
                    const float sp = (mx * vx + my * vy + mz * vz) * call.specular;
                    if (sp > 0 && view2 > 0)
                        amount += 6 * sp * sp / (dist * dist * view2);
                    for (int k = 0; k < 3; ++k)
                        sum[k] += l.color[size_t(k)] * amount * fall;
                }
                for (int k = 0; k < 3; ++k)
                    c[size_t(k)] *= std::min(1.0f, call.ambientLight[size_t(k)] + call.vertexLightScale * sum[k]);
            } else {
                // The two fill lights, as the first: their colour by how far
                // the surface faces them.
                float fill[3] = {0, 0, 0};
                for (size_t f = 0; f < call.fillLight.size() && len > 0; ++f) {
                    const gfx::Vec3 &d = call.fillDirection[f];
                    const float faces = std::max(0.0f, (wn[0] * d[0] + wn[1] * d[1] + wn[2] * d[2]) / len);
                    for (size_t k = 0; k < 3; ++k)
                        fill[k] += call.fillLight[f][k] * faces;
                }
                for (int k = 0; k < 3; ++k) {
                    const float directed = call.directedLight[size_t(k)] * facing + fill[k];
                    if (call.materialLit) // as Direct3D lights a material, the GPU's shader too
                        c[size_t(k)] = std::clamp(call.materialEmissive[size_t(k)] +
                                                      call.materialAmbient[size_t(k)] * call.ambientLight[size_t(k)] +
                                                      c[size_t(k)] * directed,
                                                  0.0f, 1.0f);
                    else
                        c[size_t(k)] *= call.ambientLight[size_t(k)] + directed;
                }
            }
        } else if (call.rgbSource == gfx::ColorSource::kShadow) {
            const float dx = out.wx - call.shadowLight[0];
            const float dy = out.wy - call.shadowLight[1];
            const float dz = out.wz - call.shadowLight[2];
            const float off = std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - call.shadowReach);
            const float dark = off > 0 ? std::min(call.shadowStrength / off, call.shadowMax) : call.shadowMax;
            for (int k = 0; k < 3; ++k)
                c[size_t(k)] *= 1 - dark;
        } else {
            for (int k = 0; k < 3; ++k)
                c[size_t(k)] *= source(call.rgbSource, k);
        }
        c[3] *= source(call.alphaSource, 3);
        out.r = c[0];
        out.g = c[1];
        out.b = c[2];
        out.a = c[3];
        return true;
    };

    const uint32_t end = std::min<uint32_t>(uint32_t(mesh.indices.size()), call.firstIndex + call.indexCount);
    for (uint32_t t = call.firstIndex; t + 2 < end; t += 3) {
        ClipVertex tri[3];
        if (!vertexAt(mesh.indices[t], tri[0]) || !vertexAt(mesh.indices[t + 1], tri[1]) ||
            !vertexAt(mesh.indices[t + 2], tri[2]))
            continue;
        // Clipped at the near plane (z >= 0 in clip space).
        std::vector<ClipVertex> poly;
        for (int k = 0; k < 3; ++k) {
            const ClipVertex &a = tri[k];
            const ClipVertex &b = tri[(k + 1) % 3];
            const bool ina = a.z >= 0 && a.w > 0;
            const bool inb = b.z >= 0 && b.w > 0;
            if (ina)
                poly.push_back(a);
            if (ina != inb && (a.z - b.z) != 0)
                poly.push_back(mix(a, b, a.z / (a.z - b.z)));
        }
        if (poly.size() < 3)
            continue;
        // To the screen.
        struct Screen {
            float x, y, z, invW;
            ClipVertex c;
        };
        std::vector<Screen> s;
        for (const ClipVertex &c : poly) {
            if (c.w <= 1e-6f)
                continue;
            const float iw = 1 / c.w;
            s.push_back({float(_vx) + (c.x * iw * 0.5f + 0.5f) * float(_vw),
                         float(_vy) + (0.5f - c.y * iw * 0.5f) * float(_vh), c.z * iw * _depthRange, iw, c});
        }
        for (size_t k = 2; k < s.size(); ++k) {
            const Screen *v0 = &s[0];
            const Screen *v1 = &s[k - 1];
            const Screen *v2 = &s[k];
            // Counter-clockwise with Y up is clockwise on the screen (Y down).
            float area = (v1->x - v0->x) * (v2->y - v0->y) - (v1->y - v0->y) * (v2->x - v0->x);
            if (area == 0 || !std::isfinite(area))
                continue;
            const bool front = area < 0;
            if ((call.state.cull == gfx::Cull::kBack && !front) || (call.state.cull == gfx::Cull::kFront && front))
                continue;
            if (area < 0) {
                std::swap(v1, v2);
                area = -area;
            }
            ++_triangles;
            // Within the viewport (a HUD portrait's), as a GPU keeps to it.
            const int x0 = std::max(std::max(0, _vx), int(std::floor(std::min({v0->x, v1->x, v2->x}))));
            const int x1 = std::min(std::min(_w, _vx + _vw) - 1, int(std::ceil(std::max({v0->x, v1->x, v2->x}))));
            const int y0 = std::max(std::max(0, _vy), int(std::floor(std::min({v0->y, v1->y, v2->y}))));
            const int y1 = std::min(std::min(_h, _vy + _vh) - 1, int(std::ceil(std::max({v0->y, v1->y, v2->y}))));
            auto edge = [](const Screen &p, const Screen &q, float x, float y) {
                return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
            };
            // Whether a pixel is in is decided exactly: the corners snapped to
            // sixteenths of a pixel, the edges in integers. A centre on an edge
            // belongs to one of the two triangles sharing it (by the edge's
            // direction), not both: drawn twice, an added quad's diagonal shows.
            struct Fixed {
                int64_t x, y;
            };
            auto snap = [](const Screen &p) {
                return Fixed{int64_t(std::llround(double(p.x) * 16)), int64_t(std::llround(double(p.y) * 16))};
            };
            const Fixed f0 = snap(*v0);
            const Fixed f1 = snap(*v1);
            const Fixed f2 = snap(*v2);
            auto fixedEdge = [](const Fixed &p, const Fixed &q, int64_t x, int64_t y) {
                return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
            };
            auto owns = [](const Fixed &p, const Fixed &q) { return q.y - p.y > 0 || (q.y == p.y && q.x - p.x > 0); };
            const bool own0 = owns(f1, f2);
            const bool own1 = owns(f2, f0);
            const bool own2 = owns(f0, f1);
            // The snapped corners can wind the other way for a sliver: none of it drawn.
            if (fixedEdge(f0, f1, f2.x, f2.y) < 0)
                continue;
            const float inv = 1 / area;
            for (int y = y0; y <= y1; ++y) {
                uint8_t *row = _color.row(y);
                for (int x = x0; x <= x1; ++x) {
                    const float px = float(x) + 0.5f;
                    const float py = float(y) + 0.5f;
                    const int64_t cx = int64_t(x) * 16 + 8;
                    const int64_t cy = int64_t(y) * 16 + 8;
                    const int64_t e0 = fixedEdge(f1, f2, cx, cy);
                    const int64_t e1 = fixedEdge(f2, f0, cx, cy);
                    const int64_t e2 = fixedEdge(f0, f1, cx, cy);
                    if (e0 < 0 || e1 < 0 || e2 < 0 || (e0 == 0 && !own0) || (e1 == 0 && !own1) || (e2 == 0 && !own2))
                        continue;
                    const float w0 = edge(*v1, *v2, px, py);
                    const float w1 = edge(*v2, *v0, px, py);
                    const float w2 = edge(*v0, *v1, px, py);
                    const float b0 = w0 * inv;
                    const float b1 = w1 * inv;
                    const float b2 = w2 * inv;
                    const float z = b0 * v0->z + b1 * v1->z + b2 * v2->z;
                    float &d = _depth[size_t(y) * size_t(_w) + size_t(x)];
                    if (call.state.depthTest && (call.state.depthEqual ? std::fabs(z - d) > 1e-5f : z > d))
                        continue;
                    uint8_t &mark = _stencil[size_t(y) * size_t(_w) + size_t(x)];
                    if ((_stencilMode == StencilMode::kLevelInside && mark != stencilLevel) ||
                        (_stencilMode == StencilMode::kLevelMark && mark != stencilLevel - 1) ||
                        (_stencilMode == StencilMode::kUnmark && mark != stencilLevel) ||
                        (_stencilMode == StencilMode::kOnce && mark))
                        continue;
                    const float iw = b0 * v0->invW + b1 * v1->invW + b2 * v2->invW;
                    auto attr = [&](float ClipVertex::*f) {
                        return (b0 * (v0->c.*f) * v0->invW + b1 * (v1->c.*f) * v1->invW + b2 * (v2->c.*f) * v2->invW) /
                               iw;
                    };
                    float src[4] = {attr(&ClipVertex::r), attr(&ClipVertex::g), attr(&ClipVertex::b),
                                    attr(&ClipVertex::a)};
                    // The dynamic lights' light here: alone, or times the
                    // texture (below).
                    const bool lit = call.dynamicLights || call.dynamicLightsTextured;
                    if (lit) {
                        const float px = attr(&ClipVertex::wx);
                        const float py = attr(&ClipVertex::wy);
                        const float pz = attr(&ClipVertex::wz);
                        float sum[3] = {0, 0, 0};
                        for (const gfx::DynamicLight &l : _lights) {
                            const float dx = px - l.origin[0];
                            const float dy = py - l.origin[1];
                            const float dz = pz - l.origin[2];
                            const float f = std::clamp(
                                1 - std::sqrt(dx * dx + dy * dy + dz * dz) / std::max(l.radius, 1e-3f), 0.0f, 1.0f);
                            for (int k = 0; k < 3; ++k)
                                sum[k] += l.color[size_t(k)] * f;
                        }
                        for (int k = 0; k < 3; ++k)
                            src[k] = sum[k];
                        src[3] = 1;
                    }
                    if (call.volumeLights) {
                        const gfx::Vec3 at{attr(&ClipVertex::wx), attr(&ClipVertex::wy), attr(&ClipVertex::wz)};
                        float sum[3] = {0, 0, 0};
                        float fog = 0;
                        for (const gfx::DynamicLight &l : _volumes) {
                            const float f = gfx::volumeGlow(_eye, at, l);
                            for (int k = 0; k < 3; ++k)
                                sum[k] += l.color[size_t(k)] * f;
                            fog += l.fog * f;
                        }
                        for (int k = 0; k < 3; ++k)
                            src[k] = sum[k];
                        src[3] = std::min(fog, 1.0f);
                    }
                    if (tex && !call.dynamicLights && !call.volumeLights) {
                        float u = attr(&ClipVertex::u);
                        float v = attr(&ClipVertex::v);
                        auto wrap = [&](int c, int n) {
                            return tex->clamp ? std::clamp(c, 0, n - 1) : ((c % n) + n) % n;
                        };
                        auto at = [&](int x, int y) {
                            return &tex->rgba[(size_t(wrap(y, tex->h)) * size_t(tex->w) + size_t(wrap(x, tex->w))) * 4];
                        };
                        float texel[4];
                        if (tex->nearest) {
                            const uint8_t *t =
                                at(int(std::floor(u * float(tex->w))), int(std::floor(v * float(tex->h))));
                            for (int k = 0; k < 4; ++k)
                                texel[k] = float(t[k]);
                        } else { // between the four nearest texels' centres
                            const float fx = u * float(tex->w) - 0.5f;
                            const float fy = v * float(tex->h) - 0.5f;
                            const int x0 = int(std::floor(fx));
                            const int y0 = int(std::floor(fy));
                            const float ax = fx - float(x0);
                            const float ay = fy - float(y0);
                            const uint8_t *a = at(x0, y0);
                            const uint8_t *b = at(x0 + 1, y0);
                            const uint8_t *c = at(x0, y0 + 1);
                            const uint8_t *d = at(x0 + 1, y0 + 1);
                            for (int k = 0; k < 4; ++k)
                                texel[k] = (float(a[k]) * (1 - ax) + float(b[k]) * ax) * (1 - ay) +
                                           (float(c[k]) * (1 - ax) + float(d[k]) * ax) * ay;
                        }
                        for (int k = 0; k < 4; ++k)
                            src[k] *= texel[k] / 255.0f;
                    }
                    if (call.fogDistance > 0) { // alpha by depth in the view, for a fog pass
                        const float depth = _viewProj[3] * attr(&ClipVertex::wx) +
                                            _viewProj[7] * attr(&ClipVertex::wy) +
                                            _viewProj[11] * attr(&ClipVertex::wz) + _viewProj[15];
                        src[3] = std::clamp(depth / call.fogDistance, 0.0f, 1.0f);
                    }
                    float alpha = src[3];
                    if (call.dynamicLightsTextured)
                        src[3] = 1; // its alpha only tested
                    if ((call.state.alphaTest == gfx::AlphaTest::kGt0 && alpha <= 0) ||
                        (call.state.alphaTest == gfx::AlphaTest::kLt128 && alpha >= 0.5f) ||
                        (call.state.alphaTest == gfx::AlphaTest::kGe128 && alpha < 0.5f))
                        continue;
                    uint8_t *out = row + x * 4;
                    float dst[4] = {float(out[0]) / 255.0f, float(out[1]) / 255.0f, float(out[2]) / 255.0f,
                                    float(out[3]) / 255.0f};
                    for (int k = 0; k < 4; ++k) {
                        const float s0 = std::clamp(src[k], 0.0f, 1.0f);
                        const float r = s0 * factor(call.state.src, s0, dst[k], alpha, dst[3]) +
                                        dst[k] * factor(call.state.dst, s0, dst[k], alpha, dst[3]);
                        out[k] = uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                    if (call.state.depthWrite)
                        d = z;
                    if (_stencilMode == StencilMode::kLevelMark)
                        mark = stencilLevel;
                    else if (_stencilMode == StencilMode::kUnmark)
                        mark = uint8_t(stencilLevel - 1);
                    else if (_stencilMode == StencilMode::kOnce)
                        mark = 1;
                }
            }
        }
    }
}

void SoftRenderer3D::endFrame(const Surface *overlay) {
    if (!drawing)
        return;
    std::array<uint8_t, 256> curve{};
    for (int i = 0; i < 256; ++i)
        curve[size_t(i)] = uint8_t(std::clamp(std::pow(float(i) / 255.0f, 1 / _gamma), 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = 0; y < _h; ++y) {
        const uint8_t *in = _color.row(y);
        uint8_t *out = _frame.row(y);
        const uint8_t *over = overlay && overlay->w == _w && overlay->h == _h ? overlay->row(y) : nullptr;
        for (int x = 0; x < _w; ++x)
            for (int k = 0; k < 3; ++k) {
                float c = float(curve[in[x * 4 + k]]);
                if (over) {
                    const float a = float(over[x * 4 + 3]) / 255.0f;
                    c = c * (1 - a) + float(over[x * 4 + k]) * a;
                }
                out[x * 4 + k] = uint8_t(c + 0.5f);
                out[x * 4 + 3] = 255;
            }
    }
    _lastDraws = _draws;
    _lastTriangles = _triangles;
    ++_frames;
}

} // namespace reluva::tools
