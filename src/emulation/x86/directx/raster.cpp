// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Direct3D 7's fixed-function pipeline in software, as the DirectX 7
// documentation describes it:
// - vertices are transformed by the world, view and projection matrices
//   (row vectors) and lit in camera space by the enabled lights and the
//   material (or the vertex colours, as the material sources say);
// - triangles are clipped to the view volume, projected into the
//   viewport, culled by their winding on the screen, and filled with
//   pixel centres at whole coordinates and a top-left rule;
// - each pixel goes through the depth test, the texture stages (with
//   wrapping, mip levels and bilinear filtering), the colour key, the
//   alpha test, fog and blending, into the render target and Z buffer,
//   whatever their pixel formats.

#include "emulation/x86/directx/device.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define RELUVA_SPAN4 1
#endif

namespace reluva::x86::dx {

// The steps of a pixel's way, inlined into the triangle's loop: called for
// every pixel, their calls cost more than much of their work.
#if defined(_MSC_VER)
#define RELUVA_PIXEL_STEP __forceinline
#else
#define RELUVA_PIXEL_STEP [[gnu::always_inline]] inline
#endif

namespace {

// Render states.
enum : uint32_t {
    kZEnable = 7,
    kShadeMode = 9,
    kZWriteEnable = 14,
    kAlphaTestEnable = 15,
    kSrcBlend = 19,
    kDestBlend = 20,
    kCullMode = 22,
    kZFunc = 23,
    kAlphaRef = 24,
    kAlphaFunc = 25,
    kAlphaBlendEnable = 27,
    kFogEnable = 28,
    kSpecularEnable = 29,
    kFogColor = 34,
    kFogTableMode = 35,
    kFogStart = 36,
    kFogEnd = 37,
    kFogDensity = 38,
    kColorKeyEnable = 41,
    kRangeFogEnable = 48,
    kTextureFactor = 60,
    kClipping = 136,
    kLighting = 137,
    kAmbient = 139,
    kFogVertexMode = 140,
    kColorVertex = 141,
    kLocalViewer = 142,
    kNormalizeNormals = 143,
    kDiffuseSource = 145,
    kSpecularSource = 146,
    kAmbientSource = 147,
    kEmissiveSource = 148
};
// Texture stage states.
enum : uint32_t {
    kColorOp = 1,
    kColorArg1 = 2,
    kColorArg2 = 3,
    kAlphaOp = 4,
    kAlphaArg1 = 5,
    kAlphaArg2 = 6,
    kTexCoordIndex = 11,
    kAddressU = 13,
    kAddressV = 14,
    kBorderColor = 15,
    kMagFilter = 16,
    kMinFilter = 17,
    kMipFilter = 18,
    kTextureTransformFlags = 24
};
constexpr int kAttributes = 8 + 2 * 8; // diffuse, specular, a u and v per stage

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
float vertexFloat(const uint8_t *p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}
uint32_t vertexWord(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
// x / 255, x / 31 and x / 63 as floats, by table.
struct Fractions {
    float of255[256], of31[32], of63[64];
    Fractions() {
        for (int i = 0; i < 256; ++i)
            of255[i] = float(i) / 255;
        for (int i = 0; i < 32; ++i)
            of31[i] = float(i) / 31;
        for (int i = 0; i < 64; ++i)
            of63[i] = float(i) / 63;
    }
};
const Fractions kFractions;
// std::floor to an int, for values an int holds.
int floorInt(float v) {
    const int i = int(v);
    return i - (float(i) > v);
}

Color unpack(uint32_t argb) {
    const float *f = kFractions.of255;
    return {f[argb >> 16 & 0xff], f[argb >> 8 & 0xff], f[argb & 0xff], f[argb >> 24]};
}
uint32_t pack(float r, float g, float b, float a) {
    auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
}
Color add(const Color &a, const Color &b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a};
}
Color mul(const Color &a, const Color &b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
}
Color scale(const Color &a, float k) {
    return {a.r * k, a.g * k, a.b * k, a.a * k};
}

void transformPoint(const Matrix &m, const float in[4], float out[4]) {
    for (int j = 0; j < 4; ++j)
        out[j] = in[0] * m[size_t(j)] + in[1] * m[size_t(4 + j)] + in[2] * m[size_t(8 + j)] + in[3] * m[size_t(12 + j)];
}
void transformNormal(const Matrix &m, const float in[3], float out[3]) {
    for (int j = 0; j < 3; ++j)
        out[j] = in[0] * m[size_t(j)] + in[1] * m[size_t(4 + j)] + in[2] * m[size_t(8 + j)];
}
float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void normalize3(float v[3]) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 0)
        v[0] /= l, v[1] /= l, v[2] /= l;
}

bool compare(uint32_t func, float a, float b) { // a (new) against b (stored)
    switch (func) {
    case 1:
        return false;
    case 2:
        return a < b;
    case 3:
        return a == b;
    case 4:
        return a <= b;
    case 5:
        return a > b;
    case 6:
        return a != b;
    case 7:
        return a >= b;
    default:
        return true;
    }
}

float blendFactor(uint32_t f, const Color &src, const Color &dst, int channel) {
    const float s[4] = {src.r, src.g, src.b, src.a};
    const float d[4] = {dst.r, dst.g, dst.b, dst.a};
    switch (f) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 3:
        return s[channel];
    case 4:
        return 1 - s[channel];
    case 5:
        return src.a;
    case 6:
        return 1 - src.a;
    case 7:
        return dst.a;
    case 8:
        return 1 - dst.a;
    case 9:
        return d[channel];
    case 10:
        return 1 - d[channel];
    case 11:
        return channel == 3 ? 1 : std::min(src.a, 1 - dst.a);
    case 12:
        return src.a; // BOTHSRCALPHA: source alpha, destination 1 - it
    case 13:
        return 1 - src.a; // BOTHINVSRCALPHA
    default:
        return 1;
    }
}

// A clip-space vertex between two, `t` of the way.
ClipVertex lerp(const ClipVertex &a, const ClipVertex &b, float t) {
    ClipVertex r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    r.w = a.w + (b.w - a.w) * t;
    auto mix = [t](const Color &p, const Color &q) {
        return Color{p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t, p.a + (q.a - p.a) * t};
    };
    r.diffuse = mix(a.diffuse, b.diffuse);
    r.specular = mix(a.specular, b.specular);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 2; ++j)
            r.tex[i][j] = a.tex[i][j] + (b.tex[i][j] - a.tex[i][j]) * t;
    return r;
}

// The distance of a clip-space vertex inside each plane of the view volume.
float planeDistance(const ClipVertex &v, int plane) {
    switch (plane) {
    case 0:
        return v.z; // near
    case 1:
        return v.w - v.z; // far
    case 2:
        return v.w + v.x;
    case 3:
        return v.w - v.x;
    case 4:
        return v.w + v.y;
    default:
        return v.w - v.y;
    }
}

} // namespace

// --- Vertices

