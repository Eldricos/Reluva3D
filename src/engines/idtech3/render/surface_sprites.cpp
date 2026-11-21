// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How sprites are spread, sized, swayed and faded follows the GPL Jedi
// Outcast renderer (tr_surfacesprites.cpp), read as a description and
// checked against jk2mp.exe's; docs/engines/idtech3/formats/shader.md says
// what's the same and what isn't. Their random draws come from the
// original's own table, walked as it walks it, so each stands where the
// original's does.

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

// The renderer's table of random numbers in 0..1, which every sprite's
// place, size and fade come from (jk2mp.exe, 0x4ead20).
constexpr std::array<float, 256> kRandomChart{{
    0.6554f, 0.6909f, 0.4806f, 0.6218f, 0.5717f, 0.3896f, 0.0677f, 0.7356f, 0.8333f, 0.1105f, 0.4445f, 0.8161f, 0.4689f,
    0.0433f, 0.7152f, 0.0336f, 0.0186f, 0.914f,  0.1626f, 0.6553f, 0.834f,  0.7094f, 0.202f,  0.8087f, 0.9119f, 0.8009f,
    0.1339f, 0.8492f, 0.9173f, 0.5003f, 0.6012f, 0.6117f, 0.5525f, 0.5787f, 0.1586f, 0.3293f, 0.9273f, 0.7791f, 0.8589f,
    0.4985f, 0.0883f, 0.8545f, 0.2634f, 0.4727f, 0.3624f, 0.1631f, 0.7825f, 0.0662f, 0.6704f, 0.351f,  0.7525f, 0.9486f,
    0.4685f, 0.1535f, 0.1545f, 0.1121f, 0.4724f, 0.8483f, 0.3833f, 0.1917f, 0.8207f, 0.3885f, 0.9702f, 0.92f,   0.8348f,
    0.7501f, 0.6675f, 0.4994f, 0.0301f, 0.5225f, 0.8011f, 0.1696f, 0.5351f, 0.2752f, 0.2962f, 0.755f,  0.5762f, 0.7303f,
    0.2835f, 0.4717f, 0.1818f, 0.2739f, 0.6914f, 0.7748f, 0.764f,  0.8355f, 0.7314f, 0.5288f, 0.734f,  0.6692f, 0.6813f,
    0.281f,  0.8057f, 0.0648f, 0.8749f, 0.9199f, 0.1462f, 0.5237f, 0.3014f, 0.4994f, 0.0278f, 0.4268f, 0.7238f, 0.5107f,
    0.1378f, 0.7303f, 0.72f,   0.3819f, 0.2034f, 0.7157f, 0.5552f, 0.4887f, 0.0871f, 0.3293f, 0.2892f, 0.4545f, 0.0088f,
    0.1404f, 0.0275f, 0.0238f, 0.0515f, 0.4494f, 0.7206f, 0.2893f, 0.606f,  0.5785f, 0.4182f, 0.5528f, 0.9118f, 0.8742f,
    0.3859f, 0.603f,  0.3495f, 0.455f,  0.9875f, 0.69f,   0.6416f, 0.2337f, 0.7431f, 0.9788f, 0.6181f, 0.2464f, 0.4661f,
    0.7621f, 0.702f,  0.8203f, 0.8869f, 0.2145f, 0.7724f, 0.6093f, 0.6692f, 0.9686f, 0.5609f, 0.031f,  0.2248f, 0.295f,
    0.2365f, 0.1347f, 0.2342f, 0.1668f, 0.3378f, 0.433f,  0.2775f, 0.9901f, 0.7053f, 0.7266f, 0.484f,  0.282f,  0.5733f,
    0.4555f, 0.6049f, 0.077f,  0.476f,  0.606f,  0.4159f, 0.3427f, 0.1234f, 0.7062f, 0.8569f, 0.1878f, 0.9057f, 0.9399f,
    0.8139f, 0.1407f, 0.1794f, 0.9123f, 0.9493f, 0.2827f, 0.9934f, 0.0952f, 0.4879f, 0.516f,  0.4118f, 0.4873f, 0.3642f,
    0.747f,  0.0866f, 0.5172f, 0.6365f, 0.2676f, 0.2407f, 0.7223f, 0.5761f, 0.1143f, 0.7137f, 0.2342f, 0.3353f, 0.688f,
    0.2296f, 0.6023f, 0.6027f, 0.4138f, 0.5408f, 0.9859f, 0.1503f, 0.7238f, 0.6054f, 0.2477f, 0.6804f, 0.1432f, 0.454f,
    0.9776f, 0.8762f, 0.7607f, 0.9025f, 0.9807f, 0.0652f, 0.8661f, 0.7663f, 0.2586f, 0.3994f, 0.0335f, 0.7328f, 0.0166f,
    0.9589f, 0.4348f, 0.5493f, 0.7269f, 0.6867f, 0.6614f, 0.68f,   0.7804f, 0.5591f, 0.8381f, 0.091f,  0.7573f, 0.8985f,
    0.3083f, 0.3188f, 0.8481f, 0.2356f, 0.6736f, 0.477f,  0.456f,  0.6266f, 0.4677f,
}};

