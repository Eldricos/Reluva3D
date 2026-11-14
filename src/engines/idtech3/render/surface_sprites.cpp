// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How sprites are spread, sized, swayed and faded follows the GPL Jedi
// Outcast renderer (tr_surfacesprites.cpp), read as a description;
// docs/engines/idtech3/formats/shader.md says what's the same and what
// isn't. The random draws are Reluva's own, so each sprite stands where
// the original's would stand only by chance.

#include "engines/idtech3/render/surface_sprites.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

using Vec3 = std::array<float, 3>;
using Type = SurfaceSprites::Type;
using Facing = SurfaceSprites::Facing;

// How far, in units, sprites fade from wholly there to gone; each starts
// fading at its own point within it.
constexpr float kFadeRange = 250;

uint32_t mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// The next of a sequence of draws in 0..1.
float unit(uint32_t &state) {
    state = mix(state + 0x9e3779b9U);
    return float(state >> 8) * (1.0f / 16777216.0f);
}

uint32_t seedOf(const std::array<Vec3, 3> &pos) {
    uint32_t h = 0x2545f491U;
    for (const Vec3 &p : pos)
        for (float f : p) {
            uint32_t bits = 0;
            std::memcpy(&bits, &f, 4);
            h = mix(h ^ bits);
        }
    return h;
}

// Whether a triangle's corners all face the way its sprites need: up (or,
// hanging down, down) for vertical ones; within about 60 degrees of up
// for the others, or nearly straight up when they lie flat.
bool facesRightWay(const SurfaceSprites &p, const std::array<const gfx::Vertex *, 3> &v) {
    for (const gfx::Vertex *c : v) {
        const float nz = c->normal[2];
        if (p.type == Type::kVertical) {
            if (p.facing != Facing::kNormal ? nz > -0.5f : nz < 0.5f)
                return false;
        } else if (nz < (p.facing != Facing::kNormal ? 0.99f : 0.5f)) {
            return false;
        }
    }
    return true;
}

float lengthSquared(const Vec3 &a, const Vec3 &b) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return x * x + y * y + z * z;
}

void quad(const std::array<Vec3, 4> &p, uint8_t light, uint8_t alpha, std::vector<gfx::Vertex> &vertices,
          std::vector<uint32_t> &indices) {
    // The whole texture, its top at the sprite's top.
    static const float kSt[4][2] = {{1, 1}, {1, 0}, {0, 0}, {0, 1}};
    const auto base = uint32_t(vertices.size());
    for (size_t i = 0; i < 4; ++i) {
        gfx::Vertex v;
        for (size_t k = 0; k < 3; ++k)
            v.pos[k] = p[i][k];
        v.normal[2] = 1;
        v.uv[0] = kSt[i][0];
        v.uv[1] = kSt[i][1];
        v.color[0] = v.color[1] = v.color[2] = light;
        v.color[3] = alpha;
        vertices.push_back(v);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
        indices.push_back(base + i);
}

} // namespace

void SurfaceSpriteSet::clear() {
    _groups.clear();
    _triangles.clear();
    _sprites.clear();
}