void Device::transformAndLight(const VertexLayout &layout, uint32_t vertices, uint32_t stride, uint32_t count,
                               std::vector<ClipVertex> &out) {
    Cpu &c = s->cpu;
    out.resize(count);
    if (!c.valid(vertices, stride * count))
        return out.clear();
    const uint8_t *mem = c.memory().data() + vertices;
    const Matrix world = transforms[1];
    const Matrix view = transforms[2];
    const Matrix proj = transforms[3];
    const Matrix worldView = multiply(world, view);
    const Matrix all = multiply(worldView, proj);
    const bool lighting = renderStates[kLighting] && !layout.rhw;
    const bool colorVertex = renderStates[kColorVertex] != 0;
    const bool specularOn = renderStates[kSpecularEnable] != 0;
    const Color ambientLight = unpack(renderStates[kAmbient]);
    const uint32_t fogMode = renderStates[kFogEnable] ? renderStates[kFogVertexMode] : 0;
    const float fogStart = asFloat(renderStates[kFogStart]);
    const float fogEnd = asFloat(renderStates[kFogEnd]);
    const float fogDensity = asFloat(renderStates[kFogDensity]);

    // The enabled lights in camera space.
    struct EyeLight {
        const Light *l;
        float pos[3], dir[3];
    };
    std::vector<EyeLight> eyeLights;
    if (lighting)
        for (const auto &[i, l] : lights)
            if (l.enabled) {
                EyeLight e{&l, {}, {}};
                const float p[4] = {l.position[0], l.position[1], l.position[2], 1};
                float q[4];
                transformPoint(view, p, q);
                e.pos[0] = q[0], e.pos[1] = q[1], e.pos[2] = q[2];
                transformNormal(view, l.direction, e.dir);
                normalize3(e.dir);
                eyeLights.push_back(e);
            }

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *v = mem + i * stride;
        ClipVertex &o = out[i];
        const bool hasDiffuse = layout.diffuse;
        const bool hasSpecular = layout.specular;
        const Color vDiffuse = hasDiffuse ? unpack(vertexWord(v + layout.diffuseAt)) : Color{1, 1, 1, 1};
        const Color vSpecular = hasSpecular ? unpack(vertexWord(v + layout.specularAt)) : Color{0, 0, 0, 0};
        float eye[4] = {0, 0, 0, 1};
        float normal[3] = {0, 0, 0};
        if (layout.rhw) {
            o.x = vertexFloat(v), o.y = vertexFloat(v + 4), o.z = vertexFloat(v + 8), o.w = vertexFloat(v + 12);
            o.diffuse = vDiffuse;
            o.specular = vSpecular;
            if (!hasSpecular)
                o.specular.a = 1; // no fog factor given: none
        } else {
            const float p[4] = {vertexFloat(v), vertexFloat(v + 4), vertexFloat(v + 8), 1};
            float clip[4];
            transformPoint(all, p, clip);
            o.x = clip[0], o.y = clip[1], o.z = clip[2], o.w = clip[3];
            transformPoint(worldView, p, eye);
            if (layout.normal) {
                const float n[3] = {vertexFloat(v + layout.normalAt), vertexFloat(v + layout.normalAt + 4),
                                    vertexFloat(v + layout.normalAt + 8)};
                transformNormal(worldView, n, normal);
                if (renderStates[kNormalizeNormals])
                    normalize3(normal);
            }
            if (lighting) {
                auto source = [&](uint32_t state, const Color &fromMaterial) {
                    if (!colorVertex)
                        return fromMaterial;
                    if (renderStates[state] == 1 && hasDiffuse)
                        return vDiffuse;
                    if (renderStates[state] == 2 && hasSpecular)
                        return vSpecular;
                    return fromMaterial;
                };
                const Color mDiffuse = source(kDiffuseSource, material.diffuse);
                const Color mAmbient = source(kAmbientSource, material.ambient);
                const Color mSpecular = source(kSpecularSource, material.specular);
                const Color mEmissive = source(kEmissiveSource, material.emissive);
                Color d = add(mEmissive, mul(ambientLight, mAmbient));
                Color sp{};
                float toEye[3] = {-eye[0], -eye[1], -eye[2]};
                if (renderStates[kLocalViewer])
                    normalize3(toEye);
                else
                    toEye[0] = 0, toEye[1] = 0, toEye[2] = -1;
                for (const EyeLight &e : eyeLights) {
                    const Light &l = *e.l;
                    float toLight[3];
                    float atten = 1;
                    float spot = 1;
                    if (l.type == 3) { // directional
                        toLight[0] = -e.dir[0], toLight[1] = -e.dir[1], toLight[2] = -e.dir[2];
                    } else {
                        toLight[0] = e.pos[0] - eye[0], toLight[1] = e.pos[1] - eye[1], toLight[2] = e.pos[2] - eye[2];
                        const float dist = std::sqrt(dot3(toLight, toLight));
                        if (l.range > 0 && dist > l.range)
                            continue;
                        normalize3(toLight);
                        const float a = l.att0 + l.att1 * dist + l.att2 * dist * dist;
                        atten = a > 0 ? 1 / a : 1;
                        if (l.type == 2) { // spot
                            const float rho = -dot3(toLight, e.dir);
                            const float cosPhi = std::cos(l.phi / 2);
                            const float cosTheta = std::cos(l.theta / 2);
                            if (rho <= cosPhi)
                                spot = 0;
                            else if (rho < cosTheta)
                                spot = std::pow((rho - cosPhi) / std::max(cosTheta - cosPhi, 1e-6f),
                                                l.falloff > 0 ? l.falloff : 1);
                        }
                    }
                    d = add(d, scale(mul(l.ambient, mAmbient), atten));
                    const float ndotl = std::max(0.0f, dot3(normal, toLight));
                    d = add(d, scale(mul(l.diffuse, mDiffuse), atten * spot * ndotl));
                    if (specularOn && ndotl > 0 && material.power > 0) {
                        float h[3] = {toLight[0] + toEye[0], toLight[1] + toEye[1], toLight[2] + toEye[2]};
                        normalize3(h);
                        const float s = std::pow(std::max(0.0f, dot3(normal, h)), material.power);
                        sp = add(sp, scale(mul(l.specular, mSpecular), atten * spot * s));
                    }
                }
                o.diffuse = {d.r, d.g, d.b, mDiffuse.a};
                o.specular = {sp.r, sp.g, sp.b, 1};
            } else {
                o.diffuse = vDiffuse;
                o.specular = vSpecular;
                o.specular.a = hasSpecular ? vSpecular.a : 1;
            }
            if (fogMode) {
                const float dist = renderStates[kRangeFogEnable] ? std::sqrt(dot3(eye, eye)) : eye[2];
                float f = 1;
                if (fogMode == 3)
                    f = fogEnd != fogStart ? (fogEnd - dist) / (fogEnd - fogStart) : 1;
                else if (fogMode == 1)
                    f = std::exp(-fogDensity * dist);
                else if (fogMode == 2)
                    f = std::exp(-(fogDensity * dist) * (fogDensity * dist));
                o.specular.a = std::clamp(f, 0.0f, 1.0f);
            }
        }
        // Each stage's coordinates: a set of the vertex's, or generated.
        for (int st = 0; st < stages; ++st) {
            const uint32_t index = stageStates[size_t(st)][kTexCoordIndex];
            const uint32_t set = index & 0xffff;
            const uint32_t gen = index >> 16;
            float coord[4] = {0, 0, 1, 0};
            if (gen && !layout.rhw) {
                float g[3] = {0, 0, 0};
                if (gen == 1) { // CAMERASPACENORMAL
                    g[0] = normal[0], g[1] = normal[1], g[2] = normal[2];
                } else if (gen == 2) { // CAMERASPACEPOSITION
                    g[0] = eye[0], g[1] = eye[1], g[2] = eye[2];
                } else { // CAMERASPACEREFLECTIONVECTOR
                    float e[3] = {eye[0], eye[1], eye[2]};
                    normalize3(e);
                    const float k = 2 * dot3(e, normal);
                    g[0] = e[0] - k * normal[0], g[1] = e[1] - k * normal[1], g[2] = e[2] - k * normal[2];
                }
                coord[0] = g[0], coord[1] = g[1], coord[2] = g[2], coord[3] = 1;
            } else if (int(set) < layout.texSets) {
                const uint8_t *t = v + layout.texAt[set];
                coord[0] = vertexFloat(t);
                if (layout.texSize[set] > 1)
                    coord[1] = vertexFloat(t + 4);
            }
            const uint32_t transformFlags = stageStates[size_t(st)][kTextureTransformFlags];
            if ((transformFlags & 0xff) && !layout.rhw) {
                float r[4];
                transformPoint(transforms[16 + uint32_t(st)], coord, r);
                if ((transformFlags & 0x100) && r[(transformFlags & 0xff) - 1] != 0) { // PROJECTED
                    const float q = r[(transformFlags & 0xff) - 1];
                    r[0] /= q, r[1] /= q;
                }
                coord[0] = r[0], coord[1] = r[1];
            }
            o.tex[st][0] = coord[0], o.tex[st][1] = coord[1];
        }
    }
}

ClipVertex Device::project(const ClipVertex &v) const {
    ClipVertex p = v;
    const float rhw = 1 / v.w;
    p.x = float(viewport.x) + (v.x * rhw + 1) * float(viewport.width) / 2;
    p.y = float(viewport.y) + (1 - v.y * rhw) * float(viewport.height) / 2;
    p.z = viewport.minZ + v.z * rhw * (viewport.maxZ - viewport.minZ);
    p.w = rhw;
    return p;
}

// --- Primitives

void Device::draw(uint32_t primitive, uint32_t fvf, uint32_t vertices, uint32_t stride, uint32_t vertexCount,
                  uint32_t indices, uint32_t indexCount) {
    const VertexLayout layout(fvf);
    if (!layout.position || !target || !vertexCount)
        return;
    projected = layout.rhw;
    stages = 0;
    while (stages < 8 && stageStates[size_t(stages)][kColorOp] != 1)
        ++stages;
    for (int st = 0; st < stages; ++st)
        bound[st] = texture(st);
    configure();
    std::vector<ClipVertex> v;
    transformAndLight(layout, vertices, stride, vertexCount, v);
    if (v.empty())
        return;
    Cpu &c = s->cpu;
    std::vector<uint32_t> order;
    if (indices) {
        if (!c.valid(indices, indexCount * 2))
            return;
        order.resize(indexCount);
        for (uint32_t i = 0; i < indexCount; ++i) {
            order[i] = c.read16(indices + 2 * i);
            if (order[i] >= v.size())
                return;
        }
    } else {
        order.resize(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
            order[i] = i;
    }
    const size_t n = order.size();
    batching_ = primitive >= 4 && primitive <= 6; // triangles: gathered, then drawn on several threads
    switch (primitive) {
    case 1: // points
        for (size_t i = 0; i < n; ++i)
            point(v[order[i]]);
        break;
    case 2: // line list
        for (size_t i = 0; i + 1 < n; i += 2)
            line(v[order[i]], v[order[i + 1]]);
        break;
    case 3: // line strip
        for (size_t i = 0; i + 1 < n; ++i)
            line(v[order[i]], v[order[i + 1]]);
        break;
    case 4: // triangle list
        for (size_t i = 0; i + 2 < n; i += 3)
            triangle(v[order[i]], v[order[i + 1]], v[order[i + 2]]);
        break;
    case 5: // triangle strip: every other one turned round to keep the winding
        for (size_t i = 0; i + 2 < n; ++i)
            if (i & 1)
                triangle(v[order[i + 1]], v[order[i]], v[order[i + 2]]);
            else
                triangle(v[order[i]], v[order[i + 1]], v[order[i + 2]]);
        break;
    case 6: // triangle fan
        for (size_t i = 1; i + 1 < n; ++i)
            triangle(v[order[0]], v[order[i]], v[order[i + 1]]);
        break;
    default:
        break;
    }
    batching_ = false;
    drawBatch();
    target->touched();
    if (depth)
        depth->touched();
}

// The draws given and not yet drawn, drawn in order by threads of their
// own while the program goes on: as many as there are processors, up to
// 8. Each draws only its own rows (groups of 8, dealt round), every draw in
// turn, so each pixel is drawn by one thread, in the order one alone would
// draw it. Each goes through the draws at its own pace: a thread with less
// to draw in one moves on to the next without waiting for the others, and
// a draw is done once every thread is past it. (Drawing draws in batches,
// every thread waiting for the slowest at the end of each, a profile at
// 1024x768 found the threads idle a third of the time.)
struct DrawQueue {
    struct Job {
        Draw draw;
        std::vector<std::array<ClipVertex, 3>> triangles;
        double area; // roughly, by the triangles' bounding boxes
        // Instead of triangles: filling the rows of one band of bands (a
        // clear), in turn with the draws.
        std::function<void(int band, int bands)> fill = {};
    };
    explicit DrawQueue(DirectX::DrawThreads) {
        const int n = std::clamp(int(std::thread::hardware_concurrency()), 1, 8);
        passed_.assign(size_t(n), 0);
        for (int i = 0; i < n; ++i)
            threads_.emplace_back([this, i, n] { work(i, n); });
    }
    ~DrawQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        for (std::thread &t : threads_)
            t.join();
    }
    void push(Job job) {
        auto shared = std::make_shared<const Job>(std::move(job));
        bool wake;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back(std::move(shared));
            ++pushed_;
            wake = sleeping_ > 0;
        }
        if (wake)
            wake_.notify_all();
    }
    // How many jobs have been pushed: a mark finishThrough can wait for.
    uint64_t pushed() {
        std::lock_guard<std::mutex> lock(mutex_);
        return pushed_;
    }
    // Waits for the first `count` jobs pushed to be drawn.
    void finishThrough(uint64_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return drawn_ >= count; });
    }
    // Waits for every job pushed to be drawn.
    void finish() {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return drawn_ == pushed_; });
    }
    // Draws jobs in order on this thread (DrawThreads::One).
    static void draw(const std::vector<Job> &jobs, DirectX::DrawThreads) {
        Draw::Raster r;
        for (const Job &j : jobs) {
            if (j.fill)
                j.fill(0, 1);
            for (const auto &t : j.triangles)
                j.draw.rasterize(r, t[0], t[1], t[2]);
        }
    }