// A float as the original's renderer makes a byte of it: cut to a whole
// number (_ftol, 0x4d4f00), its lowest 8 bits kept.
uint8_t toByte(float f) {
    return uint8_t(int64_t(f));
}

// Quake III's fast 1 / sqrt, one refinement (Q_rsqrt, jk2mp.exe 0x4339e0):
// its small error decides how many sprites a triangle gets.
float fastInverseRoot(float number) {
    uint32_t i = 0;
    float y = number;
    std::memcpy(&i, &y, 4);
    i = 0x5f3759dfU - (i >> 1);
    std::memcpy(&y, &i, 4);
    return y * (1.5f - number * 0.5f * y * y);
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
    _shownVariant.clear();
}

void SurfaceSpriteSet::showVariant(int surface, int variant) {
    if (surface < 0)
        return;
    if (size_t(surface) >= _shownVariant.size())
        _shownVariant.resize(size_t(surface) + 1, -1);
    _shownVariant[size_t(surface)] = variant;
}

void SurfaceSpriteSet::add(int surface, int material, int fog, float sort, bool additive, const SurfaceSprites &parms,
                           const std::vector<gfx::Vertex> &vertices, const uint32_t *indices, size_t indexCount,
                           int variant) {
    if (parms.type == Type::kNone)
        return;
    Group g;
    g.surface = surface;
    g.variant = variant;
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
        t.inverseRoot = fastInverseRoot(area);
        t.step = parms.density * t.inverseRoot;
        t.firstSprite = uint32_t(_sprites.size());
        // A grid of cells over the triangle's two weights, a sprite each
        // somewhere in its cell, its draws walking the random table from a
        // place and by a step (odd, at least 3) its corners give, as the
        // original's (jk2mp.exe 0x496615); one falling outside the
        // triangle is dropped (an effect's is tried again each life).
        const auto &p = t.pos;
        auto index = toByte(p[0][0] + p[0][1] + p[1][0] + p[1][1] + p[2][0] + p[2][1]);
        t.firstIndex = index;
        t.interval = uint8_t(toByte(p[0][0] + p[1][1] + p[2][2]) | 3);
        auto next = [&] {
            const float r = kRandomChart[index];
            index = uint8_t(index + t.interval);
            return r;
        };
        uint8_t turn = 0;
        // Effects' cells are laid each frame (their spacing follows the
        // weather).
        for (float a = 0; a < 1 && !effect; a += t.step)
            for (float b = 0; b < 1 - a; b += t.step) {
                Sprite s;
                s.cell = {a, b};
                float fa = 0;
                float fb = 0;
                if (parms.type == Type::kVertical) {
                    // Both draws made before either is checked.
                    fa = a + next() * t.step;
                    fb = b + next() * t.step;
                    turn = uint8_t((turn + 1) & 3);
                    if (fa > 1 || fb > 1 - fa)
                        continue;
                    s.fadeStart = fadeRange + (1 - fadeRange) * next();
                } else {
                    fa = a + next() * t.step;
                    if (fa > 1)
                        continue;
                    fb = b + next() * t.step;
                    if (fb > 1 - fa)
                        continue;
                    s.fadeStart = fadeRange + (1 - fadeRange) * next();
                    next(); // the oriented kind steps once more
                }
                s.turn = turn;
                // The size and the rest read on from there, without moving
                // the walk.
                s.sizeRandom = kRandomChart[index];
                s.flipped = kRandomChart[uint8_t(index + 1)] > 0.5f;
                s.skew = {parms.vertSkew * 2 * kRandomChart[uint8_t(index + 2)] - parms.vertSkew,
                          parms.vertSkew * 2 * kRandomChart[uint8_t(index + 3)] - parms.vertSkew};
                s.weights = {fa, fb, 1 - fa - fb};
                _sprites.push_back(s);
            }
        t.spriteCount = uint32_t(_sprites.size()) - t.firstSprite;
        if (!t.spriteCount && !effect)
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

SurfaceSpriteWeather surfaceSpriteSettings(CvarStore &cvars) {
    auto setting = [&](const char *name, const char *byDefault) {
        cvars.create(name, byDefault, 0);
        return cvars.value(name);
    };
    SurfaceSpriteWeather w;
    w.windSpeed = setting("r_windSpeed", "0");
    w.windAngle = setting("r_windAngle", "0");
    w.windGust = setting("r_windGust", "0");
    w.windDampFactor = setting("r_windDampFactor", "0.1");
    w.windPointForce = setting("r_windPointForce", "0");
    w.windPointX = setting("r_windPointX", "0");
    w.windPointY = setting("r_windPointY", "0");
    w.surfaceWeather = setting("r_surfaceWeather", "0");
    // Kept between runs, as both programs register it (0x46e7ef, 0x47f062).
    cvars.create("r_lodCurveError", "250", CvarStore::kArchive);
    w.lodCurveError = cvars.value("r_lodCurveError");
    return w;
}

void SurfaceSpriteSet::frame(int32_t timeMs, float fovX, const SurfaceSpriteWeather &w) {
    // As the original's R_SurfaceSpriteFrameUpdate: once for each time.
    if (timeMs == _lastTime)
        return;
    if (timeMs < _lastTime) {
        _windGust = 5;
        _windSpeed = w.windSpeed;
        _nextGust = 0;
        _gustLeft = 0;
        _grass = {};
    }
    constexpr float kPi = 3.14159265358979f;
    // The first ordinary field of view drawn is the standard; a narrower
    // one sees as far as it's magnified (at most as for 135 degrees).
    if (!_standardFovSet) {
        _standardScale = std::tan((fovX > 50 && fovX < 135 ? fovX : 90) * 0.5f * kPi / 180);
        _standardFovSet = fovX > 50 && fovX < 135;
        _rangeScale = 1;
    } else {
        _rangeScale = _standardScale / std::tan(std::min(fovX, 135.0f) * 0.5f * kPi / 180);
    }
    // No rain or snow system: the weather sprites' amount and the wind
    // come from the settings.
    _weatherAmount = w.surfaceWeather;
    float target = w.windSpeed;
    _windGust = w.windGust;
    auto random = [&](float lo, float hi) { return flrand ? flrand(lo, hi) : _ownRandom.flrand(lo, hi); };
    if (target > 0 && _windGust != 0) {
        if (_gustLeft > 0) {
            target *= 1 + _gustLeft;
            _gustLeft -= float(timeMs - _lastTime) / 2500.0f;
            if (_gustLeft <= 0)
                _nextGust = timeMs + int32_t(_windGust * 1000 * random(1, 4));
        } else if (timeMs >= _nextGust) {
            _gustLeft = random(0.75f, 1.5f);
        }
    }
    // Grass leans towards the wind, more the faster it blows (from straight
    // down at none to 45 degrees off it); effects drift along it.
    const float yaw = w.windAngle * kPi / 180;
    const float pitch = std::min(-90 + target, -45.0f) * kPi / 180;
    const Vec3 targetGrass{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch) - 1};
    // Damped towards the targets, as much as 50 ms of damping would.
    const float ratio = std::pow(1 - w.windDampFactor, float(timeMs - _lastTime) / 50.0f);
    _windSpeed = target - ratio * (target - _windSpeed);
    const Vec3 targetBlow{std::cos(yaw) * _windSpeed, std::sin(yaw) * _windSpeed, 0};
    for (size_t k = 0; k < 3; ++k) {
        _blow[k] = targetBlow[k] - ratio * (targetBlow[k] - _blow[k]);
        _grass[k] = targetGrass[k] - ratio * (targetGrass[k] - _grass[k]);
    }
    _lastTime = timeMs;
    _windPointForce = w.windPointForce - ratio * (w.windPointForce - _windPointForce);
    _windPointActive = _windPointForce >= 0.01f;
    _windPoint = {w.windPointX, w.windPointY};
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
    constexpr float kWindPointRadius = 750;

    for (const Group &g : _groups) {
        const SurfaceSprites &p = g.parms;
        // Weather shows only as much as the settings (or rain) say.
        const bool weather = p.type == Type::kWeather;
        if (g.sort < sortMin || g.sort > sortMax || (weather && _weatherAmount < 0.01f) || !visible(g.surface))
            continue;
        if (g.variant >= 0 &&
            (size_t(g.surface) >= _shownVariant.size() || _shownVariant[size_t(g.surface)] != g.variant))
            continue;
        // Nothing past fadeMax shows (farther with a narrower view): a
        // group wholly beyond it is skipped.
        const float cut = p.fadeMax * _rangeScale;
        const float fade = p.fadeDist * _rangeScale;
        float outside = 0;
        for (size_t k = 0; k < 3; ++k) {
            const float d = std::max({g.mins[k] - eye[k], 0.0f, eye[k] - g.maxs[k]});
            outside += d * d;
        }
        const float cut2 = cut * cut;
        const float fade2 = fade * fade;
        if (outside >= cut2)
            continue;
        const float invFade = 1 / (cut2 - fade2);
        const float fadeRange = std::min(kFadeRange / (cut - fade), 1.0f);
        const bool effect = p.type == Type::kEffect || weather;
        const float density = weather ? p.density / _weatherAmount : p.density;
        // An effect fading out from a size of its own fades in too.
        const bool fadeInOut = p.fxAlphaEnd < 0.05f && p.height >= 0.1f && p.width >= 0.1f;
        const bool windPoint = _windPointActive && p.wind > 0 && p.type == Type::kVertical;
        const bool windy = p.wind > 0 && _windSpeed > 0.001f;
        const auto first = uint32_t(indices.size());
        for (uint32_t ti = g.firstTriangle; ti < g.firstTriangle + g.triangleCount; ++ti) {
            const Triangle &t = _triangles[ti];
            // 1 at fadeDist, 0 at fadeMax, by each corner's distance.
            std::array<float, 3> near{};
            for (size_t c = 0; c < 3; ++c)
                near[c] = 1 - (lengthSquared(eye, t.pos[c]) - fade2) * invFade;
            if (near[0] <= 0 && near[1] <= 0 && near[2] <= 0)
                continue;
            // The wind point's push at each corner: away from it, fading to
            // nothing 750 units out.
            std::array<std::array<float, 2>, 3> pushDir{};
            std::array<float, 3> pushForce{};
            if (windPoint)
                for (size_t c = 0; c < 3; ++c) {
                    const float dx = t.pos[c][0] - _windPoint[0];
                    const float dy = t.pos[c][1] - _windPoint[1];
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= kWindPointRadius * kWindPointRadius)
                        continue;
                    if (d2 < 1) {
                        pushForce[c] = _windPointForce * p.wind;
                        continue;
                    }
                    const float inv = fastInverseRoot(d2);
                    pushDir[c] = {dx * inv, dy * inv};
                    pushForce[c] = _windPointForce * p.wind * (1 - 1 / (inv * kWindPointRadius));
                }
            // A sprite's quad, given where in the triangle and its draws.
            auto draw = [&](const std::array<float, 3> &w, float fadeStart, float sizeRandom, bool flipped,
                            const std::array<float, 2> &skew, uint8_t turn, float life) {
                const float along = near[0] * w[0] + near[1] * w[1] + near[2] * w[2];
                float alpha = std::min(1 - (fadeStart - along) / fadeRange, 1.0f);
                if (alpha <= 0)
                    return;
                Vec3 at{};
                for (size_t c = 0; c < 3; ++c)
                    for (size_t k = 0; k < 3; ++k)
                        at[k] += t.pos[c][k] * w[c];
                float light = t.light[0] * w[0] + t.light[1] * w[1] + t.light[2] * w[2];
                float width = p.width * (1 + p.variance[0] * sizeRandom);
                float height = p.height * (1 + p.variance[1] * sizeRandom);
                if (effect) {
                    width += life * p.fxGrow[0] * width;
                    height += life * p.fxGrow[1] * height;
                    const float span = p.fxAlphaEnd - p.fxAlphaStart;
                    if (fadeInOut)
                        alpha *= p.fxAlphaStart + span * std::fabs(life - 0.5f) * 2;
                    else
                        alpha *= p.fxAlphaStart + span * life;
                    // Drifting with the wind over its life.
                    if (windy)
                        for (size_t k = 0; k < 3; ++k)
                            at[k] += life * p.wind * _blow[k];
                }
                // Added light: brighter rather than more opaque.
                if (g.additive) {
                    light = (128 + light * 0.5f) * alpha;
                    alpha = 1;
                }
                if (flipped)
                    width = -width;
                if (p.fadeScale != 0 && along < 1)
                    width *= 1 + p.fadeScale * (1 - along);
                const auto lightByte = uint8_t(std::clamp(light, 0.0f, 255.0f));
                const auto alphaByte = uint8_t(std::clamp(alpha * 255, 0.0f, 255.0f));

                std::array<Vec3, 4> corners{};
                if (p.type == Type::kVertical) {
                    const float down = p.facing != Facing::kNormal ? -height : height;
                    const float angle = (at[0] + at[1]) * 0.02f + timeMs * 0.0015f;
                    Vec3 top{at[0] + skew[0] * height, at[1] + skew[1] * height, at[2] + down};
                    float lean = 0.2f; // the top's far corner, forward, of the width
                    float force = 0;
                    std::array<float, 2> push{};
                    if (windPoint)
                        for (size_t c = 0; c < 3; ++c) {
                            force += pushForce[c] * w[c];
                            push[0] += pushDir[c][0] * w[c];
                            push[1] += pushDir[c][1] * w[c];
                        }
                    if (windPoint && force > 0) {
                        // Blown from the wind point: swaying more, bent
                        // away and pressed down.
                        force = std::min(force, 1.0f);
                        if (_windSpeed < 80) {
                            const float sway = height * p.windIdle * 0.1f * (1 + force);
                            top[0] += std::cos(angle) * sway;
                            top[1] += std::sin(angle) * sway;
                        }
                        if (_windSpeed > 0.001f)
                            for (size_t k = 0; k < 3; ++k)
                                top[k] += height * p.wind * _grass[k];
                        top[0] += height * push[0] * force;
                        top[1] += height * push[1] * force;
                        top[2] -= height * force * (0.75f + 0.15f * std::sin((timeMs + 500 * force) * 0.01f));
                        lean = 0.15f;
                    } else {
                        // Its top swaying in a little circle, each spot in
                        // its own phase; in the wind, leaning with it and
                        // bobbing.
                        if (p.windIdle > 0) {
                            const float sway = height * p.windIdle * 0.075f;
                            top[0] += std::cos(angle) * sway;
                            top[1] += std::sin(angle) * sway;
                        }
                        if (windy) {
                            for (size_t k = 0; k < 3; ++k)
                                top[k] += height * p.wind * _grass[k];
                            const float bob = height * p.wind * 0.075f * (_windSpeed < 40 ? _windSpeed / 100 : 0.4f);
                            top[2] += std::sin(angle * 2.5f) * bob;
                        }
                    }
                    Vec3 r{};
                    for (size_t k = 0; k < 3; ++k)
                        r[k] = sideways[turn][k] * width * 0.5f;
                    for (size_t k = 0; k < 3; ++k) {
                        corners[0][k] = at[k] + r[k];
                        corners[1][k] = top[k] + r[k];
                        corners[2][k] = top[k] - r[k] + forward[k] * width * lean;
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
            };
            if (!effect) {
                for (uint32_t si = t.firstSprite; si < t.firstSprite + t.spriteCount; ++si) {
                    const Sprite &s = _sprites[si];
                    draw(s.weights, s.fadeStart, s.sizeRandom, s.flipped, s.skew, s.turn, 0);
                }
                continue;
            }
            // Effects: the cells laid now (spaced by the density, which the
            // weather divides), each life somewhere else in its cell: its
            // time on from a draw, and the walk from there by the lives
            // (jk2mp.exe 0x497c51).
            const float step = density * t.inverseRoot;
            if (!(step > 0))
                continue;
            uint8_t index = t.firstIndex;
            for (float a = 0; a < 1; a += step)
                for (float b = 0; b < 1 - a; b += step) {
                    const uint8_t cell = index;
                    index = uint8_t(index + t.interval);
                    const float lives = float((double(timeMs) + 10000.0 * kRandomChart[cell]) / p.fxDuration);
                    const float life = lives - float(int64_t(lives));
                    auto at = toByte(float(cell) + lives);
                    const float fa = a + kRandomChart[at++] * step;
                    if (fa > 1)
                        continue;
                    const float fb = b + kRandomChart[at++] * step;
                    if (fb > 1 - fa)
                        continue;
                    const float fadeStart = fadeRange + (1 - fadeRange) * kRandomChart[at];
                    draw({fa, fb, 1 - fa - fb}, fadeStart, kRandomChart[index], kRandomChart[uint8_t(index + 1)] > 0.5f,
                         {}, 0, life);
                }
        }
        if (indices.size() > first)
            parts.push_back({g.material, g.fog, first, uint32_t(indices.size()) - first});
    }
}

} // namespace reluva::idtech3