void SurfaceSpriteSet::add(int surface, int material, int fog, float sort, bool additive, const SurfaceSprites &parms,
                           const std::vector<gfx::Vertex> &vertices, const uint32_t *indices, size_t indexCount) {
    if (parms.type == Type::kNone)
        return;
    Group g;
    g.surface = surface;
    g.material = material;
    g.fog = fog;
    g.sort = sort;
    g.additive = additive;
    g.parms = parms;
    g.firstTriangle = uint32_t(_triangles.size());
    g.mins = {1e30f, 1e30f, 1e30f};
    g.maxs = {-1e30f, -1e30f, -1e30f};
    const bool effect = parms.type == Type::kEffect || parms.type == Type::kWeather;
    const float fadeRange = std::min(kFadeRange / (parms.fadeMax - parms.fadeDist), 1.0f);
    for (size_t i = 0; i + 2 < indexCount; i += 3) {
        const std::array<const gfx::Vertex *, 3> v{&vertices[indices[i]], &vertices[indices[i + 1]],
                                                   &vertices[indices[i + 2]]};
        if (!facesRightWay(parms, v))
            continue;
        Triangle t;
        for (size_t c = 0; c < 3; ++c) {
            for (size_t k = 0; k < 3; ++k)
                t.pos[c][k] = v[c]->pos[k];
            t.light[c] = v[c]->color[2];
        }
        // Spaced by the triangle's area as seen from above.
        const float area = std::fabs((t.pos[2][0] - t.pos[0][0]) * (t.pos[1][1] - t.pos[0][1]) -
                                     (t.pos[2][1] - t.pos[0][1]) * (t.pos[1][0] - t.pos[0][0]));
        if (area <= 1)
            continue;
        t.step = parms.density / std::sqrt(area);
        t.firstSprite = uint32_t(_sprites.size());
        // A grid of cells over the triangle's two weights, a sprite each
        // somewhere in its cell; one falling outside the triangle is
        // dropped (an effect's is tried again each life).
        uint32_t state = seedOf(t.pos);
        uint8_t turn = 0;
        for (float a = 0; a < 1; a += t.step)
            for (float b = 0; b < 1 - a; b += t.step) {
                Sprite s;
                s.cell = {a, b};
                turn = uint8_t((turn + 1) & 3);
                s.turn = turn;
                const float fa = a + unit(state) * t.step;
                const float fb = b + unit(state) * t.step;
                s.fadeStart = fadeRange + (1 - fadeRange) * unit(state);
                s.sizeRandom = unit(state);
                s.flipped = unit(state) > 0.5f;
                const float skewX = unit(state);
                const float skewY = unit(state);
                s.skew = {parms.vertSkew * (2 * skewX - 1), parms.vertSkew * (2 * skewY - 1)};
                s.lifeOffset = 10000 * unit(state);
                s.seed = mix(state);
                if (!effect) {
                    if (fa > 1 || fb > 1 - fa)
                        continue;
                    s.weights = {fa, fb, 1 - fa - fb};
                }
                _sprites.push_back(s);
            }
        t.spriteCount = uint32_t(_sprites.size()) - t.firstSprite;
        if (!t.spriteCount)
            continue;
        for (const Vec3 &p : t.pos)
            for (size_t k = 0; k < 3; ++k) {
                g.mins[k] = std::min(g.mins[k], p[k]);
                g.maxs[k] = std::max(g.maxs[k], p[k]);
            }
        _triangles.push_back(t);
    }
    g.triangleCount = uint32_t(_triangles.size()) - g.firstTriangle;
    if (g.triangleCount)
        _groups.push_back(g);
}

std::vector<float> SurfaceSpriteSet::sorts() const {
    std::vector<float> out;
    for (const Group &g : _groups)
        out.push_back(g.sort);
    return out;
}