private:
    void work(int band, int bands) {
        Draw::Raster r;
        r.band = band, r.bands = bands;
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            while (passed_[size_t(band)] == pushed_) {
                if (quit_)
                    return;
                ++sleeping_;
                wake_.wait(lock);
                --sleeping_;
            }
            std::shared_ptr<const Job> job = jobs_[size_t(passed_[size_t(band)] - first_)];
            lock.unlock();
            if (job->fill)
                job->fill(band, bands);
            for (const auto &t : job->triangles)
                job->draw.rasterize(r, t[0], t[1], t[2]);
            job.reset();
            lock.lock();
            ++passed_[size_t(band)];
            const uint64_t least = *std::min_element(passed_.begin(), passed_.end());
            if (least > drawn_) {
                // Every thread is past these: let them go (their surfaces
                // with them), not under the lock.
                std::vector<std::shared_ptr<const Job>> gone;
                while (first_ < least) {
                    gone.push_back(std::move(jobs_.front()));
                    jobs_.pop_front();
                    ++first_;
                }
                drawn_ = least;
                lock.unlock();
                done_.notify_all();
                gone.clear();
                lock.lock();
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::deque<std::shared_ptr<const Job>> jobs_; // from job number first_ on
    uint64_t first_ = 0, pushed_ = 0, drawn_ = 0;
    std::vector<uint64_t> passed_; // by thread: the jobs it's past
    int sleeping_ = 0;
    bool quit_ = false;
    std::vector<std::thread> threads_;
};

Device::~Device() {
    finishDraws();
    if (s) {
        s->devices.erase(this);
        s->lateDraws.erase(this);
    }
}

void Device::finishDraws() {
    if (queue_)
        queue_->finish();
}

uint64_t Device::drawsQueued() {
    return queue_ ? queue_->pushed() : 0;
}

void Device::finishDrawsThrough(uint64_t count) {
    if (queue_)
        queue_->finishThrough(count);
}

Draw Device::current() const {
    Draw d;
    d.cfg_ = cfg_;
    d.stages = stages;
    std::copy(std::begin(bound), std::end(bound), d.bound);
    d.viewport = viewport;
    d.target = target;
    d.depth = depth;
    return d;
}

// The draw's triangles: drawn on a thread of their own (DrawQueue), in
// turn with the draws before them, while the program goes on; with
// DirectX::DrawThreads::One, here and now.
void Device::drawBatch() {
    if (batch_.empty())
        return;
    double area = 0; // roughly, by bounding boxes
    for (const auto &t : batch_) {
        const auto [x0, x1] = std::minmax({t[0].x, t[1].x, t[2].x});
        const auto [y0, y1] = std::minmax({t[0].y, t[1].y, t[2].y});
        area += double(x1 - x0) * double(y1 - y0);
    }
    const DirectX::DrawThreads mode = s->owner.drawThreads;
    std::vector<DrawQueue::Job> job;
    job.push_back({current(), std::move(batch_), area});
    batch_.clear();
    if (mode == DirectX::DrawThreads::One) {
        DrawQueue::draw(job, mode);
        return;
    }
    if (!queue_)
        queue_ = std::make_shared<DrawQueue>(mode);
    queue_->push(std::move(job.front()));
}

// Clips a triangle to the view volume, projects it, culls it by its
// winding, and fills what's left.
void Device::triangle(const ClipVertex &a, const ClipVertex &b, const ClipVertex &c) {
    // Each plane adds at most one corner: 3 + 6 at most.
    struct Polygon {
        ClipVertex v[9];
        size_t n = 0;
        size_t size() const { return n; }
        ClipVertex &operator[](size_t i) { return v[i]; }
        const ClipVertex &operator[](size_t i) const { return v[i]; }
    } polys[2];
    Polygon *poly = &polys[0];
    poly->v[0] = a, poly->v[1] = b, poly->v[2] = c, poly->n = 3;
    if (!projected) {
        for (int plane = 0; plane < 6 && poly->n; ++plane) {
            Polygon &next = poly == &polys[0] ? polys[1] : polys[0];
            next.n = 0;
            for (size_t i = 0; i < poly->n; ++i) {
                const ClipVertex &p = (*poly)[i];
                const ClipVertex &q = (*poly)[(i + 1) % poly->n];
                const float dp = planeDistance(p, plane);
                const float dq = planeDistance(q, plane);
                if (dp >= 0)
                    next.v[next.n++] = p;
                if ((dp >= 0) != (dq >= 0))
                    next.v[next.n++] = lerp(p, q, dp / (dp - dq));
            }
            poly = &next;
        }
        if (poly->n < 3)
            return;
        for (size_t i = 0; i < poly->n; ++i) {
            if (poly->v[i].w <= 0)
                return;
            poly->v[i] = project(poly->v[i]);
        }
    }
    const Polygon &shape = *poly;
    // Winding on the screen (y down): positive is clockwise.
    float area = 0;
    for (size_t i = 0; i < shape.size(); ++i) {
        const ClipVertex &p = shape[i];
        const ClipVertex &q = shape[(i + 1) % shape.size()];
        area += p.x * q.y - q.x * p.y;
    }
    const uint32_t cull = renderStates[kCullMode];
    if ((cull == 2 && area > 0) || (cull == 3 && area < 0) || area == 0)
        return;
    for (size_t i = 1; i + 1 < shape.size(); ++i)
        batch_.push_back({shape[0], shape[i], shape[i + 1]});
    if (!batching_)
        drawBatch();
}

void Device::line(const ClipVertex &a0, const ClipVertex &b0) {
    ClipVertex a = a0;
    ClipVertex b = b0;
    if (!projected) {
        for (int plane = 0; plane < 6; ++plane) {
            const float da = planeDistance(a, plane);
            const float db = planeDistance(b, plane);
            if (da < 0 && db < 0)
                return;
            if (da < 0)
                a = lerp(a, b, da / (da - db));
            else if (db < 0)
                b = lerp(a, b, da / (da - db));
        }
        if (a.w <= 0 || b.w <= 0)
            return;
        a = project(a), b = project(b);
    }
    finishDraws(); // drawn here and now: after the draws before
    const Draw d = current();
    Draw::Raster r; // lines and points: the largest mip level
    d.prepareSamplers(r);
    const int steps = std::max(1, int(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y))));
    float attr[kAttributes];
    for (int i = 0; i <= steps; ++i) {
        const ClipVertex p = lerp(a, b, float(i) / float(steps));
        attr[0] = p.diffuse.r, attr[1] = p.diffuse.g, attr[2] = p.diffuse.b, attr[3] = p.diffuse.a;
        attr[4] = p.specular.r, attr[5] = p.specular.g, attr[6] = p.specular.b, attr[7] = p.specular.a;
        for (int st = 0; st < stages; ++st)
            attr[8 + 2 * st] = p.tex[st][0], attr[9 + 2 * st] = p.tex[st][1];
        d.pixel(r, int(std::lround(p.x)), int(std::lround(p.y)), p.z, p.w > 0 ? 1 / p.w : 1, attr);
    }
}

void Device::point(const ClipVertex &a0) {
    ClipVertex a = a0;
    if (!projected) {
        for (int plane = 0; plane < 6; ++plane)
            if (planeDistance(a, plane) < 0)
                return;
        a = project(a);
    }
    finishDraws();
    const Draw d = current();
    Draw::Raster r;
    d.prepareSamplers(r);
    float attr[kAttributes];
    attr[0] = a.diffuse.r, attr[1] = a.diffuse.g, attr[2] = a.diffuse.b, attr[3] = a.diffuse.a;
    attr[4] = a.specular.r, attr[5] = a.specular.g, attr[6] = a.specular.b, attr[7] = a.specular.a;
    for (int st = 0; st < stages; ++st)
        attr[8 + 2 * st] = a.tex[st][0], attr[9 + 2 * st] = a.tex[st][1];
    d.pixel(r, int(std::lround(a.x)), int(std::lround(a.y)), a.z, a.w > 0 ? 1 / a.w : 1, attr);
}

// Fills a projected triangle: every pixel whose centre (whole
// coordinates) is inside, or on a top or left edge.
void Draw::rasterize(Raster &r, const ClipVertex &a, const ClipVertex &b, const ClipVertex &c) const {
    const int left = std::max(viewport.x, 0);
    const int top = std::max(viewport.y, 0);
    const int right = std::min(viewport.x + viewport.width, target->d.width);
    const int bottom = std::min(viewport.y + viewport.height, target->d.height);
    const int x0 = std::max(left, int(std::ceil(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(right - 1, int(std::floor(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(top, int(std::ceil(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(bottom - 1, int(std::floor(std::max({a.y, b.y, c.y}))));
    if (x0 > x1 || y0 > y1)
        return;
    if (r.bands > 1) { // none of this thread's rows: nothing to set up
        bool mine = false;
        for (int g = y0 >> 3; g <= y1 >> 3 && !mine; ++g)
            mine = g % r.bands == r.band;
        if (!mine)
            return;
    }
    // Edge functions, oriented so the inside is positive whatever the winding.
    double area = double(b.x - a.x) * (c.y - a.y) - double(c.x - a.x) * (b.y - a.y);
    if (area == 0)
        return;
    const ClipVertex *v[3] = {&a, &b, &c};
    const double sign = area > 0 ? 1 : -1;
    area *= sign;
    struct Edge {
        double a, b, c;
        bool topLeft;
    } e[3];
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &p = *v[(i + 1) % 3];
        const ClipVertex &q = *v[(i + 2) % 3];
        // Positive on the side of v[i].
        e[i].a = sign * (p.y - q.y);
        e[i].b = sign * (q.x - p.x);
        e[i].c = sign * (double(p.x) * q.y - double(q.x) * p.y);
        e[i].topLeft = e[i].a > 0 || (e[i].a == 0 && e[i].b > 0);
    }
    // Attributes divided by w, and 1/w, to interpolate in screen space.
    float attr[3][kAttributes];
    const int count = 8 + 2 * stages;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &p = *v[i];
        const float rhw = p.w;
        const float plain[8] = {p.diffuse.r,  p.diffuse.g,  p.diffuse.b,  p.diffuse.a,
                                p.specular.r, p.specular.g, p.specular.b, p.specular.a};
        for (int k = 0; k < 8; ++k)
            attr[i][k] = plain[k] * rhw;
        for (int st = 0; st < stages; ++st)
            attr[i][8 + 2 * st] = p.tex[st][0] * rhw, attr[i][9 + 2 * st] = p.tex[st][1] * rhw;
    }
    // Each stage's mip level for the triangle: texels per pixel.
    for (int st = 0; st < stages; ++st) {
        r.lod[st] = 0;
        const DecodedTexture *t = bound[st];
        if (!t || t->levels.empty())
            continue;
        const double du1 = (b.tex[st][0] - a.tex[st][0]) * t->levels[0].width;
        const double dv1 = (b.tex[st][1] - a.tex[st][1]) * t->levels[0].height;
        const double du2 = (c.tex[st][0] - a.tex[st][0]) * t->levels[0].width;
        const double dv2 = (c.tex[st][1] - a.tex[st][1]) * t->levels[0].height;
        const double texels = std::fabs(du1 * dv2 - du2 * dv1);
        if (texels > 0)
            r.lod[st] = float(0.5 * std::log2(texels / area));
    }
    prepareSamplers(r);
    // z, 1/w and the attributes (divided by w) are planes on the screen:
    // f = fx x + fy y + f0.
    const int n = 2 + count;
    float values[3][2 + kAttributes];
    for (int i = 0; i < 3; ++i) {
        values[i][0] = v[i]->z;
        values[i][1] = v[i]->w;
        for (int k = 0; k < count; ++k)
            values[i][2 + k] = attr[i][k];
    }
    float fx[2 + kAttributes];
    float fy[2 + kAttributes];
    float f0[2 + kAttributes];
    for (int k = 0; k < n; ++k) {
        fx[k] = float((e[0].a * values[0][k] + e[1].a * values[1][k] + e[2].a * values[2][k]) / area);
        fy[k] = float((e[0].b * values[0][k] + e[1].b * values[1][k] + e[2].b * values[2][k]) / area);
        f0[k] = float((e[0].c * values[0][k] + e[1].c * values[1][k] + e[2].c * values[2][k]) / area);
    }
    float flatColors[8];
    for (int k = 0; k < 8; ++k)
        flatColors[k] = attr[0][k] / a.w; // the first vertex's colours
    float row[2 + kAttributes];
    float here[kAttributes];
    const bool quick = span4Fits();
    for (int y = y0; y <= y1; ++y) {
        if (r.bands > 1 && (y >> 3) % r.bands != r.band) // another thread's row
            continue;
        // The row's span: where each edge is inside, then each pixel there
        // tested exactly.
        double from = x0;
        double to = x1;
        bool empty = false;
        for (const Edge &ed : e) {
            const double rest = ed.b * y + ed.c;
            if (ed.a > 0)
                from = std::max(from, std::floor(-rest / ed.a) - 1);
            else if (ed.a < 0)
                to = std::min(to, std::ceil(-rest / ed.a) + 1);
            else if (rest < 0 || (rest == 0 && !ed.topLeft))
                empty = true;
        }
        if (empty || from > to)
            continue;
        const int xs = int(from);
        const int xe = int(to);
        for (int k = 0; k < n; ++k)
            row[k] = f0[k] + fy[k] * float(y) + fx[k] * float(xs);
        // The pixels inside: each edge's test only changes once along the
        // row, so they run from the first inside to the last.
        auto inside = [&](int x) {
            for (const Edge &ed : e) {
                const double w = ed.a * x + ed.b * y + ed.c;
                if (!(w > 0 || (w == 0 && ed.topLeft)))
                    return false;
            }
            return true;
        };
        int first = xs;
        int last = xe;
        while (first <= last && !inside(first))
            ++first;
        while (last >= first && !inside(last))
            --last;
        for (int x = xs; x < first; ++x)
            for (int k = 0; k < n; ++k)
                row[k] += fx[k];
        int x = first;
        if (quick)
            x = span4(r, x, last, y, row, fx, n, count, flatColors);
        for (; x <= last; ++x) {
            uint8_t *zAt;
            uint32_t zNew;
            if (depthTest(x, y, row[0], zAt, zNew)) { // the attributes only for pixels that pass
                const float rhw = row[1];
                const float inv = rhw != 0 ? 1 / rhw : 0;
                for (int k = 0; k < count; ++k)
                    here[k] = row[2 + k] * inv;
                if (cfg_.flat)
                    std::copy(flatColors, flatColors + 8, here);
                shade(r, x, y, row[0], inv, here, zAt, zNew);
            }
            for (int k = 0; k < n; ++k)
                row[k] += fx[k];
        }
    }
}

bool Draw::span4Fits() const {
#ifdef RELUVA_SPAN4
    const PixelConfig &cf = cfg_;
    return cf.simpleStages && !cf.colorKey &&
           (cf.target == PixelConfig::Target::Rgb565 || cf.target == PixelConfig::Target::Rgb888) &&
           (!cf.blend || (cf.srcBlend == 5 && cf.destBlend == 6)) &&
           (!cf.fog || cf.fogTableMode == 0 || cf.fogTableMode == 3);
#else
    return false;
#endif
}

#ifdef RELUVA_SPAN4
namespace {
// floorInt, four at a time.
__m128i floor4(__m128 v) {
    const __m128i i = _mm_cvttps_epi32(v);
    return _mm_add_epi32(i, _mm_castps_si128(_mm_cmpgt_ps(_mm_cvtepi32_ps(i), v)));
}
// Each pixel's weight (0..256) in its four 16-bit channels: pixels 0 and 1
// (low) or 2 and 3 (high).
__m128i weights(__m128i w, bool high) {
    const __m128i w16 = _mm_packs_epi32(w, w);          // w0 w1 w2 w3 w0 w1 w2 w3
    const __m128i pairs = _mm_unpacklo_epi16(w16, w16); // w0 w0 w1 w1 w2 w2 w3 w3
    return high ? _mm_unpackhi_epi32(pairs, pairs) : _mm_unpacklo_epi32(pairs, pairs);
}
// sample's mix on 16-bit channels: (p (256 - w) + q w) >> 8.
__m128i mix16(__m128i p, __m128i q, __m128i w) {
    const __m128i iw = _mm_sub_epi16(_mm_set1_epi16(256), w);
    return _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(p, iw), _mm_mullo_epi16(q, w)), 8);
}
} // namespace

// sample, four at a time, for a texture that wraps both ways with sizes
// powers of two and no colour key: its texels are found by masks, as
// sample's are (they're the same texels where all four are inside). False
// for any other, or weights out of 0..256 (u or v beyond an int), left to
// sample.
bool Draw::sample4(const Raster &r, int stage, __m128 u, __m128 v, __m128i &out) const {
    const Sampler &sm = r.samplers[stage];
    if (!sm.argb || !sm.wrapPow2)
        return false;
    const __m128 fx = _mm_mul_ps(u, _mm_set1_ps(sm.fw));
    const __m128 fy = _mm_mul_ps(v, _mm_set1_ps(sm.fh));
    const __m128i mx = _mm_set1_epi32(sm.width - 1);
    const __m128i my = _mm_set1_epi32(sm.height - 1);
    alignas(16) int32_t xs[4];
    alignas(16) int32_t ys[4];
    alignas(16) uint32_t t[4][4]; // t00, t10, t01, t11
    if (!sm.linear) {
        _mm_store_si128(reinterpret_cast<__m128i *>(xs), _mm_and_si128(floor4(fx), mx));
        _mm_store_si128(reinterpret_cast<__m128i *>(ys), _mm_and_si128(floor4(fy), my));
        for (int i = 0; i < 4; ++i)
            t[0][i] = sm.argb[size_t(ys[i]) * size_t(sm.width) + size_t(xs[i])];
        out = _mm_load_si128(reinterpret_cast<const __m128i *>(t[0]));
        return true;
    }
    const __m128 gx = _mm_sub_ps(fx, _mm_set1_ps(0.5f));
    const __m128 gy = _mm_sub_ps(fy, _mm_set1_ps(0.5f));
    const __m128i ix = floor4(gx);
    const __m128i iy = floor4(gy);
    const __m128 k256 = _mm_set1_ps(256.0f);
    const __m128i ax = _mm_cvttps_epi32(_mm_mul_ps(_mm_sub_ps(gx, _mm_cvtepi32_ps(ix)), k256));
    const __m128i ay = _mm_cvttps_epi32(_mm_mul_ps(_mm_sub_ps(gy, _mm_cvtepi32_ps(iy)), k256));
    // Out of 0..256 (only from coordinates no int holds): sample's way.
    const __m128i none = _mm_setzero_si128();
    const __m128i most = _mm_set1_epi32(256);
    const __m128i bad = _mm_or_si128(_mm_or_si128(_mm_cmpgt_epi32(none, ax), _mm_cmpgt_epi32(ax, most)),
                                     _mm_or_si128(_mm_cmpgt_epi32(none, ay), _mm_cmpgt_epi32(ay, most)));
    if (_mm_movemask_epi8(bad))
        return false;
    alignas(16) int32_t x1s[4];
    alignas(16) int32_t y1s[4];
    const __m128i one = _mm_set1_epi32(1);
    _mm_store_si128(reinterpret_cast<__m128i *>(xs), _mm_and_si128(ix, mx));
    _mm_store_si128(reinterpret_cast<__m128i *>(x1s), _mm_and_si128(_mm_add_epi32(ix, one), mx));
    _mm_store_si128(reinterpret_cast<__m128i *>(ys), _mm_and_si128(iy, my));
    _mm_store_si128(reinterpret_cast<__m128i *>(y1s), _mm_and_si128(_mm_add_epi32(iy, one), my));
    for (int i = 0; i < 4; ++i) {
        const uint32_t *r0 = sm.argb + size_t(ys[i]) * size_t(sm.width);
        const uint32_t *r1 = sm.argb + size_t(y1s[i]) * size_t(sm.width);
        t[0][i] = r0[xs[i]], t[1][i] = r0[x1s[i]], t[2][i] = r1[xs[i]], t[3][i] = r1[x1s[i]];
    }
    const __m128i zero = _mm_setzero_si128();
    __m128i half[2];
    for (int h = 0; h < 2; ++h) {
        auto unpack16 = [&](const uint32_t *p) {
            const __m128i v4 = _mm_load_si128(reinterpret_cast<const __m128i *>(p));
            return h ? _mm_unpackhi_epi8(v4, zero) : _mm_unpacklo_epi8(v4, zero);
        };
        const __m128i wx = weights(ax, h == 1);
        const __m128i wy = weights(ay, h == 1);
        half[h] = mix16(mix16(unpack16(t[0]), unpack16(t[1]), wx), mix16(unpack16(t[2]), unpack16(t[3]), wx), wy);
    }
    out = _mm_packus_epi16(half[0], half[1]);
    return true;
}
#endif

// The pixel loop, depthTest, shade's quick stages and finish, four pixels
// at a time in SSE: the same operations in the same order on each, so the
// same results to the bit (no fused multiply-adds either way). Texels and
// the depth test are still taken one pixel at a time.
int Draw::span4(const Raster &r, int x, int last, int y, float *row, const float *fx, int n, int count,
                const float *flatColors) const {
#ifdef RELUVA_SPAN4
    const PixelConfig &cf = cfg_;
    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 half = _mm_set1_ps(0.5f);
    // std::clamp(v, 0, 1), NaN kept as it does: (v < 0) ? 0 : (1 < v) ? 1 : v.
    auto clamp01 = [&](__m128 v) { return _mm_max_ps(zero, _mm_min_ps(one, v)); };
    // uint32_t(std::clamp(v, 0, 1) * max + 0.5f); a NaN gives 0, as the
    // scalar conversion (through 64 bits) does.
    auto channel = [&](__m128 v, float max) {
        const __m128 c = _mm_add_ps(_mm_mul_ps(clamp01(v), _mm_set1_ps(max)), half);
        return _mm_and_si128(_mm_cvttps_epi32(c), _mm_castps_si128(_mm_cmpord_ps(c, c)));
    };
    const __m128 of255 = _mm_set1_ps(255.0f);
    // A channel of four texels as kFractions.of255 has it: float(i) / 255.
    auto fractions = [&](__m128i t, int shift) {
        return _mm_div_ps(_mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(t, shift), _mm_set1_epi32(0xff))), of255);
    };
    const bool rgb565 = cf.target == PixelConfig::Target::Rgb565;
    alignas(16) float lane[2 + kAttributes][4];
    alignas(16) uint32_t texel[4];
    alignas(16) int32_t out[3][4];
    alignas(16) float dst[3][4];
    for (; x + 3 <= last; x += 4) {
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < n; ++k) {
                lane[k][i] = row[k];
                row[k] += fx[k];
            }
        uint8_t *zAt[4];
        uint32_t zNew[4];
        bool pass[4];
        int passing = 0;
        if (cf.zTest && cf.zBytes == 2 && x >= 0 && y >= 0 && x + 3 < cf.targetWidth && y < cf.targetHeight &&
            x + 3 < cf.depthWidth && y < cf.depthHeight) {
            // depthTest on a 16-bit depth buffer, four at a time:
            // uint32_t(double(clamp(z, 0, 1)) * zMax + 0.5) against what's there.
            const __m128 z = clamp01(_mm_load_ps(lane[0]));
            const __m128d zMax = _mm_set1_pd(cf.zMax);
            const __m128d halfD = _mm_set1_pd(0.5);
            auto depth2 = [&](__m128 two) {
                const __m128d d = _mm_add_pd(_mm_mul_pd(_mm_cvtps_pd(two), zMax), halfD);
                return _mm_cvttpd_epi32(_mm_and_pd(d, _mm_cmpord_pd(d, d))); // two, low; a NaN gives 0
            };
            const __m128i zn = _mm_unpacklo_epi64(depth2(z), depth2(_mm_movehl_ps(z, z)));
            uint8_t *base = cf.depthAt + uint32_t(y * cf.depthPitch) + uint32_t(x) * 2;
            const __m128i stored = _mm_and_si128(
                _mm_unpacklo_epi16(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(base)), _mm_setzero_si128()),
                _mm_set1_epi32(int(cf.zMask)));
            __m128i ok;
            switch (cf.zFunc) { // compare: new against stored, both whole numbers
            case 1:
                ok = _mm_setzero_si128();
                break;
            case 2:
                ok = _mm_cmplt_epi32(zn, stored);
                break;
            case 3:
                ok = _mm_cmpeq_epi32(zn, stored);
                break;
            case 4:
                ok = _mm_xor_si128(_mm_cmpgt_epi32(zn, stored), _mm_set1_epi32(-1));
                break;
            case 5:
                ok = _mm_cmpgt_epi32(zn, stored);
                break;
            case 6:
                ok = _mm_xor_si128(_mm_cmpeq_epi32(zn, stored), _mm_set1_epi32(-1));
                break;
            case 7:
                ok = _mm_xor_si128(_mm_cmplt_epi32(zn, stored), _mm_set1_epi32(-1));
                break;
            default:
                ok = _mm_set1_epi32(-1);
                break;
            }
            alignas(16) uint32_t zs[4];
            alignas(16) uint32_t oks[4];
            _mm_store_si128(reinterpret_cast<__m128i *>(zs), zn);
            _mm_store_si128(reinterpret_cast<__m128i *>(oks), ok);
            for (int i = 0; i < 4; ++i) {
                zAt[i] = base + 2 * i, zNew[i] = zs[i];
                passing += pass[i] = oks[i] != 0;
            }
        } else
            for (int i = 0; i < 4; ++i)
                passing += pass[i] = depthTest(x + i, y, lane[0][i], zAt[i], zNew[i]);
        if (!passing)
            continue;
        // inv = rhw != 0 ? 1 / rhw : 0, and the attributes times it.
        const __m128 rhw = _mm_load_ps(lane[1]);
        const __m128 inv = _mm_and_ps(_mm_div_ps(one, rhw), _mm_cmpneq_ps(rhw, zero));
        __m128 here[kAttributes];
        for (int k = 0; k < count; ++k)
            here[k] = _mm_mul_ps(_mm_load_ps(lane[2 + k]), inv);
        if (cf.flat)
            for (int k = 0; k < 8; ++k)
                here[k] = _mm_set1_ps(flatColors[k]);
        // Texture stages. Sources: diffuse, current, texture, factor, specular.
        __m128 src[5][4] = {
            {here[0], here[1], here[2], here[3]},
            {here[0], here[1], here[2], here[3]},
            {},
            {_mm_set1_ps(cf.factor.r), _mm_set1_ps(cf.factor.g), _mm_set1_ps(cf.factor.b), _mm_set1_ps(cf.factor.a)},
            {here[4], here[5], here[6], here[7]}};
        for (int st = 0; st < stages; ++st) {
            const StageConfig &sc = cf.stage[st];
            if (sc.usesTexture) {
                __m128i t;
                if (!sample4(r, st, here[8 + 2 * st], here[9 + 2 * st], t)) {
                    alignas(16) float u[4];
                    alignas(16) float v[4];
                    _mm_store_ps(u, here[8 + 2 * st]);
                    _mm_store_ps(v, here[9 + 2 * st]);
                    for (int i = 0; i < 4; ++i)
                        texel[i] = pass[i] ? sample(r, st, u[i], v[i], nullptr) : 0;
                    t = _mm_load_si128(reinterpret_cast<const __m128i *>(texel));
                }
                src[2][0] = fractions(t, 16), src[2][1] = fractions(t, 8), src[2][2] = fractions(t, 0);
                src[2][3] = fractions(t, 24);
            }
            __m128 result[4];
            auto apply = [&](uint32_t op, const __m128 *x1, const __m128 *x2, int from, int to) {
                for (int i = from; i < to; ++i)
                    result[i] = clamp01(op == 4 ? _mm_mul_ps(x1[i], x2[i]) : op == 2 ? x1[i] : x2[i]);
            };
            apply(sc.colorOp, src[sc.source[0]], src[sc.source[1]], 0, 3);
            if (sc.alphaOp != 1)
                apply(sc.alphaOp, src[sc.source[2]], src[sc.source[3]], 3, 4);
            else
                result[3] = src[1][3];
            for (int i = 0; i < 4; ++i)
                src[1][i] = result[i];
        }
        __m128 *current = src[1];
        // finish: specular, alpha test, fog, blending, the write.
        if (cf.specular)
            for (int i = 0; i < 3; ++i)
                current[i] = _mm_min_ps(_mm_add_ps(current[i], here[4 + i]), one); // std::min(1, v)
        if (cf.alphaTest) {
            alignas(16) float a[4];
            _mm_store_ps(a, current[3]);
            for (int i = 0; i < 4; ++i)
                if (pass[i] && !compare(cf.alphaFunc, float(floorInt(a[i] * 255 + 0.5f)), cf.alphaRef))
                    pass[i] = false;
        }
        if (cf.fog) {
            __m128 f = here[7]; // the specular alpha: the vertices' fog
            if (cf.fogTableMode == 3) {
                const __m128 d = cf.fogByZ ? _mm_load_ps(lane[0]) : inv;
                f = cf.fogEnd != cf.fogStart
                        ? _mm_div_ps(_mm_sub_ps(_mm_set1_ps(cf.fogEnd), d), _mm_set1_ps(cf.fogEnd - cf.fogStart))
                        : one;
            }
            f = clamp01(f);
            const __m128 nf = _mm_sub_ps(one, f);
            const float fog[3] = {cf.fogColor.r, cf.fogColor.g, cf.fogColor.b};
            for (int i = 0; i < 3; ++i)
                current[i] = _mm_add_ps(_mm_mul_ps(current[i], f), _mm_mul_ps(_mm_set1_ps(fog[i]), nf));
        }
        const uint32_t bytes = cf.targetBytes;
        uint8_t *at[4];
        for (int i = 0; i < 4; ++i)
            at[i] = cf.targetAt + uint32_t(y * cf.targetPitch) + uint32_t(x + i) * bytes;
        __m128 o[3] = {current[0], current[1], current[2]};
        if (cf.blend) {
            const Fractions &fr = kFractions;
            for (int i = 0; i < 4; ++i) {
                uint32_t raw = 0;
                std::memcpy(&raw, at[i], bytes);
                if (rgb565)
                    dst[0][i] = fr.of31[raw >> 11 & 31], dst[1][i] = fr.of63[raw >> 5 & 63],
                    dst[2][i] = fr.of31[raw & 31];
                else
                    dst[0][i] = fr.of255[raw >> 16 & 0xff], dst[1][i] = fr.of255[raw >> 8 & 0xff],
                    dst[2][i] = fr.of255[raw & 0xff];
            }
            const __m128 a = current[3];
            const __m128 ia = _mm_sub_ps(one, current[3]);
            for (int i = 0; i < 3; ++i)
                o[i] = _mm_add_ps(_mm_mul_ps(current[i], a), _mm_mul_ps(_mm_load_ps(dst[i]), ia));
        }
        if (rgb565) {
            _mm_store_si128(reinterpret_cast<__m128i *>(out[0]), channel(o[0], 31));
            _mm_store_si128(reinterpret_cast<__m128i *>(out[1]), channel(o[1], 63));
            _mm_store_si128(reinterpret_cast<__m128i *>(out[2]), channel(o[2], 31));
        } else
            for (int i = 0; i < 3; ++i)
                _mm_store_si128(reinterpret_cast<__m128i *>(out[i]), channel(o[i], 255));
        for (int i = 0; i < 4; ++i) {
            if (!pass[i])
                continue;
            if (rgb565) {
                const uint16_t v = uint16_t(uint32_t(out[0][i]) << 11 | uint32_t(out[1][i]) << 5 | uint32_t(out[2][i]));
                std::memcpy(at[i], &v, 2);
            } else {
                const uint32_t v = uint32_t(out[0][i]) << 16 | uint32_t(out[1][i]) << 8 | uint32_t(out[2][i]);
                std::memcpy(at[i], &v, 4);
            }
            if (zAt[i] && cf.zWrite) {
                if (cf.zBytes == 2)
                    std::memcpy(zAt[i], &zNew[i], 2);
                else
                    std::memcpy(zAt[i], &zNew[i], cf.zBytes);
            }
        }
    }
#else
    (void)r, (void)last, (void)y, (void)row, (void)fx, (void)n, (void)count, (void)flatColors;
#endif
    return x;
}

// --- Textures

const DecodedTexture *Device::texture(int stage) {
    const std::shared_ptr<Surface> &t = textures[size_t(stage)];
    if (!t)
        return nullptr;
    // The surface and its smaller mip levels.
    std::vector<Surface *> levels{t.get()};
    for (Surface *l = t.get(); l;) {
        Surface *next = nullptr;
        for (const auto &a : l->attached)
            if (a->d.caps[0] & kCapsMipMap)
                next = a.get();
        if (next)
            levels.push_back(next);
        l = next;
    }
    DecodedTexture &d = decoded[t.get()];
    bool fresh = d.owner.lock() == t && d.levels.size() == levels.size();
    for (size_t i = 0; fresh && i < levels.size(); ++i)
        fresh = d.versions[i] == levels[i]->d.version;
    if (fresh)
        return &d;
    finishDraws();             // a draw waiting may use the texture as it was decoded
    if (decoded.size() > 4096) // forget textures since freed
        for (auto it = decoded.begin(); it != decoded.end();)
            it = it->second.owner.expired() ? decoded.erase(it) : std::next(it);
    DecodedTexture &fill = decoded[t.get()];
    fill.owner = t;
    fill.levels.clear();
    fill.versions.clear();
    for (Surface *l : levels) {
        DecodedTexture::Level level{l->d.width, l->d.height, {}, {}};
        level.argb.resize(size_t(l->d.width) * size_t(l->d.height));
        level.keyed.assign(level.argb.size(), 0);
        const uint32_t *pal = l->paletteArgb() ? l->paletteArgb() : t->paletteArgb();
        const ArgbConverter convert(l->d.format, pal);
        for (int y = 0; y < l->d.height; ++y)
            for (int x = 0; x < l->d.width; ++x) {
                const uint32_t raw = l->pixel(x, y);
                const size_t at = size_t(y) * size_t(l->d.width) + size_t(x);
                level.argb[at] = convert(raw);
                if (t->hasSrcKey && raw >= t->srcKeyLow && raw <= t->srcKeyHigh)
                    level.keyed[at] = 1;
            }
        fill.levels.push_back(std::move(level));
        fill.versions.push_back(l->d.version);
    }
    return &fill;
}

void Draw::prepareSamplers(Raster &r) const {
    for (int st = 0; st < stages; ++st) {
        Sampler &sm = r.samplers[st];
        const DecodedTexture *t = bound[st];
        if (!t || t->levels.empty()) {
            sm.argb = nullptr;
            continue;
        }
        const StageConfig &sc = cfg_.stage[st];
        const float l = r.lod[st];
        size_t level = 0;
        if (sc.mipFilter >= 2 && l > 0)
            level = std::min(t->levels.size() - 1, size_t(sc.mipFilter == 3 ? std::lround(l) : int(l)));
        const DecodedTexture::Level &tex = t->levels[level];
        sm.argb = tex.argb.data();
        sm.keyed = tex.keyed.data();
        sm.width = tex.width, sm.height = tex.height;
        sm.fw = float(tex.width), sm.fh = float(tex.height);
        sm.linear = (l > 0 ? sc.minFilter : sc.magFilter) >= 2;
        sm.addressU = sc.addressU, sm.addressV = sc.addressV, sm.border = sc.border;
        auto pow2 = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
        sm.wrapPow2 = sc.addressU <= 1 && sc.addressV <= 1 && pow2(tex.width) && pow2(tex.height);
        sm.clampBoth = sc.addressU == 3 && sc.addressV == 3;
    }
}

RELUVA_PIXEL_STEP uint32_t Draw::sample(const Raster &r, int stage, float u, float v, bool *keyed) const {
    const Sampler &sm = r.samplers[stage];
    if (!sm.argb)
        return 0xffffffffu;
    auto address = [](int i, int size, uint32_t mode, bool &border) {
        if (mode <= 1) // wrap: the common case first
            return i >= 0 && i < size ? i : ((i % size) + size) % size;
        switch (mode) {
        case 2: { // mirror
            const int period = 2 * size;
            int m = ((i % period) + period) % period;
            return m < size ? m : period - 1 - m;
        }
        case 3:
            return std::clamp(i, 0, size - 1); // clamp
        default:                               // border
            if (i < 0 || i >= size)
                border = true;
            return std::clamp(i, 0, size - 1);
        }
    };
    auto texel = [&](int x, int y, bool &isKeyed) {
        if (sm.wrapPow2) {
            x &= sm.width - 1;
            y &= sm.height - 1;
        } else if (sm.clampBoth) {
            x = std::clamp(x, 0, sm.width - 1);
            y = std::clamp(y, 0, sm.height - 1);
        } else {
            bool border = false;
            x = address(x, sm.width, sm.addressU, border);
            y = address(y, sm.height, sm.addressV, border);
            if (border)
                return sm.border;
        }
        const size_t at = size_t(y) * size_t(sm.width) + size_t(x);
        if (keyed)
            isKeyed = isKeyed || sm.keyed[at];
        return sm.argb[at];
    };
    const float fx = u * sm.fw;
    const float fy = v * sm.fh;
    if (!sm.linear) {
        bool k = false;
        const uint32_t r = texel(floorInt(fx), floorInt(fy), k);
        if (keyed)
            *keyed = k;
        return r;
    }
    const float gx = fx - 0.5f;
    const float gy = fy - 0.5f;
    const int ix = floorInt(gx);
    const int iy = floorInt(gy);
    // 8-bit weights
    const int ax = int((gx - float(ix)) * 256);
    const int ay = int((gy - float(iy)) * 256);
    // Two channels at a time (0x00ff00ff masks), weights out of 256.
    auto mix = [](uint32_t p, uint32_t q, int w) {
        const uint32_t rb = ((p & 0xff00ff) * uint32_t(256 - w) + (q & 0xff00ff) * uint32_t(w)) >> 8 & 0xff00ff;
        const uint32_t ag = ((p >> 8 & 0xff00ff) * uint32_t(256 - w) + (q >> 8 & 0xff00ff) * uint32_t(w)) & 0xff00ff00;
        return rb | ag;
    };
    if (!keyed && ix >= 0 && iy >= 0 && ix + 1 < sm.width && iy + 1 < sm.height) { // all four inside: no addressing
        const uint32_t *p = sm.argb + size_t(iy) * size_t(sm.width) + size_t(ix);
        return mix(mix(p[0], p[1], ax), mix(p[sm.width], p[sm.width + 1], ax), ay);
    }
    if (!keyed && sm.wrapPow2) { // wrapping across an edge: the texel's addressing, by masks
        const int mx = sm.width - 1;
        const int my = sm.height - 1;
        const size_t x0 = size_t(ix & mx);
        const size_t x1 = size_t((ix + 1) & mx);
        const uint32_t *r0 = sm.argb + size_t(iy & my) * size_t(sm.width);
        const uint32_t *r1 = sm.argb + size_t((iy + 1) & my) * size_t(sm.width);
        return mix(mix(r0[x0], r0[x1], ax), mix(r1[x0], r1[x1], ax), ay);
    }
    bool k00 = false;
    bool k10 = false;
    bool k01 = false;
    bool k11 = false;
    const uint32_t t00 = texel(ix, iy, k00);
    const uint32_t t10 = texel(ix + 1, iy, k10);
    const uint32_t t01 = texel(ix, iy + 1, k01);
    const uint32_t t11 = texel(ix + 1, iy + 1, k11);
    // The colour key keys the nearest texel's colour.
    if (keyed)
        *keyed = ax < 128 ? (ay < 128 ? k00 : k01) : (ay < 128 ? k10 : k11);
    return mix(mix(t00, t10, ax), mix(t01, t11, ax), ay);
}

// --- Pixels

void Device::configure() {
    PixelConfig &c = cfg_;
    c.zTest = renderStates[kZEnable] && depth;
    c.zWrite = c.zTest && renderStates[kZWriteEnable];
    c.zFunc = renderStates[kZFunc];
    uint8_t *mem = s->cpu.memory().data();
    c.targetAt = mem + target->d.memory;
    c.targetWidth = target->d.width, c.targetHeight = target->d.height, c.targetPitch = target->d.pitch;
    c.targetBytes = target->d.format.bytes();
    if (depth) {
        c.depthAt = mem + depth->d.memory;
        c.depthWidth = depth->d.width, c.depthHeight = depth->d.height, c.depthPitch = depth->d.pitch;
        c.zMask = depth->d.format.g ? depth->d.format.g : 0xffff;
        c.zMax = double(c.zMask);
        c.zBytes = depth->d.format.bytes();
    }
    c.alphaTest = renderStates[kAlphaTestEnable] != 0;
    c.alphaFunc = renderStates[kAlphaFunc];
    c.alphaRef = float(renderStates[kAlphaRef] & 0xff);
    c.blend = renderStates[kAlphaBlendEnable] != 0;
    c.srcBlend = renderStates[kSrcBlend], c.destBlend = renderStates[kDestBlend];
    if (c.srcBlend == 12) // BOTHSRCALPHA
        c.srcBlend = 5, c.destBlend = 6;
    else if (c.srcBlend == 13) // BOTHINVSRCALPHA
        c.srcBlend = 6, c.destBlend = 5;
    c.fog = renderStates[kFogEnable] != 0;
    c.fogTableMode = renderStates[kFogTableMode];
    c.fogStart = asFloat(renderStates[kFogStart]);
    c.fogEnd = asFloat(renderStates[kFogEnd]);
    c.fogDensity = asFloat(renderStates[kFogDensity]);
    c.fogColor = unpack(renderStates[kFogColor]);
    c.fogByZ = projected || transforms[3][15] != 0; // W-fog with a perspective projection
    c.colorKey = renderStates[kColorKeyEnable] != 0;
    c.specular = renderStates[kSpecularEnable] != 0;
    c.flat = renderStates[kShadeMode] == 1;
    c.factor = unpack(renderStates[kTextureFactor]);
    for (int i = 0; i < stages; ++i) {
        const auto &st = stageStates[size_t(i)];
        c.stage[i] = {st[kColorOp],
                      st[kColorArg1],
                      st[kColorArg2],
                      st[kAlphaOp],
                      st[kAlphaArg1],
                      st[kAlphaArg2],
                      st[kAddressU],
                      st[kAddressV],
                      st[kBorderColor],
                      st[kMagFilter],
                      st[kMinFilter],
                      st[kMipFilter],
                      {},
                      false};
    }
    // The quick path's stages.
    c.simpleStages = true;
    for (int i = 0; i < stages; ++i) {
        StageConfig &sc = c.stage[i];
        const uint32_t args[4] = {sc.colorArg1, sc.colorArg2, sc.alphaArg1, sc.alphaArg2};
        sc.usesTexture = false;
        for (int k = 0; k < 4; ++k) {
            const uint32_t a = args[k] & 0xf;
            sc.source[k] = uint8_t(a == 0 || a == 2 || a == 3 || a == 4 ? a : 1);
            if (args[k] & 0x30)
                c.simpleStages = false;
        }
        const bool alphaUsed = sc.alphaOp != 1;
        sc.usesTexture =
            sc.source[0] == 2 || sc.source[1] == 2 || (alphaUsed && (sc.source[2] == 2 || sc.source[3] == 2));
        if (sc.colorOp < 2 || sc.colorOp > 4 || sc.alphaOp < 1 || sc.alphaOp > 4)
            c.simpleStages = false;
    }
    const PixelFormat &f = target->d.format;
    c.target = f.flags & (kPfPaletteIndexed8 | kPfAlphaPixels)                   ? PixelConfig::Target::Generic
               : f.bits == 16 && f.r == 0xf800 && f.g == 0x07e0 && f.b == 0x001f ? PixelConfig::Target::Rgb565
               : f.bits == 32 && f.r == 0xff0000 && f.g == 0xff00 && f.b == 0xff ? PixelConfig::Target::Rgb888
                                                                                 : PixelConfig::Target::Generic;
}

void Draw::pixel(Raster &r, int x, int y, float z, float eyeW, const float *attr) const {
    uint8_t *zAt;
    uint32_t zNew;
    if (depthTest(x, y, z, zAt, zNew))
        shade(r, x, y, z, eyeW, attr, zAt, zNew);
}

// Whether a pixel is on the target and passes the depth test; where its
// depth is, and the depth it would write.
RELUVA_PIXEL_STEP bool Draw::depthTest(int x, int y, float z, uint8_t *&zAt, uint32_t &zNew) const {
    zAt = nullptr;
    zNew = 0;
    const PixelConfig &cf = cfg_;
    if (x < 0 || y < 0 || x >= cf.targetWidth || y >= cf.targetHeight)
        return false;
    if (cf.zTest && x < cf.depthWidth && y < cf.depthHeight) {
        zAt = cf.depthAt + uint32_t(y * cf.depthPitch) + uint32_t(x) * cf.zBytes;
        uint32_t raw = 0;
        if (cf.zBytes == 2)
            std::memcpy(&raw, zAt, 2);
        else if (cf.zBytes == 4)
            std::memcpy(&raw, zAt, 4);
        else
            std::memcpy(&raw, zAt, cf.zBytes);
        zNew = uint32_t(double(std::clamp(z, 0.0f, 1.0f)) * cf.zMax + 0.5); // >= 0: truncating is flooring
        if (!compare(cf.zFunc, float(zNew), float(raw & cf.zMask)))
            return false;
    }
    return true;
}

// A pixel past the depth test: texture stages, then the rest (finish).
RELUVA_PIXEL_STEP void Draw::shade(const Raster &r, int x, int y, float z, float eyeW, const float *attr, uint8_t *zAt,
                                   uint32_t zNew) const {
    const PixelConfig &cf = cfg_;
    // Texture stages.
    if (cf.simpleStages && !cf.colorKey) {
        // Sources: diffuse, current, texture, factor, specular.
        float src[5][4] = {{attr[0], attr[1], attr[2], attr[3]},
                           {attr[0], attr[1], attr[2], attr[3]},
                           {},
                           {cf.factor.r, cf.factor.g, cf.factor.b, cf.factor.a},
                           {attr[4], attr[5], attr[6], attr[7]}};
        for (int st = 0; st < stages; ++st) {
            const StageConfig &sc = cf.stage[st];
            if (sc.usesTexture) {
                const uint32_t t = sample(r, st, attr[8 + 2 * st], attr[9 + 2 * st], nullptr);
                const float *f = kFractions.of255;
                src[2][0] = f[t >> 16 & 0xff], src[2][1] = f[t >> 8 & 0xff], src[2][2] = f[t & 0xff];
                src[2][3] = f[t >> 24];
            }
            float result[4];
            auto apply = [&](uint32_t op, const float *x1, const float *x2, int from, int to) {
                if (op == 4)
                    for (int i = from; i < to; ++i)
                        result[i] = std::clamp(x1[i] * x2[i], 0.0f, 1.0f);
                else
                    for (int i = from; i < to; ++i)
                        result[i] = std::clamp(op == 2 ? x1[i] : x2[i], 0.0f, 1.0f);
            };
            apply(sc.colorOp, src[sc.source[0]], src[sc.source[1]], 0, 3);
            if (sc.alphaOp != 1)
                apply(sc.alphaOp, src[sc.source[2]], src[sc.source[3]], 3, 4);
            else
                result[3] = src[1][3];
            for (int i = 0; i < 4; ++i)
                src[1][i] = result[i];
        }
        finish(x, y, z, eyeW, attr, Color{src[1][0], src[1][1], src[1][2], src[1][3]}, zAt, zNew);
        return;
    }
    const Color diffuse{attr[0], attr[1], attr[2], attr[3]};
    const Color specular{attr[4], attr[5], attr[6], attr[7]};
    Color current = diffuse;
    bool keyed = false;
    for (int st = 0; st < stages; ++st) {
        const StageConfig &sc = cf.stage[st];
        bool k = false;
        const Color tex = unpack(sample(r, st, attr[8 + 2 * st], attr[9 + 2 * st], cf.colorKey ? &k : nullptr));
        if (bound[st] && k)
            keyed = true;
        auto arg = [&](uint32_t a, float *v) {
            const Color *c;
            switch (a & 0xf) {
            case 0:
                c = &diffuse;
                break;
            case 2:
                c = &tex;
                break;
            case 3:
                c = &cf.factor;
                break;
            case 4:
                c = &specular;
                break;
            default:
                c = &current;
                break;
            }
            v[0] = c->r, v[1] = c->g, v[2] = c->b, v[3] = c->a;
            if (a & 0x10)
                for (int i = 0; i < 4; ++i)
                    v[i] = 1 - v[i];
            if (a & 0x20)
                v[0] = v[1] = v[2] = v[3];
        };
        // One operation on three channels (colour) or one (alpha).
        auto apply = [&](uint32_t op, uint32_t arg1, uint32_t arg2, float *out, int from, int to) {
            float x1[4];
            float x2[4];
            arg(arg1, x1);
            arg(arg2, x2);
            auto each = [&](auto f) {
                for (int i = from; i < to; ++i)
                    out[i] = std::clamp(f(i), 0.0f, 1.0f);
            };
            switch (op) {
            case 2:
                each([&](int i) { return x1[i]; });
                break;
            case 3:
                each([&](int i) { return x2[i]; });
                break;
            case 5:
                each([&](int i) { return 2 * x1[i] * x2[i]; });
                break;
            case 6:
                each([&](int i) { return 4 * x1[i] * x2[i]; });
                break;
            case 7:
                each([&](int i) { return x1[i] + x2[i]; });
                break;
            case 8:
                each([&](int i) { return x1[i] + x2[i] - 0.5f; });
                break;
            case 9:
                each([&](int i) { return 2 * (x1[i] + x2[i] - 0.5f); });
                break;
            case 10:
                each([&](int i) { return x1[i] - x2[i]; });
                break;
            case 11:
                each([&](int i) { return x1[i] + x2[i] - x1[i] * x2[i]; });
                break;
            case 12:
                each([&](int i) { return x1[i] * diffuse.a + x2[i] * (1 - diffuse.a); });
                break;
            case 13:
                each([&](int i) { return x1[i] * tex.a + x2[i] * (1 - tex.a); });
                break;
            case 14:
                each([&](int i) { return x1[i] * cf.factor.a + x2[i] * (1 - cf.factor.a); });
                break;
            case 15:
                each([&](int i) { return x1[i] + x2[i] * (1 - tex.a); });
                break;
            case 16:
                each([&](int i) { return x1[i] * current.a + x2[i] * (1 - current.a); });
                break;
            case 18:
                each([&](int i) { return x1[i] + x1[3] * x2[i]; });
                break; // MODULATEALPHA_ADDCOLOR
            case 19:
                each([&](int i) { return i == 3 ? x1[3] * x2[3] : x1[i] + x2[i]; });
                break;
            case 20:
                each([&](int i) { return x1[i] + (1 - x1[3]) * x2[i]; });
                break; // MODULATEINVALPHA_ADDCOLOR
            default:
                each([&](int i) { return x1[i] * x2[i]; });
                break; // MODULATE and the rest
            }
        };
        float result[4] = {current.r, current.g, current.b, current.a};
        apply(sc.colorOp, sc.colorArg1, sc.colorArg2, result, 0, 3);
        if (sc.alphaOp != 1)
            apply(sc.alphaOp, sc.alphaArg1, sc.alphaArg2, result, 3, 4);
        current = {result[0], result[1], result[2], result[3]};
    }
    if (cf.colorKey && keyed)
        return;
    finish(x, y, z, eyeW, attr, current, zAt, zNew);
}

// The rest of a pixel's way, from its colour after the texture stages:
// specular, alpha test, fog, blending, the write.
RELUVA_PIXEL_STEP void Draw::finish(int x, int y, float z, float eyeW, const float *attr, Color current, uint8_t *zAt,
                                    uint32_t zNew) const {
    const PixelConfig &cf = cfg_;
    const Color specular{attr[4], attr[5], attr[6], attr[7]};
    if (cf.specular)
        current = {std::min(1.0f, current.r + specular.r), std::min(1.0f, current.g + specular.g),
                   std::min(1.0f, current.b + specular.b), current.a};
    // Alpha test, against the reference in 0..255.
    if (cf.alphaTest && !compare(cf.alphaFunc, float(floorInt(current.a * 255 + 0.5f)), cf.alphaRef))
        return;
    // Fog: by depth (table fog) or from the vertices.
    if (cf.fog) {
        float f = specular.a;
        if (cf.fogTableMode) {
            const float d = cf.fogByZ ? z : eyeW;
            if (cf.fogTableMode == 3)
                f = cf.fogEnd != cf.fogStart ? (cf.fogEnd - d) / (cf.fogEnd - cf.fogStart) : 1;
            else if (cf.fogTableMode == 1)
                f = std::exp(-cf.fogDensity * d);
            else
                f = std::exp(-(cf.fogDensity * d) * (cf.fogDensity * d));
        }
        f = std::clamp(f, 0.0f, 1.0f);
        const Color &fog = cf.fogColor;
        current = {current.r * f + fog.r * (1 - f), current.g * f + fog.g * (1 - f), current.b * f + fog.b * (1 - f),
                   current.a};
    }
    // Blending with what's there, and the write in the target's format.
    const uint32_t bytes = cf.targetBytes;
    uint8_t *at = cf.targetAt + uint32_t(y * cf.targetPitch) + uint32_t(x) * bytes;
    auto readTarget = [&]() -> Color {
        uint32_t raw = 0;
        std::memcpy(&raw, at, bytes);
        const Fractions &f = kFractions;
        if (cf.target == PixelConfig::Target::Rgb565)
            return {f.of31[raw >> 11 & 31], f.of63[raw >> 5 & 63], f.of31[raw & 31], 1};
        if (cf.target == PixelConfig::Target::Rgb888)
            return {f.of255[raw >> 16 & 0xff], f.of255[raw >> 8 & 0xff], f.of255[raw & 0xff], 1};
        return unpack(target->d.format.toArgb(raw, target->paletteArgb()));
    };
    float out[4] = {current.r, current.g, current.b, current.a};
    if (cf.blend) {
        const Color dst = readTarget();
        const float sv[4] = {current.r, current.g, current.b, current.a};
        const float dv[4] = {dst.r, dst.g, dst.b, dst.a};
        if (cf.srcBlend == 5 && cf.destBlend == 6) { // SRCALPHA, INVSRCALPHA: the usual
            const float a = current.a;
            const float ia = 1 - current.a;
            for (int i = 0; i < 4; ++i)
                out[i] = sv[i] * a + dv[i] * ia;
        } else {
            for (int i = 0; i < 4; ++i)
                out[i] = sv[i] * blendFactor(cf.srcBlend, current, dst, i) +
                         dv[i] * blendFactor(cf.destBlend, current, dst, i);
        }
    }
    auto channel = [](float v, float max) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * max + 0.5f); };
    if (cf.target == PixelConfig::Target::Rgb565) {
        const uint16_t v = uint16_t(channel(out[0], 31) << 11 | channel(out[1], 63) << 5 | channel(out[2], 31));
        std::memcpy(at, &v, 2);
    } else if (cf.target == PixelConfig::Target::Rgb888) {
        const uint32_t v = channel(out[0], 255) << 16 | channel(out[1], 255) << 8 | channel(out[2], 255);
        std::memcpy(at, &v, 4);
    } else {
        const uint32_t v = target->d.format.fromArgb(pack(out[0], out[1], out[2], out[3]));
        std::memcpy(at, &v, bytes);
    }
    if (zAt && cf.zWrite) {
        if (cf.zBytes == 2)
            std::memcpy(zAt, &zNew, 2);
        else
            std::memcpy(zAt, &zNew, cf.zBytes);
    }
}