void SurfaceSpriteSet::build(const Vec3 &eye, const Vec3 &left, const Vec3 &up, float timeMs, float sortMin,
                             float sortMax, const std::function<bool(int surface)> &visible,
                             std::vector<gfx::Vertex> &vertices, std::vector<uint32_t> &indices,
                             std::vector<Part> &parts) const {
    // Vertical sprites turn about their height to face the camera, each
    // a little off it in one of four ways, so a clump doesn't look flat.
    const Vec3 forward{left[1], -left[0], 0};
    static const float kTurns[4][2] = {{0.985f, 0.174f}, {0.866f, -0.5f}, {0.866f, 0.5f}, {0.985f, -0.174f}};
    std::array<Vec3, 4> sideways{};
    for (size_t i = 0; i < 4; ++i)
        for (size_t k = 0; k < 3; ++k)
            sideways[i][k] = left[k] * kTurns[i][0] + forward[k] * kTurns[i][1];

    for (const Group &g : _groups) {
        const SurfaceSprites &p = g.parms;
        // Weather shows only while it rains or snows, which the game's
        // code starts.
        if (g.sort < sortMin || g.sort > sortMax || p.type == Type::kWeather || !visible(g.surface))
            continue;
        // Nothing past fadeMax shows: a group wholly beyond it is skipped.
        float outside = 0;
        for (size_t k = 0; k < 3; ++k) {
            const float d = std::max({g.mins[k] - eye[k], 0.0f, eye[k] - g.maxs[k]});
            outside += d * d;
        }
        const float cut2 = p.fadeMax * p.fadeMax;
        const float fade2 = p.fadeDist * p.fadeDist;
        if (outside >= cut2)
            continue;
        const float invFade = 1 / (cut2 - fade2);
        const float fadeRange = std::min(kFadeRange / (p.fadeMax - p.fadeDist), 1.0f);
        const bool effect = p.type == Type::kEffect;
        // An effect fading out from a size of its own fades in too.
        const bool fadeInOut = p.fxAlphaEnd < 0.05f && p.height >= 0.1f && p.width >= 0.1f;
        const auto first = uint32_t(indices.size());
        for (uint32_t ti = g.firstTriangle; ti < g.firstTriangle + g.triangleCount; ++ti) {
            const Triangle &t = _triangles[ti];
            // 1 at fadeDist, 0 at fadeMax, by each corner's distance.
            std::array<float, 3> near{};
            for (size_t c = 0; c < 3; ++c)
                near[c] = 1 - (lengthSquared(eye, t.pos[c]) - fade2) * invFade;
            if (near[0] <= 0 && near[1] <= 0 && near[2] <= 0)
                continue;
            for (uint32_t si = t.firstSprite; si < t.firstSprite + t.spriteCount; ++si) {
                const Sprite &s = _sprites[si];
                std::array<float, 3> w = s.weights;
                float life = 0;
                if (effect) {
                    // Each life somewhere else in its cell.
                    const float lives = (timeMs + s.lifeOffset) / p.fxDuration;
                    life = lives - std::floor(lives);
                    uint32_t state = mix(s.seed ^ uint32_t(int64_t(std::floor(lives))));
                    const float fa = s.cell[0] + unit(state) * t.step;
                    const float fb = s.cell[1] + unit(state) * t.step;
                    if (fa > 1 || fb > 1 - fa)
                        continue;
                    w = {fa, fb, 1 - fa - fb};
                }
                const float along = near[0] * w[0] + near[1] * w[1] + near[2] * w[2];
                float alpha = std::min(1 - (s.fadeStart - along) / fadeRange, 1.0f);
                if (alpha <= 0)
                    continue;
                Vec3 at{};
                for (size_t c = 0; c < 3; ++c)
                    for (size_t k = 0; k < 3; ++k)
                        at[k] += t.pos[c][k] * w[c];
                float light = t.light[0] * w[0] + t.light[1] * w[1] + t.light[2] * w[2];
                float width = p.width * (1 + p.variance[0] * s.sizeRandom);
                float height = p.height * (1 + p.variance[1] * s.sizeRandom);
                if (effect) {
                    width += life * p.fxGrow[0] * width;
                    height += life * p.fxGrow[1] * height;
                    const float span = p.fxAlphaEnd - p.fxAlphaStart;
                    if (fadeInOut)
                        alpha *= p.fxAlphaStart + span * std::fabs(life - 0.5f) * 2;
                    else
                        alpha *= p.fxAlphaStart + span * life;
                }
                // Added light: brighter rather than more opaque.
                if (g.additive) {
                    light = (128 + light * 0.5f) * alpha;
                    alpha = 1;
                }
                if (s.flipped)
                    width = -width;
                if (p.fadeScale != 0 && along < 1)
                    width *= 1 + p.fadeScale * (1 - along);
                const auto lightByte = uint8_t(std::clamp(light, 0.0f, 255.0f));
                const auto alphaByte = uint8_t(std::clamp(alpha * 255, 0.0f, 255.0f));

                std::array<Vec3, 4> corners{};
                if (p.type == Type::kVertical) {
                    // Its top swaying in a little circle, each spot in its
                    // own phase.
                    Vec3 top{at[0] + s.skew[0] * height, at[1] + s.skew[1] * height,
                             at[2] + (p.facing != Facing::kNormal ? -height : height)};
                    if (p.windIdle > 0) {
                        const float angle = (at[0] + at[1]) * 0.02f + timeMs * 0.0015f;
                        const float sway = height * p.windIdle * 0.075f;
                        top[0] += std::cos(angle) * sway;
                        top[1] += std::sin(angle) * sway;
                    }
                    Vec3 r{};
                    for (size_t k = 0; k < 3; ++k)
                        r[k] = sideways[s.turn][k] * width * 0.5f;
                    for (size_t k = 0; k < 3; ++k) {
                        corners[0][k] = at[k] + r[k];
                        corners[1][k] = top[k] + r[k];
                        corners[2][k] = top[k] - r[k] + forward[k] * width * 0.2f;
                        corners[3][k] = at[k] - r[k];
                    }
                } else if (p.facing != Facing::kNormal) {
                    // Flat, a unit above the surface.
                    const float h = width * 0.5f;
                    corners = {Vec3{at[0] + h, at[1] - h, at[2] + 1}, Vec3{at[0] + h, at[1] + h, at[2] + 1},
                               Vec3{at[0] - h, at[1] + h, at[2] + 1}, Vec3{at[0] - h, at[1] - h, at[2] + 1}};
                } else {
                    for (size_t k = 0; k < 3; ++k) {
                        const float r = left[k] * width * 0.5f;
                        const float top = at[k] + up[k] * height;
                        corners[0][k] = at[k] + r;
                        corners[1][k] = top + r;
                        corners[2][k] = top - r;
                        corners[3][k] = at[k] - r;
                    }
                }
                quad(corners, lightByte, alphaByte, vertices, indices);
            }
        }
        if (indices.size() > first)
            parts.push_back({g.material, g.fog, first, uint32_t(indices.size()) - first});
    }
}

} // namespace reluva::idtech3