// Clear(count, D3DRECT*, flags, colour, z): the rectangles (or the
// viewport) of the target (flag 1) and the Z buffer (flag 2), within the
// viewport.
void Device::clear(uint32_t count, uint32_t rects, uint32_t flags, uint32_t color, float z) {
    Cpu &c = s->cpu;
    const Rect vp{viewport.x, viewport.y, viewport.x + viewport.width, viewport.y + viewport.height};
    std::vector<Rect> areas;
    if (count && rects && c.valid(rects, 16 * count))
        for (uint32_t i = 0; i < count; ++i) {
            Rect r = Rect::read(c, rects + 16 * i);
            r.left = std::max(r.left, vp.left), r.top = std::max(r.top, vp.top);
            r.right = std::min(r.right, vp.right), r.bottom = std::min(r.bottom, vp.bottom);
            areas.push_back(r);
        }
    else
        areas.push_back(vp);
    // Filled in turn with the draws (DrawQueue), by rows: where the surfaces
    // are now, as the draws queued are drawn where they were.
    struct Fill {
        uint8_t *at;
        int width, height, pitch;
        uint32_t bytes, value;
    };
    std::vector<Fill> fills;
    uint8_t *mem = c.memory().data();
    if ((flags & 1) && target) {
        fills.push_back({mem + target->d.memory, target->d.width, target->d.height, target->d.pitch,
                         target->d.format.bytes(), target->d.format.fromArgb(color)});
        target->touched();
    }
    if ((flags & 2) && depth) {
        const double max = double(depth->d.format.g ? depth->d.format.g : 0xffff);
        fills.push_back({mem + depth->d.memory, depth->d.width, depth->d.height, depth->d.pitch,
                         depth->d.format.bytes(), uint32_t(std::floor(double(std::clamp(z, 0.0f, 1.0f)) * max + 0.5))});
        depth->touched();
    }
    if (fills.empty() || areas.empty())
        return;
    auto fill = [fills, areas](int band, int bands) {
        for (const Fill &f : fills)
            for (Rect r : areas) {
                r.left = std::max(r.left, 0), r.top = std::max(r.top, 0);
                r.right = std::min(r.right, f.width), r.bottom = std::min(r.bottom, f.height);
                for (int y = r.top; y < r.bottom; ++y) {
                    if (bands > 1 && (y >> 3) % bands != band) // another thread's rows
                        continue;
                    uint8_t *row = f.at + uint32_t(y * f.pitch) + uint32_t(r.left) * f.bytes;
                    if (f.bytes == 2) {
                        const uint16_t v = uint16_t(f.value);
                        for (int x = r.left; x < r.right; ++x, row += 2)
                            std::memcpy(row, &v, 2);
                    } else if (f.bytes == 4) {
                        for (int x = r.left; x < r.right; ++x, row += 4)
                            std::memcpy(row, &f.value, 4);
                    } else {
                        for (int x = r.left; x < r.right; ++x, row += f.bytes)
                            for (uint32_t b = 0; b < f.bytes; ++b)
                                row[b] = uint8_t(f.value >> (8 * b));
                    }
                }
            }
    };
    double area = 0;
    for (const Rect &r : areas)
        area += double(std::max(0, r.right - r.left)) * double(std::max(0, r.bottom - r.top));
    const DirectX::DrawThreads mode = s->owner.drawThreads;
    if (mode == DirectX::DrawThreads::One) {
        fill(0, 1);
        return;
    }
    if (!queue_)
        queue_ = std::make_shared<DrawQueue>(mode);
    queue_->push({Draw{}, {}, area, fill});
}

} // namespace reluva::x86::dx
