// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a level's BSP polygons drawn on the GPU,
// textured and lit by its lightmaps, its movers (doors, lifts) from their
// own brushes and its sky seen from the sky zone, with a fly camera
// starting at the level's PlayerStart.

#include "engines/unreal/render/level_view.h"

#include "common/developer.h"

#include "engines/unreal/formats/actors.h"
#include "engines/unreal/formats/class_defaults.h"
#include "engines/unreal/render/lighting.h"
#include "engines/unreal/render/procedural.h"
#include "engines/unreal/vm/vm_level.h"
#include "engines/unreal/formats/properties.h"
#include "engines/unreal/formats/texture.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace reluva::unreal {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kSpeed = 400;    // units per second
constexpr float kEyeHeight = 27; // above a PlayerStart's location, roughly a player's eyes
// The frame's colours are multiplied by this before the display gamma:
// none, the lights' units being the original's (lighting.md).
constexpr float kExposure = 1;

enum Held {
    kForward,
    kBack,
    kLeft,
    kRight,
    kUp,
    kDown,
    kFast,
    kTurnLeft,
    kTurnRight,
    kLookUp,
    kLookDown,
    kFire,
    kHeldCount
};

// Unreal's axes are left-handed (X forward, Y right, Z up); the renderer's
// are right-handed, so Y is negated on the way in.
gfx::Vec3 toWorld(const Vec3f &v) {
    return {v[0], -v[1], v[2]};
}

// A sprite's colour, as Render.dll's DrawActorSprite has it: its
// ColorFilter, times its ScaleGlow (each part at most 1) unless that's 1.
std::array<float, 3> spriteTint(const std::array<float, 3> &filter, float glow) {
    std::array<float, 3> c = filter;
    if (glow != 1)
        for (float &v : c)
            v = std::min(v * glow, 1.0f);
    for (float &v : c)
        v *= kExposure;
    return c;
}

// The surface flags that change how a polygon is drawn, for grouping.
constexpr uint32_t kDrawFlags = polyflag::kMasked | polyflag::kTranslucent | polyflag::kModulated |
                                polyflag::kFakeBackdrop | polyflag::kUnlit | polyflag::kTwoSided | polyflag::kMirrored |
                                polyflag::kAutoUPan | polyflag::kAutoVPan | polyflag::kWavy | polyflag::kForceRender;
constexpr uint32_t kPanFlags = polyflag::kAutoUPan | polyflag::kAutoVPan | polyflag::kWavy;

// UE1's flag for a surface in a fog zone (PF_RenderFog), kept on the
// level's batches for the volume lights' pass.
constexpr uint32_t kRenderFog = 0x40000000;

bool blended(uint32_t flags) {
    return flags & (polyflag::kTranslucent | polyflag::kModulated);
}

// Column-major 4x4 matrices for placing movers (gfx3d.h's conventions).
gfx::Mat4 translation(const Vec3f &t) {
    gfx::Mat4 m = gfx::identity();
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}
gfx::Mat4 scaling(const Vec3f &s) {
    gfx::Mat4 m = gfx::identity();
    m[0] = s[0];
    m[5] = s[1];
    m[10] = s[2];
    return m;
}
gfx::Mat4 rotation(const Rotator &r) {
    const std::array<Vec3f, 3> axes = rotationAxes(r);
    gfx::Mat4 m = gfx::identity();
    for (size_t c = 0; c < 3; ++c)
        for (size_t k = 0; k < 3; ++k)
            m[c * 4 + k] = axes[c][k];
    return m;
}

// The light at a point facing `n` from every light, without shadows (for
// movers, which the level's lightmaps don't cover), at most kMaxLight.
Vec3f lightAt(const LevelLights &lights, const Vec3f &p, const Vec3f &n) {
    Vec3f total{0.1f, 0.1f, 0.1f}; // a little, so unlit sides aren't black
    for (const LightSource &l : lights.unique) {
        const Vec3f d{l.position[0] - p[0], l.position[1] - p[1], l.position[2] - p[2]};
        const float straight = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float dist = lightDistance(l, p);
        if (dist >= l.radius || straight <= 0)
            continue;
        float amount = lightFalloff(dist / l.radius);
        if (l.incidence)
            amount *= std::max(0.0f, (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / straight);
        for (size_t k = 0; k < 3; ++k)
            total[k] += l.color[k] * amount;
    }
    for (float &c : total)
        c = std::min(c, kMaxLight);
    return total;
}

} // namespace

LevelView::LevelView(ViewContext &ctx) : Page(ctx), _names(ctx.packages.mapNames()) {
}

LevelView::~LevelView() {
    deactivate();
}

void LevelView::deactivate() {
    unload();
    _renderer = nullptr;
}

void LevelView::unload() {
    if (!_renderer)
        return;
    if (_mesh)
        _renderer->destroyMesh(_mesh);
    if (_moverMesh)
        _renderer->destroyMesh(_moverMesh);
    _mesh = _moverMesh = 0;
    _movers.clear();
    if (_spriteQuad)
        _renderer->destroyMesh(_spriteQuad);
    _spriteQuad = 0;
    _sprites.clear();
    for (const GpuMesh &g : _gpuMeshes)
        if (g.mesh)
            _renderer->destroyMesh(g.mesh);
    _gpuMeshes.clear();
    _meshes.clear();
    _posedTopology.clear();
    _liveMeshes.clear(); // indexes into meshes_
    _meshActors.clear();
    _jump = -1;
    _sky.reset();
    for (gfx::TextureId t : _textures)
        _renderer->destroyTexture(t);
    _textures.clear();
    _batches.clear();
    _tints.clear();
    _surfRanges.clear();
    _swappedBatches.clear();
    _swappedSerial = -1;
    _animatedLightmaps.clear();
    _animatedTextures.clear();
    _animatedSince.clear();
    _animatedRefs.clear();
    _uniqueByRef.clear();
    _actorFades.clear();
    _changingLights.clear();
    _lightmapPages.clear();
    _textureCache.clear();
    _seeThroughTextures.clear();
    _hudFlis.clear();
    _lights = {};
    _shadows.clear();
    stopPlay();
    _posers.clear();
}

void LevelView::activate() {
    gfx::Renderer3D *device = nullptr;
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, device)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Maps page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        text(4, 4 + 3 * font::kLineHeight, "Tab: the other pages still work.");
        setTitle("3D rendering not available");
        return;
    }
    _recorder = std::make_unique<TextureUseRecorder>(*device);
    _renderer = _recorder.get();
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    _overlayTop = _overlayBottom = 0;
    load(_index);
}

void LevelView::load(int index) {
    unload();
    _index = index;
    _error.clear();
    _info.clear();
    const std::string &name = _names[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");
    _info.push_back(name + _ctx.packages.extension(name) + "  " + std::to_string(index + 1) + "/" +
                    std::to_string(_names.size()));

    _start = {0, 0, 0};
    _startYaw = 0;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const Package *pkg = _ctx.packages.get(name);
    Model model;
    std::string modelError;
    _zones = {};
    _zonesSeen = ~uint64_t(0);
    if (!pkg) {
        _error = _ctx.packages.error(name);
    } else if (findLevelModel(*pkg, &model, &modelError) < 0) {
        _error = modelError.empty() ? "No level geometry (a Model with BSP nodes) found" : modelError;
    }
    if (!_error.empty()) {
        toStart();
        return;
    }

    // The level's scripts, started (and a second of play): what they
    // change by the time play begins (lights turned off, editor icons
    // hidden, actors destroyed). P turns this off.
    const auto tModel = Clock::now();
    LevelStart start;
    std::string startError;
    bool started = false;
    // Travelling to it to play, the play's own start serves (its scripts
    // as they start, before any tick; those that change a light later are
    // followed live).
    _started.reset();
    _startedError.clear();
    if (_startOnLoad.wanted) {
        _started = startForPlay(*pkg, start, _startedError);
        started = _started != nullptr;
        startError = _startedError;
    } else if (_runScripts) {
        if (Error err = runLevelStart(_ctx.packages, *pkg, defaultGameType(_ctx.packages), 1.0f, start))
            startError = err.message;
        else
            started = true;
    }
    const ScriptedActors *scripted = started ? &start.changed : nullptr;

    // The lightmaps, lit from the level's lights, as textures.
    _lights = gatherLights(_ctx.packages, *pkg, model, scripted);
    _shadows.clear();
    _staticLightRefs.clear();
    for (const int32_t ref : model.lights)
        if (ref)
            _staticLightRefs.insert(ref);
    const LevelLights &lights = _lights;
    const auto tLights = Clock::now();
    LightmapAtlas atlas = buildLightmaps(model, lights);
    const auto tLightmaps = Clock::now();
    std::vector<gfx::TextureId> pages;
    for (const image::Image &page : atlas.pages) {
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        gfx::TextureId id = 0;
        if (_renderer->createTexture(page.w, page.h, page.rgba.data(), params, id))
            id = 0;
        else
            _textures.push_back(id);
        pages.push_back(id);
    }
    _hasLightmaps = !atlas.pages.empty();
    _lightmapPages = pages;
    _changingLights = lights.animated;
    _lightLevels.clear();
    _mapLevels.clear();
    for (const LightSource &l : _changingLights) {
        _lightLevels.push_back(l.startsOff ? 0.0f : lightLevel(l, 0));
        _mapLevels.push_back(l.startsOff ? 0.0f : lightLevel(l, l.type, lightClock(0), false));
    }
    _animatedLightmaps = std::move(atlas.animated);
    _staleLightmaps.assign(_animatedLightmaps.size(), false);
    _lightTime = 0;
    _lightClock = {};

    // Group the polygons by texture, drawing flags and lightmap page.
    const std::vector<Polygon> polys = levelPolygons(model);
    // And by the zones they're seen from, to leave out those not seen.
    _zones.build(model);
    _leafLights.clear();
    for (const Leaf &l : model.leaves)
        _leafLights.push_back(l.permeating);
    _volumes.clear();
    for (const LightSource &l : _lights.unique)
        if (l.volumeRadius > 0 && (l.volumeFog > 0 || l.volumeColor[0] + l.volumeColor[1] + l.volumeColor[2] > 0))
            _volumes.push_back(&l);
    // The surfaces in fog zones (bFogZone): those volume lights show on.
    uint64_t fogZones = 0;
    for (size_t z = 0; z < _lights.zoneFog.size() && z < 64; ++z)
        if (_lights.zoneFog[z])
            fogZones |= uint64_t(1) << z;
    std::map<std::tuple<bool, int32_t, uint32_t, int, uint64_t>, std::vector<const Polygon *>> groups;
    for (const Polygon &p : polys) {
        const BspSurf &s = model.surfs[size_t(p.surf)];
        uint32_t flags = s.polyFlags & kDrawFlags;
        if (p.zones & fogZones)
            flags |= kRenderFog;
        int page = blended(flags) || (flags & polyflag::kFakeBackdrop) ? -1 : atlas.surfs[size_t(p.surf)].page;
        if (flags & polyflag::kMirrored) // a mirror: a batch of its own, for its plane
            page = -2 - p.surf;
        // (A panning surface pans by its own zone's speeds: grouped by it.)
        groups[{blended(flags), s.texture, flags, page,
                _zones.portalCount() || (flags & kPanFlags) ? p.zones : ~uint64_t(0)}]
            .push_back(&p);
    }

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    // Each texture uploaded once; its size divides texture coordinates.
    int missing = 0;
    std::string firstMissing;
    auto textureOf = [&](const ObjectRef &tex, bool masked = false) {
        bool unreadable = false;
        const auto t = this->textureOf(tex, &unreadable, masked);
        if (unreadable) {
            ++missing;
            if (firstMissing.empty())
                firstMissing = tex.package->path(tex.exportIndex + 1);
        }
        return t;
    };
    auto textureFor = [&](const Package &from, int32_t ref,
                          uint32_t flags) -> std::pair<gfx::TextureId, std::array<int, 2>> {
        const ObjectRef tex = _ctx.packages.resolve(from, ref);
        if (!tex && ref != 0) {
            ++missing;
            if (firstMissing.empty())
                firstMissing = from.path(ref);
        }
        return textureOf(tex, (flags & polyflag::kMasked) != 0);
    };
    const gfx::Vec3 light = [] {
        const float l = std::sqrt(0.3f * 0.3f + 0.5f * 0.5f + 0.8f * 0.8f);
        return gfx::Vec3{0.3f / l, 0.5f / l, 0.8f / l};
    }();
    for (const auto &[key, group] : groups) {
        const auto &[isBlended, textureRef, flags, page, zones] = key;
        Batch batch;
        batch.polyFlags = flags;
        batch.zones = zones;
        batch.lightmap = page >= 0 ? pages[size_t(page)] : 0;
        if ((flags & polyflag::kMirrored) && !group.empty() && !group[0]->points.empty()) {
            batch.mirrorNormal = toWorld(group[0]->normal);
            const gfx::Vec3 on = toWorld(group[0]->points[0]);
            batch.mirrorD =
                batch.mirrorNormal[0] * on[0] + batch.mirrorNormal[1] * on[1] + batch.mirrorNormal[2] * on[2];
        }
        batch.firstIndex = uint32_t(indices.size());
        const auto [textureId, size] = textureFor(*pkg, textureRef, flags);
        batch.texture = textureId;
        batch.textureSize = {std::max(1, size[0]), std::max(1, size[1])};
        batch.textureRef = _ctx.packages.resolve(*pkg, textureRef);
        for (const Polygon *p : group) {
            const auto base = uint32_t(vertices.size());
            if (p->points.size() >= 3)
                _surfRanges[p->surf].push_back(
                    {_batches.size(), uint32_t(indices.size()), uint32_t(p->points.size() - 2) * 3});
            const gfx::Vec3 n = toWorld(p->normal);
            const float facing = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
            // Lit by lightmaps, a surface with none is full bright, as the
            // original draws it; without them, shaded by facing to show its shape.
            const bool fullBright = (flags & polyflag::kUnlit) || (_hasLightmaps && page < 0);
            const auto shade = uint8_t(fullBright ? 255 : 150 + int(90 * facing));
            for (size_t k = 0; k < p->points.size(); ++k) {
                gfx::Vertex v{};
                const gfx::Vec3 w = toWorld(p->points[k]);
                std::copy(w.begin(), w.end(), v.pos);
                v.uv[0] = p->texels[k][0] / float(size[0]);
                v.uv[1] = p->texels[k][1] / float(size[1]);
                if (page >= 0) {
                    const auto lm = atlas.uv(p->surf, p->points[k]);
                    v.uv2[0] = lm[0];
                    v.uv2[1] = lm[1];
                }
                std::copy(n.begin(), n.end(), v.normal);
                v.color[0] = v.color[1] = v.color[2] = shade;
                v.color[3] = 255;
                vertices.push_back(v);
            }
            for (size_t k = 2; k < p->points.size(); ++k) {
                indices.push_back(base);
                indices.push_back(base + uint32_t(k) - 1);
                indices.push_back(base + uint32_t(k));
            }
        }
        // A translucent surface the lights reach: its tint's samples.
        if ((flags & polyflag::kTranslucent) && !(flags & polyflag::kUnlit) && _hasLightmaps) {
            std::vector<TintSample> samples;
            for (const Polygon *p : group) {
                const LightmapPlacement &pl = atlas.surfs[size_t(p->surf)];
                if (pl.page < 0 || size_t(pl.page) >= atlas.pages.size() || p->points.empty())
                    continue;
                std::vector<Vec3f> at = p->points;
                Vec3f mid{};
                for (const Vec3f &q : p->points)
                    for (size_t k = 0; k < 3; ++k)
                        mid[k] += q[k] / float(p->points.size());
                at.push_back(mid);
                const image::Image &img = atlas.pages[size_t(pl.page)];
                for (const Vec3f &q : at) {
                    const auto uv = atlas.uv(p->surf, q);
                    const int x = std::clamp(int(uv[0] * float(atlas.pageSize)), 0, img.w - 1);
                    const int y = std::clamp(int(uv[1] * float(atlas.pageSize)), 0, img.h - 1);
                    TintSample t;
                    t.at = q;
                    for (size_t a = 0; a < _animatedLightmaps.size() && t.anim < 0; ++a) {
                        const AnimatedLightmap &m = _animatedLightmaps[a];
                        if (m.page == pl.page && x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
                            t.anim = int(a);
                            t.texel = size_t(y - m.y) * size_t(m.w) + size_t(x - m.x);
                        }
                    }
                    const uint8_t *px = img.pixel(x, y);
                    t.light = {float(px[0]) / 255 * 2, float(px[1]) / 255 * 2, float(px[2]) / 255 * 2};
                    samples.push_back(t);
                }
            }
            if (!samples.empty()) {
                batch.tint = int(_tints.size());
                _tints.push_back(std::move(samples));
            }
        }
        batch.indexCount = uint32_t(indices.size()) - batch.firstIndex;
        if (batch.indexCount)
            _batches.push_back(batch);
    }
    if (!indices.empty())
        if (Error err = _renderer->createMesh(vertices, indices, _mesh))
            _error = err.message;

    // The movers: each brush's polygons in its own coordinates, grouped by
    // texture and flags, lit (without shadows) where it starts.
    std::vector<std::string> moverErrors;
    std::vector<Mover> movers = findMovers(_ctx.packages, *pkg, &moverErrors);
    vertices.clear();
    indices.clear();
    for (Mover &m : movers) {
        MoverDraw d;
        d.key = d.target = m.keyNum;
        d.mover = std::move(m);
        for (const Poly &p : d.mover.polys)
            for (const Vec3f &v : p.vertices) {
                const Vec3f q{(v[0] - d.mover.prePivot[0]) * d.mover.mainScale[0] * d.mover.postScale[0],
                              (v[1] - d.mover.prePivot[1]) * d.mover.mainScale[1] * d.mover.postScale[1],
                              (v[2] - d.mover.prePivot[2]) * d.mover.mainScale[2] * d.mover.postScale[2]};
                d.reach = std::max(d.reach, std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
            }
        const gfx::Mat4 place = moverTransform(d);
        d.built = place;
        std::map<std::pair<int32_t, uint32_t>, std::vector<const Poly *>> moverGroups;
        for (const Poly &p : d.mover.polys)
            if (p.vertices.size() >= 3 && !(p.polyFlags & (polyflag::kInvisible | polyflag::kPortal)))
                moverGroups[{p.texture, p.polyFlags & kDrawFlags}].push_back(&p);
        for (const auto &[key, group] : moverGroups) {
            Batch batch;
            batch.polyFlags = key.second;
            batch.firstIndex = uint32_t(indices.size());
            const auto [textureId, size] = textureFor(*pkg, key.first, key.second);
            batch.texture = textureId;
            for (const Poly *p : group) {
                const auto base = uint32_t(vertices.size());
                // The normal as the mover is placed, in Unreal's axes: where
                // its tip goes less where the origin goes, made unit length.
                const gfx::Vec3 origin = gfx::transformPoint(place, Vec3f{});
                const gfx::Vec3 tip = gfx::transformPoint(place, p->normal);
                Vec3f normal{tip[0] - origin[0], -(tip[1] - origin[1]), tip[2] - origin[2]};
                const float len = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                for (float &c : normal)
                    c = len > 0 ? c / len : 0;
                for (const Vec3f &v : p->vertices) {
                    gfx::Vertex vx{};
                    std::copy(v.begin(), v.end(), vx.pos);
                    const Vec3f d2{v[0] - p->base[0], v[1] - p->base[1], v[2] - p->base[2]};
                    vx.uv[0] =
                        (d2[0] * p->textureU[0] + d2[1] * p->textureU[1] + d2[2] * p->textureU[2] + float(p->panU)) /
                        float(size[0]);
                    vx.uv[1] =
                        (d2[0] * p->textureV[0] + d2[1] * p->textureV[1] + d2[2] * p->textureV[2] + float(p->panV)) /
                        float(size[1]);
                    std::copy(p->normal.begin(), p->normal.end(), vx.normal);
                    const gfx::Vec3 w = gfx::transformPoint(place, v);
                    const Vec3f light = (key.second & polyflag::kUnlit)
                                            ? Vec3f{1, 1, 1}
                                            : lightAt(lights, Vec3f{w[0], -w[1], w[2]}, normal);
                    for (size_t k = 0; k < 3; ++k)
                        vx.color[k] = uint8_t(light[k] * 255 + 0.5f);
                    vx.color[3] = 255;
                    vertices.push_back(vx);
                }
                for (size_t k = 2; k < p->vertices.size(); ++k) {
                    indices.push_back(base);
                    indices.push_back(base + uint32_t(k) - 1);
                    indices.push_back(base + uint32_t(k));
                }
            }
            batch.indexCount = uint32_t(indices.size()) - batch.firstIndex;
            if (batch.indexCount)
                d.batches.push_back(batch);
        }
        _movers.push_back(std::move(d));
    }
    if (!indices.empty())
        if (Error err = _renderer->createMesh(vertices, indices, _moverMesh))
            _error = err.message;
    _sky = findSkyZone(_ctx.packages, *pkg);
    // Each zone's sky, as ZoneInfo.LinkToSkybox links it: the SkyZoneInfo
    // tagged the zone's Event, else the first there is. The view shows its
    // own zone's (CM_ArdPrison's two: the sun for CM07A, a moon for CM07B).
    _zoneSkies.clear();
    if (_sky) {
        ClassDefaults defaults(_ctx.packages);
        std::vector<std::pair<std::string, Vec3f>> skies;
        for (size_t i = 0; i < pkg->exports().size(); ++i) {
            if (pkg->exports()[i].classRef == 0 || !isA(_ctx.packages, *pkg, int(i), "SkyZoneInfo"))
                continue;
            ActorProperties props;
            std::array<float, 3> at{};
            if (!defaults.actorProperties(*pkg, int(i), props) && props.vector("Location", at))
                skies.emplace_back(fs::toLower(props.nameValue("Tag")), Vec3f{at[0], at[1], at[2]});
        }
        for (const Zone &z : model.zones) {
            std::optional<Vec3f> sky = _sky;
            const ObjectRef zone = z.actor ? _ctx.packages.resolve(*pkg, z.actor) : ObjectRef{};
            ActorProperties props;
            if (zone && !defaults.actorProperties(*zone.package, zone.exportIndex, props)) {
                const std::string event = fs::toLower(props.nameValue("Event"));
                if (!event.empty() && event != "none")
                    for (const auto &[tag, at] : skies)
                        if (tag == event) {
                            sky = at;
                            break;
                        }
            }
            _zoneSkies.push_back(sky);
        }
    }

    // Actors drawn as meshes: each mesh read and uploaded once; its skins
    // by texture index (MultiSkins, else Skin, else the mesh's own); lit
    // by the level's lights where it stands.
    ClassDefaults defaults(_ctx.packages);
    MeshActorStats meshStats;
    std::map<std::pair<const Package *, int>, int> meshIndex; // -1: unreadable
    int badMeshes = 0;
    int borrowed = 0;
    std::string firstBadMesh;
    const std::vector<MeshActor> meshActors = findMeshActors(_ctx.packages, *pkg, defaults, &meshStats, scripted);
    // The animations each mesh's actors play, and the idles that may stand
    // in for a one-frame pose (below).
    std::map<std::pair<const Package *, int>, std::set<std::string>> wanted;
    for (const MeshActor &a : meshActors) {
        auto &w = wanted[{a.mesh.package, a.mesh.exportIndex}];
        if (!a.animSequence.empty())
            w.insert(a.animSequence);
        w.insert(idleFor(a.animSequence));
    }
    for (const MeshActor &a : meshActors) {
        auto it = meshIndex.find({a.mesh.package, a.mesh.exportIndex});
        if (it == meshIndex.end()) {
            Mesh mesh;
            GpuMesh gpu;
            Error err = readMesh(*a.mesh.package, a.mesh.exportIndex, mesh);
            // Characters' animations past their own frames, from the shared
            // animation sets (read the first time one's needed).
            if (!err && mesh.bones)
                for (const std::string &seq : wanted[{a.mesh.package, a.mesh.exportIndex}]) {
                    if (mesh.findAnim(seq))
                        continue;
                    if (!_ctx.animationSets) {
                        const auto t0 = Clock::now();
                        _ctx.sets();
                        _animationSetsMs =
                            int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
                    }
                    borrowed += borrowNamedAnimation(*a.mesh.package, a.mesh.exportIndex, _ctx.sets(), seq, mesh);
                }
            if (!err)
                err = uploadMesh(*_renderer, mesh, gpu, _ctx.meshTurn, _ctx.meshYawQuarters);
            int at = -1;
            if (err) {
                ++badMeshes;
                if (firstBadMesh.empty())
                    firstBadMesh = err.message;
            } else {
                at = int(_meshes.size());
                _meshes.push_back(std::move(mesh));
                _gpuMeshes.push_back(gpu);
            }
            it = meshIndex.emplace(std::make_pair(a.mesh.package, a.mesh.exportIndex), at).first;
        }
        if (it->second < 0)
            continue;
        const Mesh &mesh = _meshes[size_t(it->second)];
        MeshDraw d;
        d.mesh = size_t(it->second);
        d.label = a.name + " (" + a.mesh.package->path(a.mesh.exportIndex + 1) +
                  (mesh.bones ? ", " + std::to_string(mesh.bones) + " bones" : std::string()) + ")";
        d.at = toWorld(a.location);
        d.drawScale = a.drawScale;
        d.skeletal = mesh.bones > 0;
        d.placement = meshPlacement(a.location, a.rotation, a.drawScale, a.prePivot);
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < a.multiSkins.size() ? a.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = a.skin;
            if (!tex && k < mesh.textures.size())
                tex = _ctx.packages.resolve(*a.mesh.package, mesh.textures[k]);
            d.textures.push_back(textureOf(tex).first);
        }
        d.light = meshLightAt(a.location);
        const MeshAnimSeq *anim = mesh.findAnim(a.animSequence);
        // Characters are placed in a one-frame pose (BaseRelax, BaseReady),
        // which the game's scripts follow with an idle loop: its idle
        // (Idle1, or IdleReady1 for a ready pose), which every humanoid's
        // list has, so they move when animations play.
        if (mesh.bones && (!anim || anim->numFrames <= 1))
            if (const MeshAnimSeq *idle = mesh.findAnim(idleFor(a.animSequence)))
                anim = idle;
        if (anim) {
            d.anim = int(anim - mesh.anims.data());
        } else if (mesh.bones) {
            // A character naming an animation its mesh doesn't have (the
            // Grigari drones' "BaseReady"): its idle, as the game's scripts
            // would play something.
            for (size_t k = 0; k < mesh.anims.size() && d.anim < 0; ++k)
                if (fs::toLower(mesh.anims[k].name).find("idle") != std::string::npos && mesh.anims[k].numFrames > 1)
                    d.anim = int(k);
        }
        d.startFraction = a.animFrame;
        _meshActors.push_back(std::move(d));
    }

    // Actors drawn as sprites, on one square turned to face the camera.
    SpriteActorStats spriteStats;
    for (const SpriteActor &a : findSpriteActors(_ctx.packages, *pkg, defaults, &spriteStats, scripted)) {
        const auto [texture, size] = textureOf(a.texture);
        if (!texture)
            continue;
        SpriteDraw d;
        d.texture = texture;
        d.at =
            toWorld(Vec3f{a.location[0] - a.prePivot[0], a.location[1] - a.prePivot[1], a.location[2] - a.prePivot[2]});
        d.width = float(size[0]) * a.drawScale;
        d.height = float(size[1]) * a.drawScale;
        d.style = a.style;
        d.vertical = a.vertical;
        // Lit by the level's lights where it stands, as meshes are, unless
        // unlit or blended (glowing effects).
        Vec3f light{1, 1, 1};
        if (!a.unlit && a.style != kStyleTranslucent && a.style != kStyleModulated) {
            light = {0.15f, 0.15f, 0.15f};
            for (const LightSource &l : lights.unique) {
                const float dist = lightDistance(l, a.location);
                if (dist < l.radius)
                    for (size_t k = 0; k < 3; ++k)
                        light[k] += l.color[k] * lightFalloff(dist / l.radius);
            }
            for (float &c : light)
                c = std::min(c, kMaxLight);
        }
        // Tinted as a live one is (spriteTint: its ColorFilter and glow).
        const std::array<float, 3> tint =
            spriteTint({a.colorFilter[0], a.colorFilter[1], a.colorFilter[2]}, a.scaleGlow);
        for (size_t k = 0; k < 3; ++k)
            d.color[k] = light[k] * tint[k];
        _sprites.push_back(d);
    }
    {
        // A square of side 1 across X (right) and Y (up), its texture
        // upright: the sprites', and the HUD's tiles when playing.
        std::vector<gfx::Vertex> quad(4);
        const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        for (size_t k = 0; k < 4; ++k) {
            quad[k] = {{corners[k][0], corners[k][1], 0},
                       {corners[k][0] + 0.5f, 0.5f - corners[k][1]},
                       {0, 0},
                       {0, 0, 1},
                       {255, 255, 255, 255}};
        }
        if (Error err = _renderer->createMesh(quad, {0, 1, 2, 0, 2, 3}, _spriteQuad))
            _sprites.clear();
    }
    const auto tMesh = Clock::now();

    // Start at the first PlayerStart, or above the level's middle.
    for (int i = 0; i < 3; ++i)
        _start[size_t(i)] = (model.boundsMin[size_t(i)] + model.boundsMax[size_t(i)]) / 2;
    _start = toWorld(_start);
    for (int i : pkg->exportsOfClass("PlayerStart")) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (pkg->readExport(i, data))
            continue;
        Reader r(data, size_t(pkg->exports()[size_t(i)].serialOffset));
        std::array<float, 3> loc;
        if (beginObject(*pkg, i, r, props) || !props.vector("Location", loc))
            continue;
        loc[2] += kEyeHeight;
        _start = toWorld(loc);
        Rotator rot;
        if (props.rotator("Rotation", rot))
            _startYaw = -float(rot.yaw) * 2 * kPi / 65536; // negated with Y
        break;
    }
    toStart();

    char line[320];
    std::snprintf(line, sizeof line,
                  "version %d/%d: %zu nodes, %zu surfaces, %zu polygons, %zu triangles in %zu batches", pkg->version(),
                  pkg->licensee(), model.nodes.size(), model.surfs.size(), polys.size(), indices.size() / 3,
                  _batches.size());
    _info.push_back(line);
    std::snprintf(line, sizeof line, "%zu textures%s%s", _textures.size(), missing ? ", missing or undecodable: " : "",
                  missing ? std::to_string(missing).c_str() : "");
    _info.push_back(missing ? std::string(line) + " (first: " + firstMissing + ")" : std::string(line));
    if (!model.hasLighting()) {
        _info.push_back("No lightmaps: " + model.lightingError);
    } else {
        const LightmapAtlas::Stats &st = atlas.stats;
        std::snprintf(line, sizeof line,
                      "lightmaps: %d surfaces (%d lit) on %zu pages, %zu lights (%d unreadable, %zu pulsing or "
                      "flickering over %zu lightmaps), %d grids off fit",
                      st.lightmaps, st.lit, atlas.pages.size(), lights.unique.size(), lights.unreadable,
                      _changingLights.size(), _animatedLightmaps.size(), st.badFit);
        _info.push_back(line);
    }
    std::snprintf(line, sizeof line,
                  "loaded in %.0f ms: model %.0f, light actors %.0f, lightmaps %.0f, textures and mesh %.0f",
                  ms(t0, tMesh), ms(t0, tModel), ms(tModel, tLights), ms(tLights, tLightmaps), ms(tLightmaps, tMesh));
    _info.push_back(line);
    std::snprintf(line, sizeof line, "%zu movers%s; sky zone: %s", _movers.size(),
                  moverErrors.empty() ? "" : (", " + std::to_string(moverErrors.size()) + " unreadable").c_str(),
                  _sky ? "yes" : "none");
    _info.push_back(moverErrors.empty() ? std::string(line) : std::string(line) + " (first: " + moverErrors[0] + ")");
    std::snprintf(line, sizeof line,
                  "%zu actors drawn as %zu meshes (%d with the mesh from class defaults, %d hidden, %d meshes "
                  "unreadable); class defaults found %d, not %d",
                  _meshActors.size(), _meshes.size(), meshStats.fromDefaults, meshStats.hidden, badMeshes,
                  defaults.found(), defaults.missing());
    _info.push_back(line);
    const auto turned = std::count_if(_meshes.begin(), _meshes.end(), [](const Mesh &m) { return bool(m.importAxes); });
    if (turned)
        _info.push_back(std::to_string(turned) + " imported meshes turned from their packed vertices: drawn " +
                        meshTurnName(_ctx.meshTurn) + " (M changes), then " +
                        std::to_string(_ctx.meshYawQuarters * 90) + " degrees of yaw (Y changes)");
    if (borrowed)
        _info.push_back(std::to_string(borrowed) + " animations given to characters from " +
                        std::to_string(_ctx.animationSets ? _ctx.animationSets->size() : 0) +
                        " shared animation sets (read in " + std::to_string(_animationSetsMs) + " ms)");
    if (!firstBadMesh.empty())
        _info.push_back("First unreadable mesh: " + firstBadMesh);
    std::snprintf(line, sizeof line, "%zu sprites (%d editor icons and %d hidden left out); %zu animated textures",
                  _sprites.size(), spriteStats.editorOnly, spriteStats.hidden, _animatedTextures.size());
    _info.push_back(line);
    if (started) {
        std::snprintf(
            line, sizeof line,
            "scripts started in %.0f ms (P turns them off), player %s: %d lights changed, %d actors hidden, %d shown, "
            "%d drawn differently, %d destroyed; %zu kinds of script error, %zu natives not written, None "
            "accessed %d times",
            start.milliseconds, start.player.empty() ? ("none, " + start.loginError).c_str() : start.player.c_str(),
            start.lights, start.hidden, start.shown, start.drawTypes, start.removed, start.errors, start.missingNatives,
            start.accessedNone);
        _info.push_back(line);
    } else {
        _info.push_back(_runScripts ? "scripts didn't start: " + startError : "scripts off (P turns them on)");
    }
    _info.push_back(
        "Not drawn yet: light effects' patterns across surfaces (a torch's waver, water's shimmer: as brightness only)");
}

bool LevelView::watchActor(const std::string &name) {
    if (!_play)
        return false;
    for (VmObject *a : _play->level().actors()) {
        if (a->deleted || fs::toLower(_play->vm().nameText(a->name)) != fs::toLower(name))
            continue;
        LevelPlay::Look look;
        if (!_play->look(a, look))
            return false;
        // In front of it, a little above, looking back at it.
        const Value *radius = a->find(_play->vm(), "CollisionRadius");
        const float back = std::max(60.0f, (radius ? radius->asFloat() : 20) * 4);
        const Vec3f ahead = rotationAxes(look.rotation)[0];
        const Vec3f eye{look.location[0] + ahead[0] * back, look.location[1] + ahead[1] * back,
                        look.location[2] + back * 0.3f};
        _pos = toWorld(eye);
        const gfx::Vec3 d{look.location[0] - eye[0], -(look.location[1] - eye[1]), look.location[2] - eye[2]};
        _yaw = std::atan2(d[1], d[0]);
        _pitch = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
        _freeCam = true;
        return true;
    }
    return false;
}

void LevelView::placeCamera(const Vec3f &at, float yaw, float pitch) {
    _pos = toWorld(at);
    _yaw = -yaw * kPi / 180;
    _pitch = pitch * kPi / 180;
}

std::vector<std::pair<std::string, Vec3f>> LevelView::actorPlaces(const std::string &classPart) {
    std::vector<std::pair<std::string, Vec3f>> out;
    if (!_play)
        return out;
    Vm &vm = _play->vm();
    for (VmObject *a : _play->level().actors()) {
        if (a->deleted || !a->cls || a->cls->name.find(classPart) == std::string::npos)
            continue;
        const Value *at = a->find(vm, "Location");
        const auto l = at ? Vm::vectorOf(*at) : std::array<float, 3>{};
        out.push_back({vm.nameText(a->name), Vec3f{l[0], l[1], l[2]}});
    }
    return out;
}

bool LevelView::teleportPlayer(const std::string &name, const Vec3f &offset, float yaw) {
    if (!_play || !_play->player())
        return false;
    Vm &vm = _play->vm();
    for (VmObject *a : _play->level().actors()) {
        if (a->deleted || fs::toLower(vm.nameText(a->name)) != fs::toLower(name))
            continue;
        const Value *at = a->find(vm, "Location");
        if (!at)
            return false;
        const auto l = Vm::vectorOf(*at);
        VmObject *p = _play->player();
        _play->level().setLocation(p, {l[0] + offset[0], l[1] + offset[1], l[2] + offset[2]});
        const int32_t units = int32_t(std::lround(yaw * 65536 / 360)) & 0xffff;
        for (const char *rot : {"Rotation", "ViewRotation"})
            if (Value *r = p->find(vm, rot))
                *r = vm.rotatorValue(0, units, 0);
        if (Value *v = p->find(vm, "Velocity"))
            *v = vm.vectorValue(0, 0, 0);
        // Falling, onto whatever's under it, if walking (going into water
        // has already set it swimming).
        if (Value *ph = p->find(vm, "Physics"); ph && ph->i == 1)
            ph->i = 2;
        return true;
    }
    return false;
}

bool LevelView::callFunction(const std::string &name, const std::string &function) {
    if (!_play)
        return false;
    Vm &vm = _play->vm();
    const bool player = fs::toLower(name) == "player";
    for (VmObject *a : _play->level().actors())
        if (!a->deleted && (player ? a == _play->player() : fs::toLower(vm.nameText(a->name)) == fs::toLower(name))) {
            vm.callEvent(a, function.c_str());
            return true;
        }
    return false;
}

std::string LevelView::consoleCommand(const std::string &command) {
    if (!_play || !_play->player())
        return "no player";
    const Value v = _play->vm().callEvent(_play->player(), "ConsoleCommand", {Value::stringValue(command)});
    return v.s;
}

std::string LevelView::describeActor(const std::string &name) {
    if (!_play)
        return "not playing";
    Vm &vm = _play->vm();
    for (VmObject *a : _play->level().actors()) {
        if (a->deleted || fs::toLower(vm.nameText(a->name)) != fs::toLower(name))
            continue;
        auto num = [&](const char *p) {
            const Value *v = a->find(vm, p);
            return v ? v->asFloat() : 0.0f;
        };
        auto flag = [&](const char *p) {
            const Value *v = a->find(vm, p);
            return v && v->asBool();
        };
        const Value *at = a->find(vm, "Location");
        const auto l = at ? Vm::vectorOf(*at) : std::array<float, 3>{};
        auto object = [&](const char *p) {
            const Value *v = a->find(vm, p);
            return v && v->asObject() ? vm.nameText(v->o->name) : std::string("None");
        };
        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "%s at %.1f %.1f %.1f, state %s, physics %d, radius %.0f height %.0f%s%s%s; draw type %d, "
                      "style %d, scale %.2f, glow %.2f, mesh %s, skin %s, texture %s, %zu copies",
                      a->cls->name.c_str(), double(l[0]), double(l[1]), double(l[2]),
                      a->state ? a->state->name.c_str() : "none", int(num("Physics")), double(num("CollisionRadius")),
                      double(num("CollisionHeight")), flag("bCollideActors") ? ", collides with actors" : "",
                      flag("bHidden") ? ", hidden" : "", a->disabled.empty() ? "" : ", some events disabled",
                      int(num("DrawType")), int(num("Style")), double(num("DrawScale")), double(num("ScaleGlow")),
                      object("Mesh").c_str(), object("Skin").c_str(), object("Texture").c_str(),
                      _play->level().multiMeshes(a).size());
        std::string out = buf;
        if (num("LightType") != 0 || num("LightBrightness") != 0) {
            char light[160];
            std::snprintf(light, sizeof light,
                          "; light type %d, brightness %d, hue %d, saturation %d, radius %d, effect %d",
                          int(num("LightType")), int(num("LightBrightness")), int(num("LightHue")),
                          int(num("LightSaturation")), int(num("LightRadius")), int(num("LightEffect")));
            out += light;
        }
        // Its layers (C3DPlayAnim): group, sequence, frame, weight and its
        // change a second, rate; loop, held at its end, clears.
        if (const auto *layers = _play->level().layersOf(a))
            for (const auto &[group, l] : *layers) {
                char layer[256];
                std::snprintf(layer, sizeof layer, "; layer %s: %s at %.2f, weight %.2f (%+.2f/s), rate %.2f%s%s%s%s%s",
                              vm.nameText(group).c_str(), l.sequence ? vm.nameText(l.sequence).c_str() : "none",
                              double(l.frame), double(l.weight), double(l.weightRate), double(l.rate),
                              l.loop ? ", looping" : "", l.finished ? ", at its end" : "",
                              l.clear[0] ? ", clear yaw" : "", l.clear[1] ? ", clear pitch" : "",
                              l.clear[2] ? ", clear roll" : "");
                out += layer;
            }
        return out;
    }
    return "no such actor";
}

std::string LevelView::describeView() {
    if (!_play)
        return "not playing";
    char buf[200];
    auto deg = [](int32_t r) { return double(int16_t(r & 0xffff)) * 360 / 65536; };
    std::snprintf(buf, sizeof buf, "at %.1f %.1f %.1f, pitch %.1f yaw %.1f roll %.1f, fov %.1f",
                  double(_view.location[0]), double(_view.location[1]), double(_view.location[2]),
                  deg(_view.rotation.pitch), deg(_view.rotation.yaw), deg(_view.rotation.roll), double(_view.fov));
    std::string out = buf;
    const std::array<float, 3> at{_view.location[0], _view.location[1], _view.location[2]};
    if (_play->level().inSolid(at))
        out += ", in solid";
    // How many actors are asleep (unseen for 5 seconds: bForceStasis).
    size_t asleep = 0;
    for (VmObject *a : _play->level().actors())
        if (const Value *v = a->deleted ? nullptr : a->find(_play->vm(), "bForceStasis"); v && v->asBool())
            ++asleep;
    out += ", " + std::to_string(asleep) + " actors asleep";
    return out;
}

std::string LevelView::actorProperty(const std::string &name, const std::string &property) {
    if (!_play)
        return "not playing";
    Vm &vm = _play->vm();
    // "player": the player's pawn, whatever it's called.
    const bool player = fs::toLower(name) == "player";
    for (VmObject *a : _play->level().actors())
        if (!a->deleted && (player ? a == _play->player() : fs::toLower(vm.nameText(a->name)) == fs::toLower(name))) {
            const Value *v = a->find(vm, property);
            if (!v)
                return "no such property";
            if (v->type != VType::kStruct || v->fields.empty())
                return vm.toString(*v);
            std::string out = "(";
            for (const Value &f : v->fields)
                out += (out.size() > 1 ? ", " : "") + vm.toString(f);
            return out + ")";
        }
    return "no such actor";
}

std::vector<std::string> LevelView::linesSaid() {
    std::vector<std::string> out;
    if (!_play)
        return out;
    ScriptLevel &level = _play->level();
    for (size_t k = 0; k < level.linesSaid().size(); ++k) {
        const int i = level.linesSaid()[k];
        const DialogLine &l = level.dialogLines()[size_t(i)];
        char at[32];
        std::snprintf(at, sizeof at, "%7.2f ", k < level.linesSaidAt().size() ? level.linesSaidAt()[k] : 0.0f);
        out.push_back(at + l.key() + ": " + l.text + (l.event.empty() ? "" : " [" + l.event + "]"));
    }
    return out;
}

float LevelView::playTime() const {
    return _play ? _play->time() : 0;
}

std::string LevelView::describePlayer() {
    if (!_play || !_play->player())
        return "no player";
    Vm &vm = _play->vm();
    VmObject *p = _play->player();
    auto vec = [&](const char *name) {
        const Value *v = p->find(vm, name);
        if (!v)
            return std::string("?");
        const auto a = Vm::vectorOf(*v);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%.1f %.1f %.1f", double(a[0]), double(a[1]), double(a[2]));
        return std::string(buf);
    };
    const Value *physics = p->find(vm, "Physics");
    const Value *seq = p->find(vm, "AnimSequence");
    const Value *frame = p->find(vm, "AnimFrame");
    char buf[64];
    std::snprintf(buf, sizeof buf, " %.2f", double(frame ? frame->asFloat() : 0));
    const Value *rot = p->find(vm, "Rotation");
    const int yaw = rot ? int(std::lround(double(Vm::rotatorOf(*rot)[1] & 0xffff) * 360 / 65536)) : 0;
    std::string weapon = "none";
    if (const Value *w = p->find(vm, "Weapon"); w && w->asObject()) {
        weapon = vm.nameText(w->o->name);
        if (const Value *ammo = w->o->find(vm, "AmmoType"); ammo && ammo->asObject())
            if (const Value *n = ammo->o->find(vm, "AmmoAmount"))
                weapon += " (" + std::to_string(n->asInt()) + ")";
    }
    return "weapon " + weapon + ", at " + vec("Location") + ", yaw " + std::to_string(yaw) + ", velocity " +
           vec("Velocity") + ", state " + _play->playerState() + ", physics " +
           std::to_string(physics ? physics->asInt() : -1) + ", anim " +
           (seq ? vm.nameText(seq->i) : std::string("?")) + buf;
}

bool LevelView::selectMap(const std::string &name) {
    for (size_t i = 0; i < _names.size(); ++i)
        if (fs::toLower(_names[i]) == fs::toLower(name)) {
            if (_renderer)
                load(int(i));
            else
                _index = int(i);
            return true;
        }
    return false;
}

void LevelView::toStart() {
    _pos = _start;
    _yaw = _startYaw;
    _pitch = 0;
}

namespace {

// A key as the engine names it to the console (EInputKey): 13 Enter, 27
// Escape, 37 to 40 the arrows, letters and digits as their capitals' ASCII,
// 0x70 F1...; 0 for one it doesn't know.
int inputKey(uint32_t k) {
    if (k >= 'a' && k <= 'z')
        return int(k - 'a' + 'A');
    if ((k >= '0' && k <= '9') || k == key::kSpace || k == key::kTab || k == key::kBackspace || k == key::kEscape)
        return int(k);
    if (k >= key::kF1 && k < key::kF1 + 12)
        return int(0x70 + k - key::kF1);
    switch (k) {
    case key::kReturn:
    case key::kKpEnter:
        return 13;
    case key::kLeft:
        return 37;
    case key::kUp:
        return 38;
    case key::kRight:
        return 39;
    case key::kDown:
        return 40;
    case key::kLShift:
    case key::kRShift:
        return 16;
    case key::kLCtrl:
    case key::kRCtrl:
        return 17;
    case key::kLAlt:
    case key::kRAlt:
        return 18;
    case key::kPageUp:
        return 0x21;
    case key::kPageDown:
        return 0x22;
    case key::kEnd:
        return 0x23;
    case key::kHome:
        return 0x24;
    case key::kInsert:
        return 0x2d;
    case key::kDelete:
        return 0x2e;
    default:
        return 0;
    }
}

} // namespace

bool LevelView::viewerKeys() const {
    return !_game || kDeveloperKeys;
}

void LevelView::handleEvent(const Event &ev) {
    if (!_renderer || _names.empty())
        return;
    // Playing with the game's view, each key goes to the game's console
    // first, as the engine hands them on: while its menus show, it takes
    // the arrows and Enter, and the page leaves them be.
    if (_play && !_freeCam && (ev.type == EventType::kKeyDown || ev.type == EventType::kKeyUp) && !ev.repeat &&
        !(ev.type == EventType::kKeyDown && ev.key == 'v'))
        if (const int k = inputKey(ev.key)) {
            // The menu's keys (Enter, the arrows) are the game's whatever
            // KeyEvent answers: the HUD's says false having acted on Enter.
            const bool menuKey = _play->menuShowing() && (k == 13 || (k >= 37 && k <= 40));
            if ((_play->consoleKey(k, ev.type == EventType::kKeyDown) || menuKey) && ev.type == EventType::kKeyDown) {
                for (bool &h : _held)
                    h = false;
                return;
            }
        }
    auto hold = [&](uint32_t k, bool down) {
        if (_play && !_freeCam) // playing, the arrows turn and look, and Ctrl or F fires
            switch (k) {
            case key::kLeft:
                _held[kTurnLeft] = down;
                return true;
            case key::kRight:
                _held[kTurnRight] = down;
                return true;
            case key::kUp:
                _held[kLookUp] = down;
                return true;
            case key::kDown:
                _held[kLookDown] = down;
                return true;
            case key::kLCtrl:
            case key::kRCtrl:
            case 'f':
                _held[kFire] = down;
                return true;
            default:
                break;
            }
        switch (k) {
        case 'w':
            _held[kForward] = down;
            return true;
        case 's':
            _held[kBack] = down;
            return true;
        case 'a':
            _held[kLeft] = down;
            return true;
        case 'd':
            _held[kRight] = down;
            return true;
        case key::kSpace:
        case 'e':
            _held[kUp] = down;
            return true;
        case 'c':
        case 'q':
            _held[kDown] = down;
            return true;
        case key::kLShift:
        case key::kRShift:
            _held[kFast] = down;
            return true;
        default:
            return false;
        }
    };
    if (ev.type == EventType::kKeyUp) {
        if (_play && !_freeCam && ev.key >= key::kF1 && ev.key < key::kF1 + 12)
            _play->pressKey("F" + std::to_string(ev.key - key::kF1 + 1), false);
        hold(ev.key, false);
    } else if (ev.type == EventType::kKeyDown) {
        if (_play && !ev.repeat && ev.key == 'v' && viewerKeys()) { // the camera let loose, or back to the game's
            _freeCam = !_freeCam;
            for (bool &h : _held)
                h = false;
            return;
        }
        if (_play && !_freeCam && !ev.repeat) { // the keys bound to exec functions
            if (ev.key == key::kSpace)
                _play->press("Jump");
            else if (ev.key == key::kLCtrl || ev.key == key::kRCtrl || ev.key == 'f')
                _play->press("Fire");
            else if (ev.key >= '0' && ev.key <= '9') // the weapons, as the game binds them
                _play->pressKey(std::string(1, char(ev.key)));
            else if (ev.key >= key::kF1 && ev.key < key::kF1 + 12) // the communicator, the tricorder...
                _play->pressKey("F" + std::to_string(ev.key - key::kF1 + 1));
            else if (ev.key == key::kEscape) { // the game's menu (ShowMenu)
                _play->pressKey("Escape");
                return;
            }
        }
        if (hold(ev.key, true) || ev.repeat || !viewerKeys())
            return;
        if (ev.key == key::kReturn || ev.key == key::kKpEnter) {
            for (bool &h : _held)
                h = false;
            if (_play)
                stopPlay();
            else
                startPlay();
            return;
        }
        const int n = int(_names.size());
        if (ev.key == key::kLeft || ev.key == key::kRight)
            load((_index + (ev.key == key::kLeft ? n - 1 : 1)) % n);
        else if (ev.key == 'r')
            toStart();
        else if (ev.key == 'i')
            _showInfo = !_showInfo;
        else if (ev.key == 't')
            _textured = !_textured;
        else if (ev.key == 'l')
            _lightmaps = !_lightmaps;
        else if (ev.key == '[')
            _lightScale = std::max(0.1f, _lightScale / 1.25f);
        else if (ev.key == ']')
            _lightScale = std::min(8.0f, _lightScale * 1.25f);
        else if (ev.key == '-')
            _displayGamma = std::max(0.5f, _displayGamma - 0.05f);
        else if (ev.key == '=')
            _displayGamma = std::min(2.5f, _displayGamma + 0.05f);
        else if (ev.key == 'k')
            _showSky = !_showSky;
        else if (ev.key == 'z')
            _zoneCulling = !_zoneCulling;
        else if (ev.key == 'n')
            _animate = !_animate;
        else if (ev.key == 'g' || ev.key == 'h')
            jumpToMeshActor(ev.key == 'h');
        else if (ev.key == 'm' || ev.key == 'y') {
            if (ev.key == 'm')
                _ctx.meshTurn = MeshTurn((int(_ctx.meshTurn) + 1) % 3);
            else
                _ctx.meshYawQuarters = (_ctx.meshYawQuarters + 1) % 4;
            // Kept where it is: only the meshes need uploading again, but
            // the level loads quickly enough to just load it again.
            const auto pos = _pos;
            const float yaw = _yaw;
            const float pitch = _pitch;
            load(_index);
            _pos = pos;
            _yaw = yaw;
            _pitch = pitch;
        } else if (ev.key == 'p') {
            _runScripts = !_runScripts;
            const auto pos = _pos;
            const float yaw = _yaw;
            const float pitch = _pitch;
            load(_index);
            _pos = pos;
            _yaw = yaw;
            _pitch = pitch;
        } else if (ev.key == 'o')
            for (MoverDraw &m : _movers)
                if (m.key == m.target && m.mover.numKeys > 1) {
                    m.target = (m.key + 1) % m.mover.numKeys;
                    m.t = 0;
                }
    } else if (_play && !_freeCam && _play->windowsOpen() &&
               (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown ||
                ev.type == EventType::kMouseUp)) {
        // The game's windows (the options) open: the mouse is theirs, its
        // pointer where the page's is, its buttons keys 1 and 2.
        _play->consoleMouse(float(ev.mouseX), float(ev.mouseY));
        if (ev.type != EventType::kMouseMove && (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight))
            _play->consoleKey(ev.button == MouseButton::kLeft ? 1 : 2, ev.type == EventType::kMouseDown);
    } else if (_play && !_freeCam && _play->menuShowing() &&
               (ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) &&
               (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight)) {
        // The HUD's menus: the buttons as the game binds them (LeftMouse
        // Fire selects, RightMouse AltFire steps the choice), not a drag.
        const bool left = ev.button == MouseButton::kLeft;
        const bool down = ev.type == EventType::kMouseDown;
        if (!_play->consoleKey(left ? 1 : 2, down))
            _play->pressKey(left ? "LeftMouse" : "RightMouse", down);
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging && _play && !_freeCam) {
        // Playing, the mouse's movement goes to the player's input.
        _mouseDx += float(ev.mouseX - _lastX);
        _mouseDy -= float(ev.mouseY - _lastY);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.005f;
        _pitch = std::clamp(_pitch - float(ev.mouseY - _lastY) * 0.005f, -1.55f, 1.55f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    }
}

void LevelView::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    // The real time since the last frame, and as the game takes it (at
    // most a tenth of a second at once). Textures go by the real time, as
    // the engine's UTexture::Update does: a long wait is theirs to take
    // (UTexture::Tick keeps a slow texture to MinFrameRate).
    const float real = _lastMs ? float(nowMs - _lastMs) / 1000.0f : 0.0f;
    const float dt = std::min(0.1f, real);
    _lastMs = nowMs;

    if (_play) {
        const auto playFrom = std::chrono::steady_clock::now();
        updatePlay(dt);
        _frameTimes.play =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - playFrom).count();
        // Its sounds, heard from the game's camera.
        if (_ctx.mixer && !_audio)
            _audio = std::make_unique<LevelAudio>(*_ctx.mixer, _ctx.packages);
        if (_audio)
            _audio->update(*_play, _view.location, _view.rotation, dt);
        if (_play->quitAsked())
            stopPlay();
        else if (const std::string url = _play->takeTravel(); !url.empty())
            travel(url);
    }
    if (!_play || _freeCam) { // flying: not playing, or playing with the camera let loose (V)
        if (!_play)
            _fov = 90; // Unreal's default field of view, across
        const gfx::Vec3 f{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
        const gfx::Vec3 r{std::sin(_yaw), -std::cos(_yaw), 0};
        const float step = kSpeed * (_held[kFast] ? 3.0f : 1.0f) * dt;
        for (size_t k = 0; k < 3; ++k) {
            _pos[k] += f[k] * step * float(_held[kForward] - _held[kBack]);
            _pos[k] += r[k] * step * float(_held[kRight] - _held[kLeft]);
        }
        _pos[2] += step * float(_held[kUp] - _held[kDown]);
    }
    const gfx::Vec3 forward{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 right{std::sin(_yaw), -std::cos(_yaw), 0};

    const float aspect = float(kFrameW) / float(kFrameH);
    const float fovY = 2 * std::atan(std::tan(std::clamp(_fov, 1.0f, 170.0f) * kPi / 360) / aspect);
    gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 65536), gfx::lookAlong(_pos, forward, {0, 0, 1}));
    // Movers between keyframes, at their own pace.
    for (MoverDraw &m : _movers)
        if (m.key != m.target) {
            m.t += dt / std::max(0.05f, m.mover.moveTime);
            if (m.t >= 1) {
                m.key = m.target;
                m.t = 0;
            }
        }

    updateLightmaps(real);
    updateAnimatedTextures(real);
    gatherDynamicLights(_pos);
    // The Fallen's distance fog: the camera's zone's (DistanceFogging, the
    // depth at which all is its DistanceFogColor), growing evenly from the
    // camera (Direct3D's linear table fog from 0 to it); nothing beyond it
    // is drawn (Render.dll's SetDistanceClipping puts the far plane there),
    // but the sky.
    _fog = {};
    if (_play)
        _fog.distance = _play->distanceFog(Vec3f{_pos[0], -_pos[1], _pos[2]}, _fog.color);
    if (_fog.distance > 0)
        viewProj = gfx::multiply(gfx::perspective(fovY, aspect, 4, std::max(_fog.distance, 8.0f)),
                                 gfx::lookAlong(_pos, forward, {0, 0, 1}));

    // The renderer raises colours to 1 / its gamma.
    _renderer->setGamma(1 / _displayGamma);
    const gfx::Mat4 proj = gfx::perspective(fovY, aspect, 4, 65536);
    const bool sky = _sky && _showSky;
    if (sky) {
        // The sky first: the level seen from the sky zone, looking the
        // same way; then the view, from scratch, with the backdrop
        // surfaces leaving it showing.
        Vec3f skyAt = *_sky;
        if (const int z = _zones.zoneAt(Vec3f{_pos[0], -_pos[1], _pos[2]});
            z >= 0 && size_t(z) < _zoneSkies.size() && _zoneSkies[size_t(z)])
            skyAt = *_zoneSkies[size_t(z)];
        const gfx::Vec3 eye = toWorld(skyAt);
        const gfx::Mat4 skyView = gfx::multiply(proj, gfx::lookAlong(eye, forward, {0, 0, 1}));
        seeZonesFrom(eye, skyView, false);
        _renderer->beginFrame(skyView, eye, {20, 24, 40});
        drawBatches(levelBatches(), _mesh, gfx::identity(), true);
        drawSurfaceSwaps();
        _renderer->clearDepth();
        _renderer->setView(viewProj, _pos);
    } else {
        _renderer->beginFrame(viewProj, _pos, {20, 24, 40});
    }
    seeZonesFrom(_pos, viewProj, true);
    if (_animate)
        _animTime += dt;
    if (_play)
        _coronaDt = dt;
    // The last frame's posed meshes, drawn by now.
    for (const GpuMesh &g : _posedMeshes)
        if (g.mesh)
            _renderer->destroyMesh(g.mesh);
    _posedMeshes.clear();
    drawMirrors(proj, gfx::lookAlong(_pos, forward, {0, 0, 1}), viewProj, _pos, forward, right, 0, nullptr);
    // Backdrop surfaces: with the sky, only their depth, hiding what's
    // behind them; without it, a plain colour.
    for (const Batch &b : _batches) {
        if (!(b.polyFlags & polyflag::kFakeBackdrop) || !seen(b))
            continue;
        gfx::DrawCall call;
        call.mesh = _mesh;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.state.cull = gfx::Cull::kNone;
        call.color = {0.15f, 0.2f, 0.35f, 1};
        if (sky) {
            call.state.src = gfx::Blend::kZero;
            call.state.dst = gfx::Blend::kOne;
        }
        _renderer->draw(call);
    }
    drawWorld(forward, right, _pos);
    if (_play)
        drawScreenFlash(forward, right);
    if (_play && !_freeCam)
        drawGameHud();
    drawOverlay(nowMs);
    if (_play)
        drawPlayHud();
    const auto endFrom = std::chrono::steady_clock::now();
    _renderer->endFrame(&_overlay);
    _frameTimes.endFrame =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - endFrom).count();
}

// The game's own HUD, over the world: the player's PreRender and
// PostRender with the canvas (ScriptLevel::renderHud), then the actors and
// tiles they drew, in screen pixels: cut out where see-through, translucent ones added,
// modulated ones multiplied in.
void LevelView::drawGameHud() {
    const int w = _renderer->width();
    const int h = _renderer->height();
    ScriptLevel::HudView v;
    v.location = {_view.location[0], _view.location[1], _view.location[2]};
    v.rotation = {_view.rotation.pitch, _view.rotation.yaw, _view.rotation.roll};
    v.fov = _view.fov;
    v.width = w;
    v.height = h;
    _play->level().renderHud(v);
    const ScriptLevel::Hud &hud = _play->level().hud();
    gfx::Mat4 pixels = gfx::identity();
    pixels[0] = 2.0f / float(w);
    pixels[5] = -2.0f / float(h);
    pixels[10] = 0;
    pixels[12] = -1;
    pixels[13] = 1;
    // In the order the HUD drew them: the actors over the tiles before
    // them, each in its view (the camera and rectangle SetRenderFrame set:
    // a talker's head in its portrait), the depth cleared first if asked.
    size_t next = 0;
    bool flat = false; // set for tiles
    auto drawActorsUpTo = [&](size_t tiles) {
        std::vector<SpriteDraw> sprites;
        while (next < hud.actors.size() && hud.actors[next].afterTiles <= tiles) {
            const ScriptLevel::HudActor &a = hud.actors[next++];
            const ScriptLevel::HudView &v = a.view;
            const auto axes = rotationAxes(Rotator{v.rotation[0], v.rotation[1], v.rotation[2]});
            const gfx::Vec3 eye = toWorld(Vec3f{v.location[0], v.location[1], v.location[2]});
            const gfx::Vec3 ahead = toWorld(axes[0]);
            const gfx::Vec3 up = toWorld(axes[2]);
            const gfx::Vec3 side{-toWorld(axes[1])[0], -toWorld(axes[1])[1], -toWorld(axes[1])[2]};
            const float aspect = float(v.width) / float(std::max(1, v.height));
            const float fovY = 2 * std::atan(std::tan(std::clamp(v.fov, 1.0f, 170.0f) * kPi / 360) / aspect);
            _renderer->setViewport(v.x, v.y, v.width, v.height);
            _renderer->setView(gfx::multiply(gfx::perspective(fovY, aspect, 1, 65536), gfx::lookAlong(eye, ahead, up)),
                               eye);
            if (a.clearZ)
                _renderer->clearDepth();
            sprites.clear();
            drawLiveActor(a.actor, sprites, &a);
            drawSprites(sprites, ahead, side);
            flat = false;
        }
        _renderer->setViewport(0, 0, w, h);
    };
    for (size_t i = 0; i < hud.tiles.size(); ++i) {
        if (next < hud.actors.size() && hud.actors[next].afterTiles <= i)
            drawActorsUpTo(i);
        if (!flat) {
            _renderer->setView(pixels, gfx::Vec3{0, 0, 0});
            flat = true;
        }
        const ScriptLevel::HudTile &t = hud.tiles[i];
        const auto [id, size] =
            t.frame > 0 ? hudFliTexture(t.texture, t.frame, t.seeThrough) : hudTexture(t.texture, t.seeThrough);
        if (!id)
            continue;
        gfx::DrawCall call;
        call.mesh = _spriteQuad;
        call.indexCount = 6;
        call.model = gfx::identity();
        // The quad's corners are -0.5..0.5 with v down from its top.
        call.model[0] = t.w;
        call.model[5] = -t.h;
        call.model[12] = t.x + t.w / 2;
        call.model[13] = t.y + t.h / 2;
        call.texture = id;
        const float tw = float(size[0]);
        const float th = float(size[1]);
        call.preRow0 = {t.ul / tw, 0, t.u / tw};
        call.preRow1 = {0, t.vl / th, t.v / th};
        call.color = {float(t.color[0]) / 255, float(t.color[1]) / 255, float(t.color[2]) / 255, 1};
        call.state.cull = gfx::Cull::kNone;
        call.state.depthTest = false;
        call.state.depthWrite = false;
        // See-through texels stay so in every style (letters are drawn
        // masked however they blend).
        call.state.alphaTest = gfx::AlphaTest::kGe128;
        if (t.style == kStyleTranslucent) {
            call.state.src = call.state.dst = gfx::Blend::kOne;
        } else if (t.style == kStyleModulated) {
            call.color = {1, 1, 1, 1};
            call.state.src = gfx::Blend::kDstColor;
            call.state.dst = gfx::Blend::kSrcColor;
        }
        _renderer->draw(call);
    }
    drawActorsUpTo(hud.tiles.size());
}

// An FliTexture's frame for the HUD: its animation shown at that frame and
// uploaded when it changes (one texture each, and each way of colour 0).
std::pair<gfx::TextureId, std::array<int, 2>> LevelView::hudFliTexture(const ObjectRef &tex, int frame,
                                                                       bool seeThrough) {
    auto key = std::make_tuple(tex.package, tex.exportIndex, seeThrough);
    auto it = _hudFlis.find(key);
    if (it == _hudFlis.end()) {
        HudFli f;
        f.anim = openAnimatedTexture(_ctx.packages, *tex.package, tex.exportIndex);
        if (f.anim) {
            f.anim->showFrame(frame, seeThrough);
            f.frame = frame;
            const image::Image &img = f.anim->image();
            gfx::TextureParams params;
            params.nearest = true;
            params.mipmaps = false;
            if (!_renderer->createTexture(img.w, img.h, img.rgba.data(), params, f.id))
                _textures.push_back(f.id);
            else
                f.id = 0;
        }
        it = _hudFlis.emplace(key, std::move(f)).first;
    }
    HudFli &f = it->second;
    if (!f.anim || !f.id)
        return textureOf(tex);
    if (f.frame != frame && f.anim->showFrame(frame, seeThrough)) {
        const image::Image &img = f.anim->image();
        _renderer->updateTextures({{f.id, 0, 0, img.w, img.h, img.rgba.data()}});
    }
    f.frame = frame;
    return {f.id, {std::max(1, f.anim->image().w), std::max(1, f.anim->image().h)}};
}

// A texture for the HUD, with its colour 0 see-through if asked (letters,
// masked tiles), kept by both.
std::pair<gfx::TextureId, std::array<int, 2>> LevelView::hudTexture(const ObjectRef &tex, bool seeThrough) {
    if (!seeThrough)
        return textureOf(tex);
    auto it = _seeThroughTextures.find({tex.package, tex.exportIndex});
    if (it == _seeThroughTextures.end()) {
        image::Image img;
        gfx::TextureId id = 0;
        gfx::TextureParams params;
        params.nearest = true;
        params.mipmaps = false;
        if (!decodeTexture(_ctx.packages, *tex.package, tex.exportIndex, img, nullptr, true) &&
            !_renderer->createTexture(img.w, img.h, img.rgba.data(), params, id))
            _textures.push_back(id);
        else
            id = 0;
        it = _seeThroughTextures
                 .emplace(std::make_pair(tex.package, tex.exportIndex),
                          std::make_pair(id, std::array<int, 2>{std::max(1, img.w), std::max(1, img.h)}))
                 .first;
    }
    return it->second;
}

// The sprites, facing the camera: those drawn solid first, then the
// blended ones over them (not hiding what's behind).
void LevelView::drawSprites(const std::vector<SpriteDraw> &sprites, const gfx::Vec3 &forward, const gfx::Vec3 &right) {
    const gfx::Vec3 up{right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                       right[0] * forward[1] - right[1] * forward[0]};
    for (int blendedPass = 0; blendedPass < 2; ++blendedPass)
        for (const SpriteDraw &d : sprites) {
            const bool blend = d.style == kStyleTranslucent || d.style == kStyleModulated;
            if (blend != (blendedPass == 1))
                continue;
            // Vertical sprites stay upright, turning only about Z.
            const gfx::Vec3 u = d.oriented ? d.up : d.vertical ? gfx::Vec3{0, 0, 1} : up;
            const gfx::Vec3 r = d.oriented ? d.right : right;
            gfx::DrawCall call;
            call.mesh = _spriteQuad;
            call.indexCount = 6;
            call.model = gfx::identity();
            for (size_t k = 0; k < 3; ++k) {
                call.model[k] = r[k] * d.width;
                call.model[4 + k] = u[k] * d.height;
                call.model[8 + k] = -forward[k];
                call.model[12 + k] = d.at[k];
            }
            call.state.depthBias = d.oriented;
            if (d.onTop)
                call.state.depthTest = false;
            call.texture = _textured ? d.texture : 0;
            call.color = {d.color[0], d.color[1], d.color[2], 1};
            call.state.cull = gfx::Cull::kNone;
            if (d.style == kStyleTranslucent) {
                call.state.src = call.state.dst = gfx::Blend::kOne;
                call.state.depthWrite = false;
            } else if (d.style == kStyleModulated) {
                // The background times the texture, doubled: grey leaves it as is.
                call.color = {1, 1, 1, 1};
                call.state.src = gfx::Blend::kDstColor;
                call.state.dst = gfx::Blend::kSrcColor;
                call.state.depthWrite = false;
            } else {
                call.state.alphaTest = gfx::AlphaTest::kGe128; // masked: index 0 see-through
            }
            _renderer->draw(call);
        }
}

// Moves the fire, water, wet, ice and FLC textures on, uploading those that
// changed.
// Fire, water and the rest tick as UE1's do: only those drawn (since the
// last time here), each by the time since it last ticked, so one unseen a
// while takes that time at once. Their own pace allows one step for it.
void LevelView::updateAnimatedTextures(float dt) {
    std::vector<gfx::TextureRegion> regions;
    _animatedSince.resize(_animatedTextures.size(), 0);
    const std::unordered_set<gfx::TextureId> *used = _recorder ? &_recorder->used() : nullptr;
    for (size_t i = 0; i < _animatedTextures.size(); ++i) {
        const auto &[id, anim] = _animatedTextures[i];
        _animatedSince[i] += dt;
        if (used && !used->count(id))
            continue;
        const float since = _animatedSince[i];
        _animatedSince[i] = 0;
        // While playing, an FLC shows the frame its texture's script side
        // has (its Mode, FrameTime and FrameNum, played on as it's drawn:
        // UFliTexture::Tick): one an explosion plays once stops.
        bool changed = false;
        VmObject *fli = nullptr;
        if (_play && anim->frames() > 0 && i < _animatedRefs.size() && _animatedRefs[i].first.package) {
            const ObjectRef &ref = _animatedRefs[i].first;
            fli = _play->vm().objectAt(*ref.package, ref.exportIndex + 1);
        }
        if (fli) {
            _play->level().fliDrawn(fli);
            const Value *frame = fli->find(_play->vm(), "FrameNum");
            changed = anim->showFrame(frame ? std::max(1, frame->asInt()) : 1, _animatedRefs[i].second);
        } else {
            changed = anim->advance(since);
        }
        if (changed)
            regions.push_back({id, 0, 0, anim->image().w, anim->image().h, anim->image().rgba.data()});
    }
    if (_recorder)
        _recorder->used().clear();
    if (!regions.empty())
        _renderer->updateTextures(regions);
}

// A changing light's share of its brightness now: its curve; while
// playing, a light the scripts may change as its live actor is (its
// LightType and LightBrightness: a TriggerLight fading, a light turned
// off, or destroyed).
float LevelView::liveLightLevel(const LightSource &l, bool whole) {
    if (!_play && l.startsOff)
        return 0; // as the level starts: its scripts haven't turned it on
    if (!_play || !l.scripted || !l.ref)
        return lightLevel(l, l.type, _lightClock, whole);
    VmObject *a = _play->actorAt(l.ref);
    if (!a)
        return 0;
    Vm &vm = _play->vm();
    const Value *type = a->find(vm, "LightType");
    const Value *brightness = a->find(vm, "LightBrightness");
    const float share = brightness && l.brightness ? float(brightness->asInt()) / float(l.brightness) : 1;
    return std::clamp(lightLevel(l, type ? uint8_t(type->asInt()) : l.type, _lightClock, whole) * share, 0.0f, 2.0f);
}

// Redraws the lightmaps that pulsing, flickering... lights reach, where
// those lights' brightness changed, and those with a light effect's
// pattern on them every frame (it moves across them). Meshes take each
// light's level as a whole (lightLevels_); the lightmaps theirs less a
// pattern, which lightPattern gives texel by texel (mapLevels_). Only
// lightmaps in a zone seen are drawn; the others are drawn when it is.
void LevelView::updateLightmaps(float dt) {
    _lightTime += dt;
    // Playing, the lights go by the level's time, the random ones by the
    // frames drawn; textures play on by the real clock, paused or not.
    if (_play) {
        const float now = _play->level().time();
        ++_lightClock.frame;
        if (now != _lightClock.time)
            _lightClock.strobeOn = !_lightClock.strobeOn;
        _lightClock.time = now;
        _play->level().setRealTime(_lightTime);
    } else {
        _lightClock = lightClock(_lightTime);
    }
    std::vector<bool> changed(_changingLights.size(), false);
    _patternNow.resize(_changingLights.size());
    for (size_t i = 0; i < _changingLights.size(); ++i) {
        _lightLevels[i] = liveLightLevel(_changingLights[i]);
        const float level = liveLightLevel(_changingLights[i], false);
        const bool pattern = hasLightPattern(_changingLights[i]) && level > 0;
        if (pattern)
            _patternNow[i] = patternNow(_changingLights[i], _lightClock);
        if (pattern || std::fabs(level - _mapLevels[i]) * 255 >= 1) {
            _mapLevels[i] = level;
            changed[i] = true;
        }
    }
    _staleLightmaps.resize(_animatedLightmaps.size(), false);
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<gfx::TextureRegion> regions;
    for (size_t k = 0; k < _animatedLightmaps.size(); ++k) {
        const AnimatedLightmap &m = _animatedLightmaps[k];
        const bool redraw = _staleLightmaps[k] ||
                            std::any_of(m.lights.begin(), m.lights.end(),
                                        [&](const AnimatedLightmap::Light &l) { return changed[size_t(l.index)]; });
        if (!redraw || size_t(m.page) >= _lightmapPages.size() || !_lightmapPages[size_t(m.page)])
            continue;
        if (m.zone >= 0 && m.zone < 64 && !((_zonesSeen >> m.zone) & 1)) {
            _staleLightmaps[k] = true;
            continue;
        }
        _staleLightmaps[k] = false;
        pixels.emplace_back();
        m.draw(_mapLevels, pixels.back(), &_changingLights, &_patternNow);
        regions.push_back({_lightmapPages[size_t(m.page)], m.x, m.y, m.w, m.h, nullptr});
    }
    if (regions.empty())
        return;
    for (size_t i = 0; i < regions.size(); ++i)
        regions[i].rgba = pixels[i].data();
    _renderer->updateTextures(regions);
}

// The idle loop a character placed in pose `anim` plays (see load()).
std::string LevelView::idleFor(const std::string &anim) {
    return fs::toLower(anim).rfind("baseready", 0) == 0 ? "IdleReady1" : "Idle1";
}

// Puts the camera before the next placed mesh (or the next whose mesh is
// posed from bones: the characters), looking at it along X.
void LevelView::jumpToMeshActor(bool skeletalOnly) {
    const int n = int(_meshActors.size());
    for (int step = 1; step <= n; ++step) {
        const int i = (_jump + step) % n;
        const MeshDraw &d = _meshActors[size_t(i)];
        if (skeletalOnly && !d.skeletal)
            continue;
        _jump = i;
        // Back from it by its size as placed (The Fallen's Arachnids are
        // drawn at 0.03 of their mesh: backing off by the mesh's own size
        // went through the walls).
        const GpuMesh &g = _gpuMeshes[d.mesh];
        const float back = std::clamp(g.radius * std::fabs(d.drawScale) * 3, 30.0f, 400.0f);
        _pos = {d.at[0] - back, d.at[1], d.at[2] + back * 0.25f};
        _yaw = 0;
        _pitch = -0.24f;
        return;
    }
}

gfx::Mat4 LevelView::moverTransform(const MoverDraw &d) const {
    const Mover &m = d.mover;
    // Where it is between its two keyframes.
    const Vec3f base = m.basePosition();
    const Rotator baseRot = m.baseRotation();
    const Vec3f &a = m.keyPos[size_t(d.key)];
    const Vec3f &b = m.keyPos[size_t(d.target)];
    const Rotator &ra = m.keyRot[size_t(d.key)];
    const Rotator &rb = m.keyRot[size_t(d.target)];
    const float t = d.t;
    const Vec3f location{base[0] + a[0] + (b[0] - a[0]) * t, base[1] + a[1] + (b[1] - a[1]) * t,
                         base[2] + a[2] + (b[2] - a[2]) * t};
    const Rotator rot{baseRot.pitch + ra.pitch + int32_t(float(rb.pitch - ra.pitch) * t),
                      baseRot.yaw + ra.yaw + int32_t(float(rb.yaw - ra.yaw) * t),
                      baseRot.roll + ra.roll + int32_t(float(rb.roll - ra.roll) * t)};
    return moverPlacement(m, location, rot);
}

gfx::Mat4 LevelView::moverPlacement(const Mover &m, const Vec3f &location, const Rotator &rot) {
    // location + postScale * (rotation * (mainScale * (v - prePivot))),
    // then Y negated into the renderer's axes.
    const gfx::Mat4 flip = scaling({1, -1, 1});
    gfx::Mat4 m4 = gfx::multiply(flip, translation(location));
    m4 = gfx::multiply(m4, scaling(m.postScale));
    m4 = gfx::multiply(m4, rotation(rot));
    m4 = gfx::multiply(m4, scaling(m.mainScale));
    return gfx::multiply(m4, translation({-m.prePivot[0], -m.prePivot[1], -m.prePivot[2]}));
}

// The zones seen from `eye` (gfx's coordinates) through the portals, for
// drawBatches to leave out the rest.
void LevelView::seeZonesFrom(const gfx::Vec3 &eye, const gfx::Mat4 &viewProj, bool mainView) {
    const Vec3f at{eye[0], -eye[1], eye[2]}; // Unreal's coordinates have y the other way
    if (!_zoneCulling) {
        _zonesSeen = ~uint64_t(0);
    } else {
        gfx::Mat4 flip = gfx::identity();
        flip[5] = -1;
        _zonesSeen = _zones.visible(at, gfx::multiply(viewProj, flip));
    }
    // The zones' render times (The Fallen's sleep); the view's zone, the
    // main view's only (not the sky's).
    if (_play)
        _play->zonesDrawn(_zonesSeen, mainView ? _zones.zoneAt(at) : -1);
}

// The level's batches, playing: with the textures the scripts have swapped
// for others everywhere (SetSurfaceTexture's bReplaceAllWorld).
const std::vector<LevelView::Batch> &LevelView::levelBatches() {
    if (!_play || _play->level().surfaceSwaps().world.empty())
        return _batches;
    const ScriptLevel::SurfaceSwaps &swaps = _play->level().surfaceSwaps();
    if (_swappedSerial != swaps.serial) {
        _swappedSerial = swaps.serial;
        _swappedBatches = _batches;
        for (Batch &b : _swappedBatches)
            if (auto it = swaps.world.find({b.textureRef.package, b.textureRef.exportIndex}); it != swaps.world.end())
                b.texture = textureOf(it->second, nullptr, (b.polyFlags & polyflag::kMasked) != 0).first;
    }
    return _swappedBatches;
}

// One surface's texture swapped (SetSurfaceTexture): its polygons drawn
// again over themselves with the new one, then its lightmap. The texture
// coordinates are the old texture's, so a new one of another size would
// be stretched; The Fallen's switches swap textures of one size.
void LevelView::drawSurfaceSwaps() {
    if (!_play)
        return;
    for (const auto &[surf, tex] : _play->level().surfaceSwaps().surfaces) {
        auto it = _surfRanges.find(surf);
        if (it == _surfRanges.end())
            continue;
        for (const SurfRange &r : it->second) {
            if (r.batch >= _batches.size())
                continue;
            Batch b = _batches[r.batch];
            b.firstIndex = r.firstIndex;
            b.indexCount = r.indexCount;
            b.texture = textureOf(tex, nullptr, (b.polyFlags & polyflag::kMasked) != 0).first;
            drawBatches({b}, _mesh, gfx::identity(), true, true);
        }
    }
}

Vec3f LevelView::tintOf(int tint) const {
    const std::vector<TintSample> &samples = _tints[size_t(tint)];
    Vec3f sum{};
    for (const TintSample &t : samples) {
        Vec3f light = t.light;
        if (t.anim >= 0) {
            const AnimatedLightmap &m = _animatedLightmaps[size_t(t.anim)];
            light = m.base[t.texel];
            for (const AnimatedLightmap::Light &l : m.lights) {
                const float level = size_t(l.index) < _mapLevels.size() ? _mapLevels[size_t(l.index)] : 1;
                for (size_t k = 0; k < 3; ++k)
                    light[k] += l.share[t.texel][k] * level;
            }
        }
        for (const LightSource &l : _dynamic) {
            const float dist = lightDistance(l, t.at);
            if (dist < l.radius)
                for (size_t k = 0; k < 3; ++k)
                    light[k] += l.color[k] * lightFalloff(dist / l.radius);
        }
        for (size_t k = 0; k < 3; ++k)
            sum[k] += std::min(light[k], kMaxLight);
    }
    // Twice the average, at most the texture's own colour: fitted to a
    // recording of the original's logos (lit by a TriggerLight that only
    // reaches 60% before its dispatcher puts it out, they show at about
    // their texture's colour; the plain average was 2.5 times too dark).
    for (float &v : sum)
        v = std::min(kMaxLight, 2 * v / float(std::max<size_t>(samples.size(), 1)));
    return sum;
}

void LevelView::drawBatches(const std::vector<Batch> &batches, gfx::MeshId mesh, const gfx::Mat4 &model, bool levelLit,
                            bool over, const MeshLight *moverLight) {
    auto lit = [&](const Batch &b) {
        return levelLit && _lightmaps && b.lightmap && !blended(b.polyFlags) &&
               !(b.polyFlags & (polyflag::kFakeBackdrop | polyflag::kUnlit));
    };
    for (const Batch &b : batches) {
        if ((b.polyFlags & polyflag::kFakeBackdrop) || !seen(b))
            continue;
        if (&b == _mirrorThrough) // the mirror the view is through
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.model = model;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        if ((b.polyFlags & polyflag::kMirrored) && !blended(b.polyFlags) && _mirrorLevel < 3 &&
            _renderer->hasStencil()) {
            // An opaque mirror shows only what it reflects: its depth, to
            // hide what's behind it.
            call.state.src = gfx::Blend::kZero;
            call.state.dst = gfx::Blend::kOne;
            call.state.cull = gfx::Cull::kNone;
            _renderer->draw(call);
            continue;
        }
        // One-sided, as UE1's surfaces are (their back, the way our
        // winding goes, is the renderer's front), unless flagged two-sided:
        // a camera in solid sees through the wall it's in. M03_SiskoL1A's
        // opening rides down inside the rock beside the lift shaft, looking
        // in at the far wall's lamp and braces; drawn two-sided, all it saw
        // was the back of the near wall.
        // A mover's polygons (its brush's) wind the other way round: from
        // inside M04_WorfL1a's turbolift, culled as the level's, all that
        // showed was its brush's untextured outer faces from behind.
        call.state.cull = (b.polyFlags & polyflag::kTwoSided) ? gfx::Cull::kNone
                          : moverLight                        ? gfx::Cull::kBack
                                                              : gfx::Cull::kFront;
        call.texture = _textured ? b.texture : 0;
        if (b.polyFlags & kPanFlags) {
            const auto [du, dv] = surfacePan(b);
            call.preRow0[2] = du / float(b.textureSize[0]);
            call.preRow1[2] = dv / float(b.textureSize[1]);
        }
        // With a lightmap the second pass lights it; else the vertex colours
        // (shading by facing, or a mover's light).
        call.rgbSource = lit(b) ? gfx::ColorSource::kConstant : gfx::ColorSource::kVertex;
        if (moverLight && !(b.polyFlags & polyflag::kUnlit) && !blended(b.polyFlags)) {
            call.rgbSource = gfx::ColorSource::kLighting;
            call.ambientLight = moverLight->ambient;
            call.directedLight = moverLight->directed;
            call.lightDirection = moverLight->direction;
            if (moverLight->perVertex) {
                call.surfaceLighting = true;
                call.vertexLightCount = int(std::min<size_t>(moverLight->lights.size(), gfx::kMaxVertexLights));
                for (int i = 0; i < call.vertexLightCount; ++i)
                    call.vertexLights[size_t(i)] = moverLight->lights[size_t(i)];
            }
        }
        if (!(b.polyFlags & polyflag::kModulated)) // those multiply what's there, so stay as they are
            call.color = {kExposure, kExposure, kExposure, 1};
        if (b.tint >= 0 && _lightmaps) {
            const Vec3f t = tintOf(b.tint);
            call.color = {kExposure * t[0] * _lightScale, kExposure * t[1] * _lightScale,
                          kExposure * t[2] * _lightScale, 1};
        }
        if (b.polyFlags & polyflag::kMasked)
            call.state.alphaTest = gfx::AlphaTest::kGe128;
        if (over) // drawn over what's there already
            call.state.depthEqual = true;
        if (b.polyFlags & polyflag::kTranslucent) {
            call.state.src = gfx::Blend::kOne;
            call.state.dst = gfx::Blend::kOneMinusSrcColor;
            call.state.depthWrite = false;
        } else if (b.polyFlags & polyflag::kModulated) {
            call.state.src = gfx::Blend::kDstColor;
            call.state.dst = gfx::Blend::kSrcColor;
            call.state.depthWrite = false;
            call.rgbSource = gfx::ColorSource::kConstant;
        }
        _renderer->draw(call);
        if (lit(b)) {
            // The lightmap over it, doubled (dst * src * 2).
            gfx::DrawCall light = call;
            light.texture = b.lightmap;
            light.texGen = gfx::TexGen::kUv2;
            light.rgbSource = gfx::ColorSource::kConstant;
            light.color = {_lightScale, _lightScale, _lightScale, 1};
            light.state.src = gfx::Blend::kDstColor;
            light.state.dst = gfx::Blend::kSrcColor;
            light.state.depthEqual = true;
            light.state.depthWrite = false;
            light.state.alphaTest = gfx::AlphaTest::kNone;
            _renderer->draw(light);
        }
        // The lights that move and change (a spark's flash, an
        // explosion's) added over it: its texture times their light, as
        // UE1 adds dynamic light to the lightmap's.
        if (!_dynamic.empty() && !over && !blended(b.polyFlags) && !(b.polyFlags & polyflag::kUnlit) && call.texture) {
            gfx::DrawCall dyn = call;
            dyn.dynamicLightsTextured = true;
            dyn.state.src = gfx::Blend::kOne;
            dyn.state.dst = gfx::Blend::kOne;
            dyn.state.depthEqual = true;
            dyn.state.depthWrite = false;
            _renderer->draw(dyn);
        }
        // The zone's distance fog over it, last.
        if (_fog.distance > 0 && !over && !blended(b.polyFlags))
            drawFogOver(*_renderer, call, _fog);
    }
}

// The level's volume lights (VolumeRadius) over its surfaces in fog zones,
// as the original's volumetric lighting does through their fog maps: at
// each point, for each light, how much of its sphere lies between the
// camera and the surface (its density 1 - (d / r)^2 along the line, 1
// straight through the middle); its colour (VolumeBrightness) times that
// added, and what's there dimmed by its VolumeFog times that (blended
// One, OneMinusSrcAlpha).
void LevelView::drawVolumes(const gfx::Vec3 &eye) {
    if (_volumes.empty() || !_lightmaps)
        return;
    const Vec3f e{eye[0], -eye[1], eye[2]};
    std::vector<std::pair<float, const LightSource *>> near;
    for (const LightSource *l : _volumes) {
        const float dx = l->position[0] - e[0];
        const float dy = l->position[1] - e[1];
        const float dz = l->position[2] - e[2];
        near.push_back({std::sqrt(dx * dx + dy * dy + dz * dz) - l->volumeRadius, l});
    }
    std::sort(near.begin(), near.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<gfx::DynamicLight> lights;
    for (size_t i = 0; i < near.size() && i < size_t(gfx::kMaxDynamicLights); ++i) {
        const LightSource &l = *near[i].second;
        lights.push_back({toWorld(l.position),
                          l.volumeRadius,
                          {l.volumeColor[0] * kExposure, l.volumeColor[1] * kExposure, l.volumeColor[2] * kExposure},
                          l.volumeFog});
    }
    _renderer->setVolumeLights(lights);
    volumesOver(levelBatches(), _mesh, gfx::identity(), gfx::Cull::kFront, true);
}

// The volume lights over batches drawn (the level's in fog zones, flagged
// so; or all of a mover's in one), where they left their depth.
void LevelView::volumesOver(const std::vector<Batch> &batches, gfx::MeshId mesh, const gfx::Mat4 &model, gfx::Cull cull,
                            bool flagged) {
    for (const Batch &b : batches) {
        if ((flagged && !(b.polyFlags & kRenderFog)) || (b.polyFlags & polyflag::kFakeBackdrop) ||
            blended(b.polyFlags) || !seen(b) || &b == _mirrorThrough)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.model = model;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.state.cull = (b.polyFlags & polyflag::kTwoSided) ? gfx::Cull::kNone : cull;
        drawVolumeOver(*_renderer, call);
    }
}

bool LevelView::inFogZone(const Vec3f &at) const {
    if (_volumes.empty() || !_lightmaps)
        return false;
    const int z = _zones.zoneAt(at);
    return z >= 0 && size_t(z) < _lights.zoneFog.size() && _lights.zoneFog[size_t(z)];
}

// The lit actors that aren't the level's own lights (those are in its
// lightmaps) nor static (as in UE1, those light the world only through
// lightmaps): their light as it is now, the nearest kMaxDynamicLights to
// the eye given to the renderer.
void LevelView::gatherDynamicLights(const gfx::Vec3 &eye) {
    _dynamic.clear();
    if (!_play)
        return;
    _play->level().litActors(_litScratch);
    const Package *map = &_play->map();
    for (const ScriptLevel::LitActor &a : _litScratch) {
        if (a.isStatic || (a.actor->package == map && _staticLightRefs.count(a.actor->exportIndex + 1)))
            continue;
        LightSource l;
        l.position = {a.location[0], a.location[1], a.location[2]};
        l.radius = kRadiusUnit * (float(a.radius) + 1);
        l.type = a.type;
        l.effect = a.effect;
        l.period = float(std::max<uint8_t>(a.period, 1)) / 35;
        l.phase = float(a.phase) / 256;
        const float level = lightLevel(l, l.type, _lightClock);
        if (level <= 0)
            continue;
        // As the level's lights: the original lights with them the same way.
        const Vec3f c = lightColor(a.hue, a.saturation, a.brightness, _lights.levelBrightness);
        l.color = {c[0] * level, c[1] * level, c[2] * level};
        l.key = a.actor;
        _dynamic.push_back(l);
    }
    const Vec3f e{eye[0], -eye[1], eye[2]};
    auto dist2 = [&](const LightSource &l) {
        const float dx = l.position[0] - e[0];
        const float dy = l.position[1] - e[1];
        const float dz = l.position[2] - e[2];
        return dx * dx + dy * dy + dz * dz;
    };
    if (_dynamic.size() > size_t(gfx::kMaxDynamicLights)) {
        std::partial_sort(_dynamic.begin(), _dynamic.begin() + gfx::kMaxDynamicLights, _dynamic.end(),
                          [&](const LightSource &a, const LightSource &b) { return dist2(a) < dist2(b); });
        _dynamic.resize(size_t(gfx::kMaxDynamicLights));
    }
    std::vector<gfx::DynamicLight> lights;
    for (const LightSource &l : _dynamic)
        lights.push_back({toWorld(l.position), l.radius, {l.color[0], l.color[1], l.color[2]}});
    _renderer->setDynamicLights(lights);
}

void LevelView::drawOverlay(uint64_t nowMs) {
    ++_frames;
    if (nowMs - _fpsStart >= 1000) {
        _fps = float(_frames) * 1000.0f / float(nowMs - _fpsStart);
        _fpsStart = nowMs;
        _frames = 0;
    }
    // Only the rows drawn last frame are cleared: the box at the top, the
    // play lines at the bottom (clearing all 2.7 MB a frame cost a tenth).
    const size_t rowBytes = size_t(kFrameW) * 4;
    const size_t top = std::min(size_t(std::clamp(_overlayTop, 0, kFrameH)) * rowBytes, _overlay.pixels.size());
    const size_t bottom = std::min(size_t(std::clamp(_overlayBottom, 0, kFrameH)) * rowBytes, _overlay.pixels.size());
    std::fill(_overlay.pixels.begin() + ptrdiff_t(bottom), _overlay.pixels.end(), uint8_t(0));
    _overlayBottom = kFrameH;
    // A cut scene's black bars are the HUD's own (CTV_Hud.RenderLetterBox,
    // with the subtitles drawn over them).
    if (!_showInfo && _error.empty()) {
        std::fill(_overlay.pixels.begin(), _overlay.pixels.begin() + ptrdiff_t(top), uint8_t(0));
        _overlayTop = 0;
        _overlayLines.clear();
        return;
    }
    std::vector<std::string> lines = _info;
    const size_t errorLine = _error.empty() ? size_t(-1) : lines.size();
    if (!_error.empty())
        lines.push_back(_error);
    if (!_playError.empty())
        lines.push_back("Can't play: " + _playError);
    lines.push_back(
        !_play ? "Enter plays the level"
        : _freeCam
            ? "Playing, the camera loose (V back to the game's): W/S/A/D, Space/C and drag fly it"
            : "Playing (Enter stops): W/S move, A/D step aside, arrows turn and look, drag to look, Space "
              "jumps, C crouches, Ctrl or F fires, 0-9 pick weapons, F1-F12 as the game binds them, Esc the game's menu, V lets "
              "the camera loose");
    char where[240];
    // Shown in Unreal's own coordinates.
    const std::string zones = !_zones.portalCount()        ? "no zone portals"
                              : !_zoneCulling              ? "all zones drawn (Z)"
                              : _zonesSeen == ~uint64_t(0) ? "all zones seen"
                              : std::popcount(_zonesSeen) == 1
                                  ? "1 zone seen (Z draws all)"
                                  : std::to_string(std::popcount(_zonesSeen)) + " zones seen (Z draws all)";
    std::snprintf(where, sizeof where,
                  "at %.0f %.0f %.0f, %.0f fps; lightmaps %s, brightness x%.2f, display gamma %.2f; %s", _pos[0],
                  -_pos[1], _pos[2], _fps,
                  !_hasLightmaps ? "none"
                  : _lightmaps   ? "on"
                                 : "off",
                  _lightScale, _displayGamma, zones.c_str());
    lines.push_back(where);
    if (_jump >= 0 && size_t(_jump) < _meshActors.size()) {
        const MeshDraw &d = _meshActors[size_t(_jump)];
        std::snprintf(where, sizeof where,
                      "mesh actor %d of %zu: %s, at %.0f %.0f %.0f (G next, H next posed from bones)", _jump + 1,
                      _meshActors.size(), d.label.c_str(), d.at[0], -d.at[1], d.at[2]);
        lines.push_back(where);
    }
    const int lh = font::kLineHeight;
    size_t widest = 0;
    for (const std::string &l : lines)
        widest = std::max(widest, font::length(l));
    const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8);
    const int boxH = std::min(kFrameH, int(lines.size()) * lh + 6);
    // The box is kept from frame to frame (nothing else draws up there):
    // only the lines that changed (the place and the frame rate, mostly)
    // are drawn again. Drawing all its text each frame cost a twentieth.
    const bool same = boxW == _overlayBoxW && boxH == _overlayTop && lines.size() == _overlayLines.size() &&
                      errorLine == _overlayErrorLine && bottom >= top;
    auto band = [&](int from, int to) {
        for (int y = std::max(0, from); y < std::min(to, boxH); ++y) {
            uint8_t *p = _overlay.row(y);
            std::fill(p, p + size_t(kFrameW) * 4, uint8_t(0));
            for (int x = 0; x < boxW; ++x)
                p[x * 4 + 3] = 150;
        }
    };
    if (!same) {
        std::fill(_overlay.pixels.begin(), _overlay.pixels.begin() + ptrdiff_t(top), uint8_t(0));
        band(0, boxH);
        _overlayLines.assign(lines.size(), std::string(1, '\0'));
    }
    _overlayTop = boxH;
    _overlayBoxW = boxW;
    _overlayErrorLine = errorLine;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (_overlayLines[i] == lines[i])
            continue;
        if (same)
            band(3 + int(i) * lh, 3 + int(i + 1) * lh);
        font::drawText(_overlay, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
        _overlayLines[i] = lines[i];
    }
}

std::pair<gfx::TextureId, std::array<int, 2>> LevelView::textureOf(const ObjectRef &tex, bool *unreadable,
                                                                   bool masked) {
    if (!tex)
        return {0, {64, 64}};
    auto it = _textureCache.find({tex.package, tex.exportIndex, masked});
    if (it == _textureCache.end()) {
        image::Image img;
        gfx::TextureId id = 0;
        TextureInfo ti;
        // A masked surface leaves colour 0 out whether or not its texture
        // is marked masked (CM_SpaceStation's windows, an FLC over the hull).
        if (decodeTexture(_ctx.packages, *tex.package, tex.exportIndex, img, &ti, masked)) {
            if (unreadable)
                *unreadable = true;
        } else {
            // Fire, water, wet, ice and FLC textures and flipbooks play,
            // redrawn as they change (so without mipmaps, which updates
            // don't redo).
            std::unique_ptr<AnimatedTexture> anim;
            if (isAnimatedTexture(_ctx.packages, *tex.package, tex.exportIndex))
                anim = openAnimatedTexture(_ctx.packages, *tex.package, tex.exportIndex);
            if (anim && masked && anim->frames() > 0)
                anim->showFrame(1, true);
            if (anim && anim->image().w == img.w && anim->image().h == img.h)
                img = anim->image();
            else
                anim.reset();
            gfx::TextureParams params;
            params.nearest = (ti.polyFlags & polyflag::kNoSmooth) != 0;
            params.mipmaps = !anim;
            if (!_renderer->createTexture(img.w, img.h, img.rgba.data(), params, id)) {
                _textures.push_back(id);
                if (anim) {
                    _animatedTextures.push_back({id, std::move(anim)});
                    _animatedRefs.push_back({tex, masked});
                }
            } else {
                id = 0;
            }
        }
        it = _textureCache
                 .emplace(std::make_tuple(tex.package, tex.exportIndex, masked),
                          std::make_pair(id, std::array<int, 2>{std::max(1, img.w), std::max(1, img.h)}))
                 .first;
    }
    return it->second;
}

// The lights reaching where an actor stands: some all round, the rest
// from their average direction.
MeshLight LevelView::meshLightAt(const Vec3f &at, const LevelPlay::Look *actor, const void *who) const {
    MeshLight light;
    if (actor) {
        // A live actor is lit by its lights per vertex (actorLights), all
        // round only by the zone's ambient and its glow.
        actorLights(at, *actor, light, who);
        return light;
    }
    Vec3f total{};
    Vec3f toward{};
    // The level's lights as bright as they are now (pulsing, flickering,
    // turned off by scripts), then the lights that move and change.
    // While playing, only lights that see it: the level's walls (its BSP)
    // between a light and the actor keep its light off it, as they keep it
    // off the walls behind them (Sisko lit white in M03_SiskoL1A's red room
    // by the yellow lamps of the rooms round it).
    float sum = 0;
    // A special lit light (bSpecialLit) lights none of them: CM11_LabRitual's
    // eight blue halos high over the room washed Obanak blue, where the
    // recording has his face lit by the altar's fire.
    auto add = [&](const LightSource &l, float level) {
        if (level <= 0 || l.specialLit)
            return;
        const Vec3f dv{l.position[0] - at[0], l.position[1] - at[1], l.position[2] - at[2]};
        const float straight = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        const float dist = lightDistance(l, at);
        if (dist >= l.radius || straight <= 0)
            return;
        if (_play && _play->wallBetween(at, l.position))
            return;
        const float amount = lightFalloff(dist / l.radius) * level;
        sum += amount;
        for (size_t k = 0; k < 3; ++k) {
            total[k] += l.color[k] * amount;
            toward[k] += dv[k] / straight * amount;
        }
    };
    for (const LightSource &l : _lights.unique)
        add(l, levelOf(l));
    for (const LightSource &l : _dynamic)
        add(l, 1);
    // At most the texture's own colour where the light falls full on it,
    // as the lightmaps are: ambient and directed together under kMaxLight.
    // Each capped alone, they reached twice it, washing characters' faces
    // under bright lights out to pale (the opening's Terrell, against a
    // recording of the original). UE1 lights a mesh's vertices by which way
    // they face each light; with one direction to give, light from all
    // round becomes ambient and light from one side stays directed (how
    // far apart they are: 1 less the length of their summed directions over
    // their sum). Half of it all ambient lit CM10_KiraL1A's guards, under
    // two lamps over the door, as brightly in front as on top, where the
    // recording has them dark against it.
    const float len = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    const float spread = sum > 0 ? std::clamp(1 - len / sum, 0.0f, 1.0f) : 1.0f;
    for (size_t k = 0; k < 3; ++k) {
        light.ambient[k] = std::min(kMaxLight, 0.15f + 0.5f * spread * total[k]);
        light.directed[k] = std::min((1 - 0.3f * spread) * total[k], std::max(0.0f, kMaxLight - light.ambient[k]));
    }
    light.direction = len > 0 ? toWorld(Vec3f{toward[0] / len, toward[1] / len, toward[2] / len}) : gfx::Vec3{0, 0, 1};
    return light;
}

// The world as seen: the level's surfaces, the volume lights over them,
// the movers and the actors (or, not playing, the placed meshes and
// sprites), from `eye` looking along `forward`.
// How far a panning surface's texture has moved, in texels, as Render.dll
// moves it (0x10b228f5): auto-panned 35 texels a second at its zone's
bool LevelView::seen(const Batch &b) const {
    return (b.polyFlags & polyflag::kForceRender) || (b.zones & _zonesSeen);
}

// TexUPanSpeed and TexVPanSpeed (counted in 1/256 texels, coming round
// every 1024), and a wavy one swaying 4 cos 2.3t + 8 sin t across and
// 4 sin 2.3t + 8 cos t down; t the level's time.
std::array<float, 2> LevelView::surfacePan(const Batch &b) const {
    const float t = _play ? _play->level().time() : _lightTime;
    int zone = 0;
    if (b.zones != ~uint64_t(0) && b.zones != 0)
        while (!(b.zones >> zone & 1))
            ++zone;
    const std::array<float, 2> speed =
        size_t(zone) < _lights.zonePan.size() ? _lights.zonePan[size_t(zone)] : std::array<float, 2>{1, 1};
    auto pan = [&](float rate) { return float(int32_t(t * rate * 35 * 256) & 0x3ffff) / 256; };
    std::array<float, 2> out{};
    if (b.polyFlags & polyflag::kAutoUPan)
        out[0] = pan(speed[0]);
    if (b.polyFlags & polyflag::kAutoVPan)
        out[1] = pan(speed[1]);
    if (b.polyFlags & polyflag::kWavy) {
        out[0] += 4 * std::cos(2.3f * t) + 8 * std::sin(t);
        out[1] += 4 * std::sin(2.3f * t) + 8 * std::cos(t);
    }
    return out;
}

void LevelView::drawWorld(const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &eye) {
    drawBatches(levelBatches(), _mesh, gfx::identity(), true);
    drawSurfaceSwaps();
    drawVolumes(eye);
    for (size_t i = 0; i < _movers.size(); ++i) {
        const MoverDraw &m = _movers[i];
        VmObject *live = _play && i < _moverActors.size() ? _moverActors[i] : nullptr;
        LevelPlay::Look look;
        // Lit where it is now, by the lights as they are now.
        Vec3f at;
        gfx::Mat4 place;
        if (live && _play->look(live, look)) {
            at = look.location;
            place = moverPlacement(m.mover, look.location, look.rotation);
        } else {
            place = moverTransform(m);
            at = Vec3f{place[12], -place[13], place[14]};
        }
        // As Render.dll lights a mover's surfaces: a bDynamicLightMover by
        // the lights permeating its leaf (each in front of the surface, no
        // shadows) and the dynamic lights, where it is now; any other by
        // the lightmaps it was built with, which go with it as it moves
        // (Reluva: its vertices' light worked out when the level was read,
        // from every light reaching them, without shadows).
        const bool baked = !m.mover.dynamicLight;
        const MeshLight light = baked ? MeshLight{} : dynamicMoverLight(at, m.reach);
        drawBatches(m.batches, _moverMesh, place, false, false, baked ? nullptr : &light);
        if (inFogZone(at))
            volumesOver(m.batches, _moverMesh, place, gfx::Cull::kBack, false);
    }
    if (_play) {
        drawLiveActors(forward, right);
    } else {
        for (const MeshDraw &d : _meshActors) {
            const Mesh &mesh = _meshes[d.mesh];
            const MeshAnimSeq *anim = d.anim >= 0 ? &mesh.anims[size_t(d.anim)] : nullptr;
            drawMesh(*_renderer, _gpuMeshes[d.mesh], d.placement, poseAt(mesh, anim, _animTime, d.startFraction),
                     _textured ? d.textures : std::vector<gfx::TextureId>{}, d.light, kExposure);
        }
        drawSprites(_sprites, forward, right);
    }
}

// Mirrors (surfaces flagged Mirrored: Sisko's desk in Ops, the glass
// panes of M01_SiskoL1B): as the original's OccludeBsp does, each seen
// from its front shows the world reflected in its plane, mirrors in it
// too, up to three reflections deep. Each is drawn where the mirror is
// (the stencil raised a level there), nothing behind its plane (the
// projection's near plane moved onto it), its actors, movers and
// sprites too (not their shadows or coronas); then the mark lowered again
// and the depth cleared, for the view it's in to draw its own world over
// it: the mirror's own texture if it's translucent or modulated, else only
// its depth, and whatever stands before it. Called before the view's own
// world, with that view (`level` reflections deep, through `through`).
void LevelView::drawMirrors(const gfx::Mat4 &proj, const gfx::Mat4 &view, const gfx::Mat4 &viewProj,
                            const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, int level,
                            const Batch *through) {
    if (!_renderer->hasStencil() || level >= 3)
        return;
    int drawn = 0;
    for (const Batch &m : levelBatches()) {
        if (!(m.polyFlags & polyflag::kMirrored) || &m == through || !seen(m) || drawn >= 4)
            continue;
        const gfx::Vec3 n = m.mirrorNormal;
        const float side = n[0] * eye[0] + n[1] * eye[1] + n[2] * eye[2] - m.mirrorD;
        if (side <= 0) // seen from behind
            continue;
        ++drawn;
        // Where the mirror is: marked a level up.
        gfx::DrawCall mark;
        mark.mesh = _mesh;
        mark.firstIndex = m.firstIndex;
        mark.indexCount = m.indexCount;
        mark.state.src = gfx::Blend::kZero;
        mark.state.dst = gfx::Blend::kOne;
        mark.state.depthWrite = false;
        mark.state.cull = gfx::Cull::kNone;
        _renderer->setStencilLevel(uint8_t(level + 1));
        _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kLevelMark);
        _renderer->draw(mark);
        // The world reflected in the plane: x - 2 (n.x - d) n.
        gfx::Mat4 reflect = gfx::identity();
        for (size_t c = 0; c < 3; ++c)
            for (size_t r = 0; r < 3; ++r)
                reflect[c * 4 + r] -= 2 * n[r] * n[c];
        for (size_t r = 0; r < 3; ++r)
            reflect[12 + r] = 2 * m.mirrorD * n[r];
        const gfx::Mat4 mirrorView = gfx::multiply(view, reflect);
        // Its near plane on the mirror's: in the camera's space the plane
        // facing back, -n.x + d >= 0 kept (the reflection's side).
        const gfx::Vec3 on{n[0] * m.mirrorD, n[1] * m.mirrorD, n[2] * m.mirrorD};
        std::array<float, 4> c{};
        for (size_t r = 0; r < 3; ++r)
            c[r] = -(view[0 * 4 + r] * n[0] + view[1 * 4 + r] * n[1] + view[2 * 4 + r] * n[2]);
        const gfx::Vec3 onView = gfx::transformPoint(view, on);
        c[3] = -(c[0] * onView[0] + c[1] * onView[1] + c[2] * onView[2]);
        gfx::Mat4 p = proj;
        // Lengyel's oblique near plane, for depth 0-1.
        auto sgn = [](float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; };
        const std::array<float, 4> q{(sgn(c[0]) - p[8]) / p[0], (sgn(c[1]) - p[9]) / p[5], -1, (1 + p[10]) / p[14]};
        const float cq = c[0] * q[0] + c[1] * q[1] + c[2] * q[2] + c[3] * q[3];
        if (std::fabs(cq) > 1e-9f)
            for (size_t k = 0; k < 4; ++k)
                p[k * 4 + 2] = c[k] / cq;
        const gfx::Mat4 mirrorViewProj = gfx::multiply(p, mirrorView);
        const gfx::Vec3 mirrorEye = gfx::transformPoint(reflect, eye);
        auto turn = [&](const gfx::Vec3 &v) { // a direction reflected
            const float k = 2 * (n[0] * v[0] + n[1] * v[1] + n[2] * v[2]);
            return gfx::Vec3{v[0] - k * n[0], v[1] - k * n[1], v[2] - k * n[2]};
        };
        const gfx::Vec3 mirrorForward = turn(forward);
        const gfx::Vec3 mirrorRight = turn(right);
        _renderer->setView(mirrorViewProj, mirrorEye);
        _renderer->setMirrored((level + 1) % 2 == 1);
        const int saved = _mirrorLevel;
        const Batch *savedThrough = _mirrorThrough;
        _mirrorLevel = level + 1;
        _mirrorThrough = &m;
        // The mirrors seen in it first, then its world, only where it is.
        drawMirrors(proj, mirrorView, mirrorViewProj, mirrorEye, mirrorForward, mirrorRight, level + 1, &m);
        _renderer->setStencilLevel(uint8_t(level + 1));
        _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kLevelInside);
        drawWorld(mirrorForward, mirrorRight, mirrorEye);
        _mirrorLevel = saved;
        _mirrorThrough = savedThrough;
        // Back to the view it's in: the mark lowered where the mirror is,
        // whatever's in front of it, and the depth forgotten.
        _renderer->setMirrored(level % 2 == 1);
        _renderer->setView(viewProj, eye);
        mark.state.depthTest = false;
        _renderer->setStencilLevel(uint8_t(level + 1));
        _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kUnmark);
        _renderer->draw(mark);
        _renderer->clearDepth();
        if (level > 0) {
            _renderer->setStencilLevel(uint8_t(level));
            _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kLevelInside);
        } else {
            _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kNone);
        }
    }
}

// The mover's brushes have no lightmaps of their own (their models' are
// stale, pointing at no surface), so its vertices are lit as the level's
// surfaces are, where it is now: the lights reaching its brush (at most
// six, the brightest there, a light smaller than the brush counted for
// the share of it it reaches), each colour x (1 - 3 f^2 + 2 f^3) x the
// facing (a cylinder light 1 - f^2 across), with its zone's ambient
// light, at most 1; without the lights' shadows. Reluva's reading: how
// the original's moving-brush tracker lights them isn't known.
MeshLight LevelView::dynamicMoverLight(const Vec3f &at, float reach) const {
    MeshLight light;
    struct Candidate {
        const LightSource *l;
        float level;
        float importance;
    };
    std::vector<Candidate> found;
    auto consider = [&](const LightSource &l, float level) {
        if (level <= 0 || l.specialLit || l.radius <= 0)
            return;
        const float dist = lightDistance(l, at);
        if (dist >= l.radius + reach)
            return;
        const float near = std::max(0.0f, dist - reach);
        const float share = reach > l.radius ? l.radius / reach : 1.0f;
        found.push_back({&l, level, float(l.brightness) * level * (1 - near / l.radius) * share});
    };
    const int leaf = _zones.leafAt(at);
    if (leaf >= 0 && size_t(leaf) < _leafLights.size() && _leafLights[size_t(leaf)] >= 0)
        for (size_t i = size_t(_leafLights[size_t(leaf)]); i < _lights.sources.size() && _lights.sources[i].radius > 0;
             ++i)
            consider(_lights.sources[i], levelOf(_lights.sources[i]));
    for (const LightSource &l : _dynamic)
        consider(l, 1);
    // The renderer takes six: the most of them.
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate &a, const Candidate &b) { return a.importance > b.importance; });
    for (size_t i = 0; i < found.size() && i < size_t(gfx::kMaxVertexLights); ++i) {
        gfx::VertexLight v;
        v.position = toWorld(found[i].l->position);
        v.radius = found[i].l->radius;
        v.cylinder = found[i].l->cylinder;
        for (size_t k = 0; k < 3; ++k)
            v.color[k] = found[i].l->color[k] * found[i].level;
        light.lights.push_back(v);
    }
    const int zone = _zones.zoneAt(at);
    const Vec3f ambient =
        zone >= 0 && size_t(zone) < _lights.zoneAmbient.size() ? _lights.zoneAmbient[size_t(zone)] : Vec3f{};
    light.ambient = {ambient[0], ambient[1], ambient[2]};
    light.directed = {0, 0, 0};
    light.perVertex = true;
    return light;
}

MeshLight LevelView::moverLightAt(const Vec3f &at, float reach, size_t *reaching) const {
    MeshLight light;
    struct Candidate {
        const LightSource *l;
        float level;
        float importance;
    };
    std::vector<Candidate> found;
    auto consider = [&](const LightSource &l, float level) {
        if (level <= 0 || l.specialLit || l.radius <= 0)
            return;
        const float dist = lightDistance(l, at);
        if (dist >= l.radius + reach)
            return;
        const float near = std::max(0.0f, dist - reach);
        // On a brush larger than the light's reach, only so much of it is
        // lit: CM_ArdPrison's docked Defiant (Mover10, 5000 across) was
        // otherwise given six bright lamps of radius 50 to 100 by its
        // pivot and left black, not the station's lights of 4000 round it.
        const float share = reach > l.radius ? l.radius / reach : 1.0f;
        found.push_back({&l, level, float(l.brightness) * level * (1 - near / l.radius) * share});
    };
    for (const LightSource &l : _lights.unique)
        consider(l, levelOf(l));
    for (const LightSource &l : _dynamic)
        consider(l, 1);
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate &a, const Candidate &b) { return a.importance > b.importance; });
    if (reaching)
        *reaching = found.size();
    for (size_t i = 0; i < found.size() && i < size_t(gfx::kMaxVertexLights); ++i) {
        gfx::VertexLight v;
        v.position = toWorld(found[i].l->position);
        v.radius = found[i].l->radius;
        v.cylinder = found[i].l->cylinder;
        for (size_t k = 0; k < 3; ++k)
            v.color[k] = found[i].l->color[k] * found[i].level;
        light.lights.push_back(v);
    }
    const int zone = _zones.zoneAt(at);
    const Vec3f ambient =
        zone >= 0 && size_t(zone) < _lights.zoneAmbient.size() ? _lights.zoneAmbient[size_t(zone)] : Vec3f{};
    light.ambient = {ambient[0], ambient[1], ambient[2]};
    light.directed = {0, 0, 0};
    light.perVertex = true;
    return light;
}

// An actor's mesh in its style; a MultiTexture one (bMultiTextureOn) twice,
// as the original's DrawMesh does: its own skins in MultiTextureOneStyle
// with MultiTextureOneScaleGlow, then over them MultiTextureTwo (else its
// skins again) in MultiTextureTwoStyle with MultiTextureTwoScaleGlow,
// environment mapped if bMultiTextureEnvMap, pushed out along its normals
// by MultiTextureTwoFatness / 16 - 8 world units. A blended pass's glow is
// its alpha; an unlit one's (bMultiTexture...Unlit) its colour, times
// ColorFilter; a lit one keeps that much of its light.
void LevelView::drawActorMesh(const GpuMesh &gpu, const gfx::Mat4 &placement, const MeshPose &pose,
                              const std::vector<gfx::TextureId> &skins, const MeshLight &light,
                              const LevelPlay::Look &look) {
    const LevelPlay::Look::MultiTexture &m = look.multi;
    MeshFog fog = _fog;
    fog.volumes = inFogZone(look.location);
    if (!m.on) {
        drawMesh(*_renderer, gpu, placement, pose, skins, light, kExposure, look.style, &fog);
        return;
    }
    const gfx::Vec3 filter{m.colorFilter[0], m.colorFilter[1], m.colorFilter[2]};
    MeshPass one;
    one.glow = m.oneGlow;
    one.unlit = m.oneUnlit;
    one.unlitColor = filter;
    drawMesh(*_renderer, gpu, placement, pose, skins, light, kExposure, m.oneStyle, &fog, &one);
    MeshPass two;
    two.second = true;
    two.glow = m.twoGlow;
    two.unlit = m.twoUnlit;
    two.unlitColor = filter;
    two.texture = m.two && _textured ? textureOf(m.two).first : 0;
    two.envMap = m.envMap;
    // World units to the mesh's own (its placement's scale).
    float scale = 0;
    for (int c = 0; c < 3; ++c)
        scale += std::sqrt(placement[size_t(c * 4)] * placement[size_t(c * 4)] +
                           placement[size_t(c * 4 + 1)] * placement[size_t(c * 4 + 1)] +
                           placement[size_t(c * 4 + 2)] * placement[size_t(c * 4 + 2)]) /
                 3;
    if (scale > 0)
        two.fatness = (float(m.twoFatness) / 16 - 8) / scale;
    MeshFog over = fog; // the volume lights once, over the first pass
    over.volumes = false;
    drawMesh(*_renderer, gpu, placement, pose, skins, light, kExposure, m.twoStyle, &over, &two);
}

// An actor's light as the original's renderer works it out (Render.dll's
// actor lighting, read for what it does): the lights reaching it, most
// important first ((1 - distance / radius) x brightness), down to an
// eighth of the first's, at most three that stay where they are and
// others while there are fewer than three, each seen from it (no wall
// between; lights that move needn't be). Its ambient light is its zone's
// ambient through the engine's colour curve plus AmbientGlow / 255 (255:
// pulsing, 0.25 + 0.2 sin 8t). Each vertex then adds 1.4 x ScaleGlow x
// its lights (gfx::DrawCall::vertexLights), at most 1.
void LevelView::actorLights(const Vec3f &at, const LevelPlay::Look &look, MeshLight &light, const void *who) const {
    // As Render.dll's light manager for an actor (SetupForActor): the
    // candidates are the lights it was lit by last frame (kept to fade
    // out), then the level's lights permeating its BSP leaf, then the
    // lights that move. Each is kept if its radius passes the straight
    // distance (a cylinder light's too) and weighed (1 - distance /
    // radius) x LightBrightness. In that order of weight, at most three
    // that stay put (bStatic), and lights that don't while fewer than
    // three are taken in all; under an eighth of the first taken's weight,
    // passed over. A light that doesn't move must see the actor (a line
    // through the BSP; the engine looks again 1 frame in 16). Each fades
    // in as it's taken and out as it isn't, a full fade in a third of a
    // second (768 of 255 a second), lighting it as it fades out.
    struct Candidate {
        const LightSource *l;
        float level;
        float importance;
        bool isStatic;
        bool moves;
        uint64_t key;
    };
    std::vector<Candidate> found;
    std::set<uint64_t> listed;
    auto consider = [&](const LightSource &l, float level, bool moves, uint64_t key) {
        if (level <= 0 || l.specialLit || l.radius <= 0 || !listed.insert(key).second)
            return;
        const float dx = l.position[0] - at[0];
        const float dy = l.position[1] - at[1];
        const float dz = l.position[2] - at[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist >= l.radius)
            return;
        found.push_back({&l, level, (1 - dist / l.radius) * float(l.brightness), l.isStatic, moves, key});
    };
    ActorFade *fade = nullptr;
    bool fresh = true;
    float step = 1;
    if (who) {
        ActorFade &f = _actorFades[who];
        fresh = f.time < 0;
        step = fresh ? 1 : std::clamp((_lightTime - f.time) * 768 / 255, 0.0f, 1.0f);
        f.time = _lightTime;
        fade = &f;
    }
    // The level's lights by their actor (ref); those that move by theirs.
    const int leaf = _zones.leafAt(at);
    std::map<int32_t, const LightSource *> byRef;
    if (leaf >= 0 && size_t(leaf) < _leafLights.size() && _leafLights[size_t(leaf)] >= 0)
        for (size_t i = size_t(_leafLights[size_t(leaf)]); i < _lights.sources.size() && _lights.sources[i].ref; ++i)
            byRef[_lights.sources[i].ref] = &_lights.sources[i];
    if (_uniqueByRef.empty())
        for (const LightSource &u : _lights.unique)
            if (u.ref)
                _uniqueByRef.emplace(u.ref, &u);
    auto level = [&](int32_t ref, uint64_t key) {
        if (auto it = _uniqueByRef.find(ref); it != _uniqueByRef.end())
            consider(*it->second, levelOf(*it->second), false, key);
    };
    if (fade)
        for (const auto &[key, f] : fade->lights) {
            (void)f;
            if (!(key >> 63)) // a moving light is found below if still near
                level(int32_t(key), key);
        }
    for (const auto &[ref, l] : byRef) {
        (void)l;
        level(ref, uint64_t(ref));
    }
    for (const LightSource &l : _dynamic)
        consider(l, 1, true, (uint64_t(1) << 63) | uint64_t(uintptr_t(l.key)));
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate &a, const Candidate &b) { return a.importance > b.importance; });
    int total = 0;
    int still = 0;
    float threshold = -1;
    std::map<uint64_t, float> next;
    for (const Candidate &c : look.unlit ? std::vector<Candidate>{} : found) {
        float was = fade && fade->lights.count(c.key) ? fade->lights[c.key] : 0;
        bool taken = false;
        if (c.importance >= threshold && (c.isStatic ? still : total) < 3) {
            // A distance fog light (LightEffect 20) needs no line to the
            // actor (no light in the game is one).
            const bool sees = c.moves || c.l->effect == 20 || !_play || !_play->wallBetween(at, c.l->position);
            if (sees) {
                taken = true;
                ++total;
                if (c.isStatic)
                    ++still;
                if (threshold < 0)
                    threshold = c.importance / 8;
            }
        }
        const float now = taken ? (fresh ? 1.0f : std::min(1.0f, was + step)) : std::max(0.0f, was - step);
        if (now <= 0)
            continue;
        if (next.size() < 16)
            next[c.key] = now;
        gfx::VertexLight v;
        // Shaded as a point light, a cylinder light too (Render.dll's actor
        // lighting has no cylinder case).
        v.position = toWorld(c.l->position);
        v.radius = c.l->radius;
        // The lightmaps' colours are doubled (lightColor); actors' aren't.
        for (size_t k = 0; k < 3; ++k)
            v.color[k] = c.l->color[k] * c.level / 2 * (who ? now : 1.0f);
        light.lights.push_back(v);
    }
    if (fade)
        fade->lights = std::move(next);
    const int zone = _zones.zoneAt(at);
    const Vec3f zoneAmbient =
        zone >= 0 && size_t(zone) < _lights.zoneActorAmbient.size() ? _lights.zoneActorAmbient[size_t(zone)] : Vec3f{};
    const float glow =
        look.ambientGlow == 255 ? 0.25f + 0.2f * std::sin(8 * _lightTime) : float(look.ambientGlow) / 255;
    // bUnlit: no lights, half of them all round instead (1.4 x ScaleGlow
    // x 0.5).
    const float unlit = look.unlit ? 0.7f * look.scaleGlow : 0;
    if (look.unlit)
        light.lights.clear();
    for (size_t k = 0; k < 3; ++k) {
        light.ambient[k] = std::min(1.0f, zoneAmbient[k] + glow + unlit);
        light.directed[k] = 0;
    }
    light.perVertex = true;
    light.scale = 1.4f * look.scaleGlow;
    light.specular = look.specular;
}

// --- Playing --------------------------------------------------------------------------------

Error LevelView::saveGame(const std::string &name) {
    if (!_play)
        return Error::fail("not playing");
    return _play->level().saveGame(name);
}

void LevelView::travel(const std::string &url, bool items) {
    const std::string lower = fs::toLower(url);
    if (items && _play)
        _play->level().travelItems() = true;
    // What goes with the player, as the engine has it: its played time
    // always; on "?RESTART" what it came into this level with, else with
    // ClientTravel's bItems its pawn and inventory (the travel text), else
    // nothing. (A saved game loaded has its own.)
    _pendingTravel.clear();
    _pendingPlayedTime = -1;
    if (_play && lower.find("?load=") == std::string::npos) {
        ScriptLevel &level = _play->level();
        if (VmObject *p = _play->player())
            if (const Value *t = p->find(_play->vm(), "GamePlayedTime"))
                _pendingPlayedTime = t->asFloat();
        if (lower.find("?restart") != std::string::npos)
            _pendingTravel = level.incomingTravel();
        else if (level.travelItems())
            _pendingTravel = level.exportTravel();
    }
    // "?RESTART": the map again, from where it began.
    if (lower.rfind("?restart", 0) == 0) {
        const std::string portal = _portal;
        startPlay(portal);
        return;
    }
    // "?LOAD=Save1.dss": a saved game, on its own map.
    if (const size_t load = lower.find("?load="); load != std::string::npos) {
        std::string name = url.substr(load + 6);
        name = name.substr(0, name.find('?'));
        const std::filesystem::path file = _ctx.saveFolder / name;
        ScriptLevel::SaveInfo info;
        if (_ctx.saveFolder.empty() || ScriptLevel::readSaveInfo(file, info)) {
            // None there: the game's word for it (its quick load).
            if (_play)
                _play->hudEvent(1010);
            return;
        }
        // The original's: the save is the level itself, opened as a map.
        if (info.byGame) {
            const std::string level = _ctx.packages.addLater(file);
            if (std::find(_names.begin(), _names.end(), level) == _names.end())
                _names.push_back(level);
            _startOnLoad = {true, {}, {}};
            if (!selectMap(level)) {
                _startOnLoad = {};
                stopPlay();
                _playError = "load " + name + ": doesn't open";
                return;
            }
            startPlay({});
            return;
        }
        _startOnLoad = {true, {}, file};
        if (!selectMap(info.map)) {
            _startOnLoad = {};
            stopPlay();
            _playError = "load " + name + ": no map " + info.map;
            return;
        }
        startPlay({}, file);
        return;
    }
    std::string where = url.substr(0, url.find('?'));
    const size_t cut = where.find_first_of("/#");
    const std::string portal = cut == std::string::npos ? std::string() : where.substr(cut + 1);
    std::string map = where.substr(0, cut);
    if (const size_t dot = map.rfind('.'); dot != std::string::npos)
        map = map.substr(0, dot);
    _startOnLoad = {true, portal, {}};
    if (!selectMap(map)) {
        _startOnLoad = {};
        stopPlay();
        _playError = "travel to " + url + ": no map " + map;
        return;
    }
    startPlay(portal);
}

// A level started to be played, what its start changed in `changes`;
// none, and why in `error`, if it couldn't start.
std::unique_ptr<LevelPlay> LevelView::startForPlay(const Package &map, LevelStart &changes, std::string &error) {
    auto play = std::make_unique<LevelPlay>(_ctx.packages, map);
    play->level().incomingTravel() = std::exchange(_pendingTravel, std::string());
    play->level().carriedPlayedTime() = std::exchange(_pendingPlayedTime, -1.0f);
    const auto t0 = std::chrono::steady_clock::now();
    if (Error err =
            play->start(_startOnLoad.portal, _startOnLoad.saved, _ctx.saveFolder, _ctx.configFolder, &changes)) {
        error = err.message;
        return nullptr;
    }
    changes.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return play;
}

void LevelView::startPlay(const std::string &portal, const std::filesystem::path &saved) {
    stopPlay();
    const Package *pkg = _names.empty() ? nullptr : _ctx.packages.get(_names[size_t(_index)]);
    std::unique_ptr<LevelPlay> play = std::move(_started);
    const std::string startedError = std::exchange(_startedError, std::string());
    const bool startedOnLoad = std::exchange(_startOnLoad, {}).wanted;
    if (!pkg || !_error.empty())
        return;
    if (startedOnLoad && !play && !startedError.empty()) {
        _playError = startedError;
        return;
    }
    if (!play) {
        play = std::make_unique<LevelPlay>(_ctx.packages, *pkg);
        play->level().incomingTravel() = std::exchange(_pendingTravel, std::string());
        play->level().carriedPlayedTime() = std::exchange(_pendingPlayedTime, -1.0f);
        if (Error err = play->start(portal, saved, _ctx.saveFolder, _ctx.configFolder)) {
            _playError = err.message;
            return;
        }
    }
    if (!play->player()) {
        _playError = "no player logged in: " + play->loginError();
        return;
    }
    // Each mover's live actor, by its path in the map.
    std::map<std::string, VmObject *> byPath;
    for (VmObject *a : play->level().actors())
        if (a->package == pkg && a->exportIndex >= 0)
            byPath[pkg->path(a->exportIndex + 1)] = a;
    _moverActors.clear();
    for (const MoverDraw &m : _movers) {
        auto it = byPath.find(m.mover.name);
        _moverActors.push_back(it == byPath.end() ? nullptr : it->second);
    }
    _playError.clear();
    _portal = portal;
    _playClock = 0;
    _held[kFast] = false;
    _play = std::move(play);
}

void LevelView::stopPlay() {
    _shadows.clear(); // keyed by the play's actors
    _freeCam = false;
    if (_renderer)
        for (const GpuMesh &g : _posedMeshes)
            if (g.mesh)
                _renderer->destroyMesh(g.mesh);
    _posedMeshes.clear();
    if (_audio)
        _audio->reset();
    _play.reset();
    _moverActors.clear();
    // The live actors' meshes stay uploaded (in meshes_), for playing again.
}

void LevelView::updatePlay(float dt) {
    ScriptLevel::Controls &c = _play->controls();
    if (_freeCam) { // the keys fly the camera: the player stands
        c = {};
        _play->advance(dt);
        _playClock += dt;
        _view = _play->view();
        return;
    }
    c.forward = float(_held[kForward] - _held[kBack]);
    c.strafe = float(_held[kRight] - _held[kLeft]);
    c.turn = float(_held[kTurnRight] - _held[kTurnLeft]);
    c.lookUp = float(_held[kLookUp] - _held[kLookDown]);
    c.mouseX += _mouseDx;
    c.mouseY += _mouseDy;
    c.jump = _held[kUp];
    c.duck = _held[kDown]; // C, as the game binds it (Duck)
    c.fire = _held[kFire];
    _mouseDx = _mouseDy = 0;
    _play->advance(dt);
    _playClock += dt;
    // The camera where the game puts it.
    _view = _play->view();
    _pos = toWorld(_view.location);
    // The way it looks, from its rotation's direction: the game's camera
    // writes the same direction with the yaw turned round and the pitch
    // past straight up when it's ahead of the player (CTVE_Camera), which
    // taken as yaw and pitch alone looks straight down.
    const Vec3f f = rotationAxes(_view.rotation)[0];
    _yaw = std::atan2(-f[1], f[0]); // negated with Y
    _pitch = std::clamp(std::asin(std::clamp(f[2], -1.0f, 1.0f)), -1.55f, 1.55f);
    _fov = _view.fov;
}

int LevelView::liveMesh(const ObjectRef &ref, const std::string &anim) {
    auto it = _liveMeshes.find({ref.package, ref.exportIndex});
    if (it == _liveMeshes.end()) {
        Mesh mesh;
        GpuMesh gpu;
        int at = -1;
        if (!readMesh(*ref.package, ref.exportIndex, mesh) &&
            !uploadMesh(*_renderer, mesh, gpu, _ctx.meshTurn, _ctx.meshYawQuarters)) {
            at = int(_meshes.size());
            _meshes.push_back(std::move(mesh));
            _gpuMeshes.push_back(gpu);
        }
        it = _liveMeshes.emplace(std::make_pair(ref.package, ref.exportIndex), at).first;
    }
    const int at = it->second;
    if (at < 0 || anim.empty())
        return at;
    // A character's animation from the shared sets, the first time it plays
    // one its mesh doesn't have: borrowed, and the mesh uploaded again.
    Mesh &mesh = _meshes[size_t(at)];
    if (mesh.bones && !mesh.findAnim(anim) &&
        borrowNamedAnimation(*ref.package, ref.exportIndex, _ctx.sets(), anim, mesh)) {
        GpuMesh gpu;
        if (!uploadMesh(*_renderer, mesh, gpu, _ctx.meshTurn, _ctx.meshYawQuarters)) {
            if (_gpuMeshes[size_t(at)].mesh)
                _renderer->destroyMesh(_gpuMeshes[size_t(at)].mesh);
            _gpuMeshes[size_t(at)] = gpu;
        }
    }
    return at;
}

// The level's actors as they are now: meshes posed where their animation
// is (AnimFrame), and sprites.
void LevelView::drawLiveActors(const gfx::Vec3 &forward, const gfx::Vec3 &right) {
    std::vector<SpriteDraw> sprites;
    // Only those that may be seen: not hidden, in a zone seen (the rest
    // aren't drawn, so their scripts aren't asked to get them ready).
    for (VmObject *a : _play->level().actors())
        if (_play->mayBeSeen(a, _zonesSeen))
            drawLiveActor(a, sprites);
    addDecals(sprites);
    if (_mirrorLevel == 0) // not in a mirror: their fading goes by the camera's view
        addCoronas(sprites, forward, _coronaDt);
    drawSprites(sprites, forward, right);
}

// The player's screen flash, over the scene (not the HUD): every pixel
// times its FlashScale, plus its FlashFog, as UE1's renderer does. The
// scripts work both out each tick (PlayerPawn.ViewFlash): the head's zone's
// ViewFlash and ViewFog (a water zone's blue-green: The Fallen's
// CTV_H20Zone, ViewFog 0.13 0.20 0.17), and the flashes of being hurt or
// picking things up (ClientFlash). Drawn as one quad before the camera,
// its colour the fog, its alpha the scale.
void LevelView::drawScreenFlash(const gfx::Vec3 &forward, const gfx::Vec3 &right) {
    VmObject *p = _play ? _play->player() : nullptr;
    if (!p || !_spriteQuad)
        return;
    Vm &vm = _play->vm();
    const Value *scaleV = p->find(vm, "FlashScale");
    const Value *fogV = p->find(vm, "FlashFog");
    if (!scaleV || !fogV)
        return;
    const auto scale = Vm::vectorOf(*scaleV);
    const auto fog = Vm::vectorOf(*fogV);
    const float s = std::clamp(scale[0], 0.0f, 4.0f);
    const float f[3] = {std::clamp(fog[0], 0.0f, 1.0f), std::clamp(fog[1], 0.0f, 1.0f), std::clamp(fog[2], 0.0f, 1.0f)};
    if (std::fabs(s - 1) < 0.01f && f[0] + f[1] + f[2] < 0.01f)
        return;
    const gfx::Vec3 up{right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                       right[0] * forward[1] - right[1] * forward[0]};
    const float d = 16;
    const float size = 2 * d * std::tan(std::clamp(_fov, 1.0f, 170.0f) * kPi / 360) * 1.5f;
    gfx::DrawCall call;
    call.mesh = _spriteQuad;
    call.indexCount = 6;
    call.model = gfx::identity();
    for (size_t k = 0; k < 3; ++k) {
        call.model[k] = right[k] * size;
        call.model[4 + k] = up[k] * size;
        call.model[8 + k] = -forward[k];
        call.model[12 + k] = _pos[k] + forward[k] * d;
    }
    call.texture = 0;
    call.rgbSource = call.alphaSource = gfx::ColorSource::kConstant;
    if (s <= 1) { // dst x scale + fog
        call.color = {f[0], f[1], f[2], s};
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kSrcAlpha;
    } else { // brighter than it is: dst x 1 + dst x (scale - 1), then the fog
        call.color = {s - 1, s - 1, s - 1, 1};
        call.state.src = gfx::Blend::kDstColor;
        call.state.dst = gfx::Blend::kOne;
    }
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    _renderer->draw(call);
    if (s > 1 && f[0] + f[1] + f[2] >= 0.01f) {
        call.color = {f[0], f[1], f[2], 1};
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
        _renderer->draw(call);
    }
}

// Coronas, as Render.dll draws them: of the lights permeating the
// leaf the camera is in (the level model's leaf list) and the moving
// lights reaching it, each with bCorona and a Skin, at most 32, those with
// a clear line from the light to the camera; each frame every corona's
// glow drops by the frame's time and a seen one's rises by twice that (in
// over a second, out over a second), gone at 0. Drawn as its Skin,
// translucent, over everything, centred where the light is on the
// screen, the frame's width x DrawScale x 0.8 pixels across, in its
// light's hue made whiter by its saturation, times the glow (its
// brightness not used).
void LevelView::addCoronas(std::vector<SpriteDraw> &sprites, const gfx::Vec3 &forward, float dt) {
    _play->coronas(_coronaScratch);
    const Vec3f eye{_pos[0], -_pos[1], _pos[2]};
    const float tanHalf = std::tan(std::clamp(_fov, 1.0f, 170.0f) * kPi / 360);
    // The level's lights reaching the camera's leaf.
    std::set<int32_t> permeating;
    if (const int leaf = _zones.leafAt(eye);
        leaf >= 0 && size_t(leaf) < _leafLights.size() && _leafLights[size_t(leaf)] >= 0)
        for (size_t i = size_t(_leafLights[size_t(leaf)]); i < _lights.sources.size() && _lights.sources[i].ref; ++i)
            permeating.insert(_lights.sources[i].ref);
    // Every glow down by the frame's time.
    for (auto it = _coronaFade.begin(); it != _coronaFade.end();) {
        it->second -= dt;
        it = it->second < 0 ? _coronaFade.erase(it) : std::next(it);
    }
    for (const LevelPlay::Corona &c : _coronaScratch) {
        const bool candidate = c.isStatic ? permeating.count(c.ref) != 0
                                          : std::sqrt((c.location[0] - eye[0]) * (c.location[0] - eye[0]) +
                                                      (c.location[1] - eye[1]) * (c.location[1] - eye[1]) +
                                                      (c.location[2] - eye[2]) * (c.location[2] - eye[2])) < c.radius;
        if (candidate && !_play->wallBetween(c.location, eye)) {
            auto it = _coronaFade.find(c.actor);
            if (it != _coronaFade.end())
                it->second = std::min(1.0f, it->second + 2 * dt);
            else if (_coronaFade.size() < 32)
                _coronaFade[c.actor] = std::min(1.0f, 2 * dt);
        }
    }
    for (const LevelPlay::Corona &c : _coronaScratch) {
        auto it = _coronaFade.find(c.actor);
        if (it == _coronaFade.end())
            continue;
        const float fade = it->second;
        const gfx::Vec3 at = toWorld(c.location);
        const float depth =
            (at[0] - _pos[0]) * forward[0] + (at[1] - _pos[1]) * forward[1] + (at[2] - _pos[2]) * forward[2];
        if (fade <= 0 || depth <= 1)
            continue;
        const auto [texture, size] = textureOf(c.texture);
        if (!texture)
            continue;
        // Its colour: the hue in three thirds, whitened by the saturation.
        const float h = float(c.hue);
        Vec3f col = c.hue < 86    ? Vec3f{(85 - h) / 85, h / 85, 0}
                    : c.hue < 171 ? Vec3f{0, (170 - h) / 85, (h - 85) / 85}
                                  : Vec3f{(h - 170) / 85, 0, (255 - h) / 84};
        const float white = float(c.saturation) / 255;
        for (float &x : col)
            x = (x + (1 - x) * white) * fade;
        const float pixels = float(kFrameW) * c.drawScale * 0.8f;
        const float world = pixels * depth * 2 * tanHalf / float(kFrameW);
        SpriteDraw d;
        d.texture = texture;
        d.at = at;
        d.width = d.height = world;
        d.style = kStyleTranslucent;
        d.color = {col[0], col[1], col[2]};
        d.onTop = true;
        sprites.push_back(d);
    }
}

// The Fallen's characters cast shadows of their own shape (actors with
// bProjectsShadows, from lights with bActorShadows: every Light by
// default): its renderer says so (GetRendererFlags bit 2), and its blob
// shadows (CTV_Shadow) step aside. As the original's renderer does it
// (Render.dll's PrepareProjectedShadow and DrawProjectedShadow, read for
// what they do; docs/engines/unreal/lighting.md): the mesh flattened from
// one light onto the floor under it, darkest close to its feet, its
// strength easing in and out as it moves between lights.
void LevelView::drawShadow(VmObject *a, const Vec3f &at) {
    if (_mirrorLevel > 0) // in a mirror: the stencil is the mirror's
        return;
    Vm &vm = _play->vm();
    const Value *casts = a->find(vm, "bProjectsShadows");
    if (!casts || !casts->asBool())
        return;
    auto number = [&](VmObject *o, const char *name, float fallback) {
        const Value *v = o->find(vm, name);
        return v ? v->asFloat() : fallback;
    };
    auto flag = [&](VmObject *o, const char *name) {
        const Value *v = o->find(vm, name);
        return v && v->asBool();
    };
    // A weapon held casts from its holder's light, faded by its ScaleGlow.
    VmObject *caster = a;
    Vec3f from = at;
    float glow = 1;
    if (vm.isA(a, "Weapon") && flag(a, "bHeldItem")) {
        const Value *owner = a->find(vm, "Owner");
        if (owner && owner->asObject()) {
            glow = number(a, "ScaleGlow", 1);
            caster = owner->asObject();
            if (const Value *loc = caster->find(vm, "Location")) {
                const auto l = Vm::vectorOf(*loc);
                from = {l[0], l[1], l[2]};
            }
        }
    }
    // One that fades with a blended overlay (a cloak) fades its shadow too.
    if (flag(caster, "bMultiTextureOn")) {
        const Value *style = caster->find(vm, "MultiTextureOneStyle");
        if (style && style->asInt() == 5) // STY_Blended
            glow = number(caster, "MultiTextureOneScaleGlow", 1);
    }
    const float height = std::max(1.0f, number(caster, "CollisionHeight", 40));
    const float radius = std::max(1.0f, number(caster, "CollisionRadius", 20));
    // The floor: within four times its half height straight down, not too
    // steep, and facing the camera.
    std::array<float, 3> hit{};
    std::array<float, 3> normal{};
    const std::array<float, 3> top{from[0], from[1], from[2]};
    const std::array<float, 3> down{from[0], from[1], from[2] - 4 * height};
    if (!_play->level().trace(nullptr, top, down, {0, 0, 0}, false, hit, normal) || normal[2] < 0.7f)
        return;
    const Vec3f eye{_pos[0], -_pos[1], _pos[2]};
    if ((hit[0] - eye[0]) * normal[0] + (hit[1] - eye[1]) * normal[1] + (hit[2] - eye[2]) * normal[2] > 0)
        return;
    // Fainter far off: 900 / its depth in the view, at most 3.5.
    const gfx::Vec3 forward{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 hw = toWorld(Vec3f{hit[0], hit[1], hit[2]});
    const float depth =
        (hw[0] - _pos[0]) * forward[0] + (hw[1] - _pos[1]) * forward[1] + (hw[2] - _pos[2]) * forward[2];
    if (depth <= 0)
        return;
    const float nearness = std::min(900 / depth, 3.5f);
    if (nearness < 0.05f)
        return;
    // The light: of those that cast actor shadows and reach the floor
    // there (walls between or not), the one giving the most (brightness
    // times 1 - distance / radius, less in a brighter zone), the one it had
    // kept unless another gives 0.02 more.
    const Vec3f floor{hit[0], hit[1], hit[2]};
    const int zone = _zones.zoneAt(floor);
    const float ambient = zone >= 0 && size_t(zone) < _lights.zoneAmbientBrightness.size()
                              ? float(_lights.zoneAmbientBrightness[size_t(zone)]) / 255
                              : 0;
    ShadowState &state = _shadows[caster];
    auto amountOf = [&](const LightSource &l) {
        const float dx = l.position[0] - floor[0];
        const float dy = l.position[1] - floor[1];
        const float dz = l.position[2] - floor[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!l.actorShadows || l.specialLit || dist >= l.radius)
            return 0.0f;
        return float(l.brightness) / 255 * levelOf(l) * (1 - dist / l.radius) / (1 + ambient);
    };
    int best = -1;
    float bestAmount = 0.01f;
    float keptAmount = 0;
    for (size_t i = 0; i < _lights.unique.size(); ++i) {
        const float amount = amountOf(_lights.unique[i]);
        if (int(i) == state.light)
            keptAmount = amount;
        if (amount > bestAmount) {
            bestAmount = amount;
            best = int(i);
        }
    }
    float amount = bestAmount;
    if (state.light >= 0 && best != state.light && keptAmount > 0 && bestAmount - keptAmount < 0.02f) {
        best = state.light;
        amount = keptAmount;
    }
    // Its strength eases towards that (1.3 a second), and when the light
    // changes it fades out before the new one fades in; after 3 s unseen,
    // or to or from a light that moves, it jumps.
    const float target = best >= 0 ? std::min(amount * nearness, 1.0f) : 0;
    const float now = _play->time();
    const float since = state.seen ? now - state.lastSeen : 1e9f;
    const float step = std::clamp(since * 1.3f, 0.0f, 1.0f);
    auto still = [&](int i) { return i < 0 || _lights.unique[size_t(i)].isStatic; };
    if (state.seen && since <= 3 && still(state.light) && still(best)) {
        if (best == state.light) {
            state.strength = std::min(state.strength + step, target);
        } else {
            state.strength = std::max(state.strength - step, 0.0f);
            if (state.strength < 0.05f)
                state.light = best;
        }
    } else {
        state.strength = target;
        state.light = best;
    }
    state.seen = true;
    state.lastSeen = now;
    if (state.light < 0 || state.strength < 0.05f)
        return;
    // The light at least twice the caster's size above it, and at least
    // half its distance across: a light low beside it casts no endless
    // shadow.
    const Vec3f n{normal[0], normal[1], normal[2]};
    Vec3f light = _lights.unique[size_t(state.light)].position;
    const Vec3f rel{light[0] - from[0], light[1] - from[1], light[2] - from[2]};
    const float above = rel[0] * n[0] + rel[1] * n[1] + rel[2] * n[2];
    const float across = std::sqrt(std::max(0.0f, rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2] - above * above));
    const float least = std::max(2 * std::max(height, radius), across * 0.5f);
    if (above < least)
        for (size_t k = 0; k < 3; ++k)
            light[k] += n[k] * (least - above);
    const float lx = light[0] - from[0];
    const float ly = light[1] - from[1];
    const float lz = light[2] - from[2];
    // Just off the floor (more far off), each vertex darkened by
    // min(strength x 50, 35) over how far past the caster it falls along
    // the light's ray, at most 0.8.
    const float lift = 0.1f + 0.005f * std::max(depth, 0.0f);
    const gfx::Vec3 nw = toWorld(n);
    const gfx::Vec3 p = toWorld(Vec3f{hit[0] + n[0] * lift, hit[1] + n[1] * lift, hit[2] + n[2] * lift});
    const float d = nw[0] * p[0] + nw[1] * p[1] + nw[2] * p[2];
    ShadowShade shade;
    shade.light = toWorld(light);
    shade.reach = std::sqrt(lx * lx + ly * ly + lz * lz);
    shade.strength = std::min(glow * state.strength * 50, 35.0f);
    shade.most = 0.8f;
    const gfx::Mat4 flatten = planarShadow(shade.light, nw, d);
    _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kOnce);
    drawMeshShadow(*_renderer, _lastMesh.gpu, gfx::multiply(flatten, _lastMesh.placement), _lastMesh.pose, shade);
    _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kNone);
}

// One of the level's actors, as it looks now: a mesh drawn, a sprite kept
// for drawSprites. Hidden ones only if `evenHidden` (the HUD draws some).
void LevelView::drawLiveActor(VmObject *a, std::vector<SpriteDraw> &sprites, const ScriptLevel::HudActor *hud) {
    constexpr int kDrawSprite = 1;
    constexpr int kDrawMesh = 2;
    constexpr int kDrawVerticalSprite = 5;
    constexpr int kDrawSpriteAnimOnce = 7;
    const bool evenHidden = hud != nullptr;
    LevelPlay::Look look;
    // Drawn for the HUD: as it looked when the HUD asked.
    auto asAsked = [&] {
        if (!hud)
            return;
        look.drawType = hud->drawType;
        look.style = uint8_t(hud->style);
        look.scaleGlow = hud->scaleGlow;
    };
    if (!_play->look(a, look))
        return;
    asAsked();
    if (look.hidden && !evenHidden)
        return;
    // The Fallen's copies of it (MultiMesh), drawn in its place unless it
    // asks to be drawn itself as well.
    if (!hud) {
        std::vector<LevelPlay::Copy> copies;
        if (_play->copies(a, copies)) {
            drawCopies(a, look, copies, sprites);
            if (!look.renderInitial)
                return;
        }
    }
    if (look.drawType == kDrawMesh && look.mesh) {
        // About to be drawn: its scripts get their say first, and it's
        // drawn as they leave it.
        _play->preRender(a);
        look = {};
        if (!_play->look(a, look))
            return;
        asAsked();
        if ((look.hidden && !evenHidden) || look.drawType != kDrawMesh || !look.mesh)
            return;
        if (!look.tweenFrom.empty())
            liveMesh(look.mesh, look.tweenFrom); // borrowed too, if it's from the shared sets
        const int at = liveMesh(look.mesh, look.animSequence);
        if (at < 0)
            return;
        const Mesh &mesh = _meshes[size_t(at)];
        const MeshAnimSeq *anim = mesh.findAnim(look.animSequence);
        MeshPose pose = poseAtFraction(mesh, anim, look.animFrame, look.animLoop);
        // Tweening into it: from the frame it was showing to its first.
        if (const MeshAnimSeq *from = look.tweenFrom.empty() ? nullptr : mesh.findAnim(look.tweenFrom);
            from && anim && look.animFrame < 0) {
            const MeshPose old = poseAtFraction(mesh, from, look.tweenFromFrame, look.tweenFromLoop);
            pose = {old.lerp < 0.5f ? old.frame : old.next, pose.frame, tweenProgress(look, *anim)};
        }
        if (look.particles) {
            drawParticles(a, look, mesh, pose, sprites);
            return;
        }
        std::vector<gfx::TextureId> skins;
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < look.multiSkins.size() ? look.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = look.skin;
            if (!tex && k < mesh.textures.size())
                tex = _ctx.packages.resolve(*look.mesh.package, mesh.textures[k]);
            if (!tex) // no skin of its own: the actor's Texture
                tex = look.texture;
            skins.push_back(_textured ? textureOf(tex).first : 0);
        }
        _play->drawn(a);
        // DrawScale and AxisScale both, as Engine.dll's frame builders
        // (0x1032b630, 0x103c8d37).
        const gfx::Mat4 placement =
            gfx::multiply(meshPlacement(look.location, look.rotation, look.drawScale, look.prePivot, look.preRotate),
                          meshAxisScaling(mesh, look.axisScale));
        const MeshLight light = meshLightAt(look.location, &look, a);
        _lastMesh = {};
        if ((look.layers.empty() && look.adjust.empty() && look.faces.empty()) || !mesh.bones ||
            !drawLayered(a, look, at, placement, skins, light)) {
            _lastMesh = {};
            drawActorMesh(_gpuMeshes[size_t(at)], placement, pose, skins, light, look);
            _lastMesh = {true, _gpuMeshes[size_t(at)], placement, pose};
        }
        if (!hud && _lastMesh.set && look.style == 1)
            drawShadow(a, look.location);
    } else if ((look.drawType == kDrawSprite || look.drawType == kDrawVerticalSprite ||
                look.drawType == kDrawSpriteAnimOnce) &&
               look.texture) {
        const auto [texture, size] = textureOf(look.texture);
        if (!texture)
            return;
        _play->drawn(a);
        SpriteDraw d;
        d.texture = texture;
        d.at = toWorld(Vec3f{look.location[0] - look.prePivot[0], look.location[1] - look.prePivot[1],
                             look.location[2] - look.prePivot[2]});
        d.width = float(size[0]) * look.drawScale;
        d.height = float(size[1]) * look.drawScale;
        d.style = look.style;
        d.vertical = look.drawType == kDrawVerticalSprite;
        d.color = spriteTint(look.colorFilter, look.scaleGlow);
        sprites.push_back(d);
    }
}

// A bParticles mesh, as Render.dll's DrawMesh (0x10b0ef40): no
// triangles, its Texture facing the camera at each vertex as posed,
// DrawScale times the texture's size across; the mesh itself placed as if
// DrawScale were 1 (0x1032b619). With bRandomFrame each vertex has one of
// MultiSkins, by its index (0 to 7: index / 3, rounded), and none if that
// one's empty. Tinted by ScaleGlow as a sprite.
void LevelView::drawParticles(VmObject *a, const LevelPlay::Look &look, const Mesh &mesh, const MeshPose &pose,
                              std::vector<SpriteDraw> &sprites) {
    if (!look.texture)
        return;
    const auto n = size_t(mesh.frameVerts);
    // Frames without their own vertices (keyFrames) use the first.
    const size_t frames = n ? mesh.verts.size() / n : 0;
    if (frames == 0)
        return;
    const size_t f0 = size_t(pose.frame) < frames ? size_t(pose.frame) : 0;
    const size_t f1 = size_t(pose.next) < frames ? size_t(pose.next) : 0;
    _play->drawn(a);
    const gfx::Mat4 placement =
        gfx::multiply(meshPlacement(look.location, look.rotation, 1, look.prePivot, look.preRotate),
                      meshAxisScaling(mesh, look.axisScale));
    const std::array<float, 3> tint = spriteTint(look.colorFilter, look.scaleGlow);
    const auto [own, ownSize] = textureOf(look.texture);
    for (size_t v = 0; v < n; ++v) {
        gfx::TextureId texture = own;
        std::array<int, 2> size = ownSize;
        if (look.randomFrame) {
            const ObjectRef skin = look.multiSkins[size_t(std::lround(float(v) / 3)) & 7];
            if (!skin)
                continue;
            std::tie(texture, size) = textureOf(skin);
        }
        if (!texture)
            continue;
        Vec3f at{};
        for (size_t c = 0; c < 3; ++c) {
            const float x0 = mesh.verts[f0 * n + v][c];
            at[c] = x0 + (mesh.verts[f1 * n + v][c] - x0) * pose.lerp;
        }
        SpriteDraw d;
        d.texture = texture;
        d.at = gfx::transformPoint(placement, placeMeshVertex(mesh, at, _ctx.meshTurn, _ctx.meshYawQuarters));
        d.width = float(size[0]) * look.drawScale;
        d.height = float(size[1]) * look.drawScale;
        d.style = look.style;
        d.color = tint;
        sprites.push_back(d);
    }
}

// An actor's copies: its mesh posed as it is (its animation now) in each
// one's place, turned, scaled by DrawScale and along its own axes by
// AxisScale (along its axes before RotOrigin), in its style, as bright as its ScaleGlow (where it's lit, or
// wholly if the actor's unlit); or, for a sprite, its texture (the copy's
// own, else the actor's) facing the camera.
void LevelView::drawCopies(VmObject *a, const LevelPlay::Look &look, const std::vector<LevelPlay::Copy> &copies,
                           std::vector<SpriteDraw> &sprites) {
    constexpr int kDrawSprite = 1;
    constexpr int kDrawMesh = 2;
    constexpr int kDrawVerticalSprite = 5;
    constexpr int kDrawSpriteAnimOnce = 7;
    _play->drawn(a);
    if (look.drawType == kDrawMesh && look.mesh) {
        const int at = liveMesh(look.mesh, look.animSequence);
        if (at < 0)
            return;
        const Mesh &mesh = _meshes[size_t(at)];
        std::vector<gfx::TextureId> skins;
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < look.multiSkins.size() ? look.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = look.skin;
            if (!tex && k < mesh.textures.size())
                tex = _ctx.packages.resolve(*look.mesh.package, mesh.textures[k]);
            if (!tex) // no skin of its own: the actor's Texture
                tex = look.texture;
            skins.push_back(_textured ? textureOf(tex).first : 0);
        }
        const MeshPose pose = poseAtFraction(mesh, mesh.findAnim(look.animSequence), look.animFrame, look.animLoop);
        for (const LevelPlay::Copy &c : copies) {
            const gfx::Mat4 placement =
                gfx::multiply(meshPlacement(c.location, c.rotation, c.drawScale, look.prePivot, look.preRotate),
                              meshAxisScaling(mesh, c.axisScale));
            MeshLight light = look.unlit ? MeshLight{} : meshLightAt(c.location);
            for (size_t k = 0; k < 3; ++k) {
                light.ambient[k] *= c.scaleGlow;
                light.directed[k] *= c.scaleGlow;
            }
            drawMesh(*_renderer, _gpuMeshes[size_t(at)], placement, pose, skins, light, kExposure, c.style, &_fog);
        }
    } else if (look.drawType == kDrawSprite || look.drawType == kDrawVerticalSprite ||
               look.drawType == kDrawSpriteAnimOnce) {
        for (const LevelPlay::Copy &c : copies) {
            const auto [texture, size] = textureOf(c.texture ? c.texture : look.texture);
            if (!texture)
                continue;
            SpriteDraw d;
            d.texture = texture;
            d.at = toWorld(c.location);
            d.width = float(size[0]) * c.drawScale;
            d.height = float(size[1]) * c.drawScale;
            d.style = c.style;
            d.vertical = look.drawType == kDrawVerticalSprite;
            d.color = spriteTint(c.colorFilter, c.scaleGlow);
            sprites.push_back(d);
        }
    }
}

// The decals stuck to the world: their texture flat on the surface, its
// size by their scale, multiplying what's under it (grey leaves it as it
// is: marks, scorches, footprints).
void LevelView::addDecals(std::vector<SpriteDraw> &sprites) {
    for (const ScriptLevel::Decal &d : _play->decals()) {
        const auto [texture, size] = textureOf(d.texture);
        if (!texture)
            continue;
        const std::array<float, 3> &n = d.normal;
        const std::array<float, 3> &u = d.up;
        const Vec3f right{u[1] * n[2] - u[2] * n[1], u[2] * n[0] - u[0] * n[2], u[0] * n[1] - u[1] * n[0]};
        SpriteDraw s;
        s.texture = texture;
        s.at = toWorld(Vec3f{d.at[0] + n[0] * 0.5f, d.at[1] + n[1] * 0.5f, d.at[2] + n[2] * 0.5f});
        s.width = float(size[0]) * d.scale;
        s.height = float(size[1]) * d.scale;
        s.style = kStyleModulated;
        s.oriented = true;
        s.right = toWorld(right);
        s.up = toWorld(Vec3f{u[0], u[1], u[2]});
        sprites.push_back(s);
    }
}

// How far through its tween into its animation an actor is (0 at the
// start, 1 there): AnimFrame rises from -1/frames to 0.
float LevelView::tweenProgress(const LevelPlay::Look &look, const MeshAnimSeq &anim) {
    return std::clamp(1 + look.animFrame * float(std::max(1, anim.numFrames)), 0.0f, 1.0f);
}

// A mesh with layered animations playing on parts of it, its bones bent
// by its scripts, or its face moved: posed now from its own animation and theirs, uploaded as one frame (kept until the next
// frame is drawn) and drawn. False if it can't be (no skeleton, an
// animation it hasn't): drawn as its own animation alone then.
bool LevelView::drawLayered(VmObject *actor, const LevelPlay::Look &look, int at, const gfx::Mat4 &placement,
                            const std::vector<gfx::TextureId> &skins, const MeshLight &light) {
    auto it = _posers.find({look.mesh.package, look.mesh.exportIndex});
    if (it == _posers.end()) {
        Poser p;
        p.poser = LayeredPoser::open(*look.mesh.package, look.mesh.exportIndex, _ctx.sets());
        if (p.poser) {
            // Where its skeleton's axes go as it's drawn.
            const Mesh &m = _meshes[size_t(at)];
            const Vec3f zero = placeMeshVertex(m, m.origin, _ctx.meshTurn, _ctx.meshYawQuarters);
            std::array<Vec3f, 3> axes{};
            for (size_t k = 0; k < 3; ++k) {
                Vec3f step = m.origin;
                step[k] += m.scale[k] != 0 ? 1 / m.scale[k] : 0;
                const Vec3f to = placeMeshVertex(m, step, _ctx.meshTurn, _ctx.meshYawQuarters);
                axes[k] = {to[0] - zero[0], to[1] - zero[1], to[2] - zero[2]};
            }
            p.poser->setPlacement(axes);
        }
        it = _posers.emplace(std::make_pair(look.mesh.package, look.mesh.exportIndex), std::move(p)).first;
    }
    Poser &p = it->second;
    if (!p.poser)
        return false;
    std::vector<LayeredPoser::Layer> layers;
    for (const LevelPlay::Look::Layer &l : look.layers) {
        // Its group's bones (the mesh's lists), else the bone's and those
        // under it (or all but those).
        const std::pair<std::string, bool> key{l.group + "|" + l.bone, l.rest};
        auto part = p.parts.find(key);
        if (part == p.parts.end()) {
            std::vector<bool> bones = p.poser->groupPart(l.group);
            if (bones.empty())
                bones = p.poser->part(l.bone, l.rest);
            part = p.parts.emplace(key, std::move(bones)).first;
        }
        if (!part->second.empty())
            layers.push_back({l.anim, l.frame, &part->second, l.weight, l.clear, l.loop, p.poser->groupIndex(l.group),
                              l.bone.empty() ? -1 : p.poser->boneIndex(l.bone), l.from.get(), l.fadingOut});
    }
    std::vector<LayeredPoser::Face> faces;
    if (p.poser->hasFace())
        for (const LevelPlay::Look::Face &f : look.faces)
            faces.push_back({f.anim, f.seconds, f.weight, f.fromSeconds, f.fromWeight, f.mix});
    // The level's poser for it, when the mesh is turned as the level has
    // it: it has worked this pose out already for the scripts' bone
    // queries this tick, IK and all, and keeps it.
    LayeredPoser *poser = p.poser.get();
    if (_play && actor && _ctx.meshTurn == MeshTurn::kReverse && _ctx.meshYawQuarters == kImportYawQuarters)
        if (LayeredPoser *shared = _play->level().poserOf(actor))
            poser = shared;
    std::vector<Vec3f> verts;
    if ((layers.empty() && look.adjust.empty() && faces.empty()) ||
        !poser->pose({look.animSequence, look.animLoop}, look.animFrame, layers, verts, &look.adjust, &faces))
        return false;
    // Tweening into its animation: from the pose it was in.
    const Mesh &own = _meshes[size_t(at)];
    if (const MeshAnimSeq *anim = own.findAnim(look.animSequence);
        anim && look.animFrame < 0 && !look.tweenFrom.empty()) {
        std::vector<Vec3f> old;
        if (p.poser->pose({look.tweenFrom, look.tweenFromLoop}, look.tweenFromFrame, layers, old, &look.adjust,
                          &faces) &&
            old.size() == verts.size()) {
            const float t = tweenProgress(look, *anim);
            for (size_t v = 0; v < verts.size(); ++v)
                for (size_t k = 0; k < 3; ++k)
                    verts[v][k] = old[v][k] + (verts[v][k] - old[v][k]) * t;
        }
    }
    // One frame of the mesh: its vertices swapped for the posed ones while
    // it's uploaded.
    Mesh &mesh = _meshes[size_t(at)];
    if (verts.size() != size_t(mesh.frameVerts))
        return false;
    std::swap(mesh.verts, verts);
    const int32_t frames = mesh.animFrames;
    mesh.animFrames = 1;
    GpuMesh gpu;
    auto topology = _posedTopology.find(at);
    if (topology == _posedTopology.end()) {
        MeshTopology t;
        buildMeshTopology(mesh, t); // none: the upload below fails as uploadMesh's would
        topology = _posedTopology.emplace(at, std::move(t)).first;
    }
    const Error err = topology->second.indices.empty()
                          ? Error::fail("the mesh has no visible triangles")
                          : uploadMesh(*_renderer, mesh, topology->second, gpu, _ctx.meshTurn, _ctx.meshYawQuarters);
    mesh.animFrames = frames;
    std::swap(mesh.verts, verts);
    if (err)
        return false;
    _posedMeshes.push_back(gpu);
    drawActorMesh(gpu, placement, MeshPose{}, skins, light, look);
    _lastMesh = {true, gpu, placement, MeshPose{}};
    return true;
}

// A line of how the game's going (the cut scene's bars are drawn under
// the info, drawOverlay).
void LevelView::drawPlayHud() {
    char line[200];
    const std::string title = _play->title();
    std::snprintf(line, sizeof line, "%s%s%s  health %d  %s  %.0f s played%s%s", title.c_str(),
                  title.empty() ? "" : "  ", _play->player()->cls->name.c_str(), _play->health(),
                  _play->playerState().c_str(), double(_play->time()), _view.letterbox ? "  (cut scene)" : "",
                  _play->behind() > 1 ? ("  " + std::to_string(int(_play->behind())) + " s behind real time").c_str()
                                      : "");
    const int y = kFrameH - font::kLineHeight - 4;
    const int width = std::min(kFrameW, int(font::length(line)) * font::kAdvance + 8);
    // With the game's own HUD drawn, between its panels at the bottom
    // corners, and its objectives and subtitles are its own.
    const bool gameHud = !_play->level().hud().tiles.empty();
    const int left = gameHud ? std::max(0, (kFrameW - width) / 2) : 0;
    _overlayBottom = std::min(_overlayBottom, std::max(0, y - 2));
    for (int yy = y - 2; yy < kFrameH; ++yy) {
        uint8_t *p = _overlay.row(yy);
        for (int x = left; x < std::min(kFrameW, left + width); ++x)
            p[x * 4 + 3] = std::max<uint8_t>(p[x * 4 + 3], 150);
    }
    font::drawText(_overlay, left + 4, y, line, Color{230, 230, 230});
    if (gameHud)
        return;
    // The objectives active now, above it.
    int above = y - 2;
    const std::vector<std::string> objectives = _play->activeObjectives();
    if (!objectives.empty()) {
        std::string list = "Objectives:";
        for (const std::string &o : objectives)
            list += (list.size() > 11 ? "; " : " ") + o;
        const size_t columns = size_t(kFrameW - 8) / size_t(font::kAdvance);
        if (list.size() > columns)
            list = list.substr(0, columns - 3) + "...";
        above -= font::kLineHeight + 2;
        const int w = std::min(kFrameW, int(font::length(list)) * font::kAdvance + 8);
        _overlayBottom = std::min(_overlayBottom, std::max(0, above - 1));
        for (int yy = above - 1; yy < above + font::kLineHeight; ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = std::max<uint8_t>(p[x * 4 + 3], 150);
        }
        font::drawText(_overlay, 4, above, list.c_str(), Color{160, 200, 255});
    }
    drawSubtitles(above - 6);
}

// The lines being said, above the status line: who says it (the main
// characters by name, the rest by their code) and the line, wrapped.
void LevelView::drawSubtitles(int bottom) {
    static const std::map<std::string, std::string> kNames = {
        {"KRA", "Kira"},   {"SKO", "Sisko"}, {"WRF", "Worf"},   {"OBN", "O'Brien"}, {"DAX", "Dax"},
        {"BSR", "Bashir"}, {"ODO", "Odo"},   {"OBK", "Obanak"}, {"GRK", "Garak"},   {"CPU", "Computer"},
    };
    const size_t columns = size_t(kFrameW - 16) / size_t(font::kAdvance);
    std::vector<std::string> rows;
    for (const ScriptLevel::Subtitle &s : _play->level().subtitles()) {
        auto it = kNames.find(s.speaker);
        std::string text = (it != kNames.end() ? it->second : s.speaker) + ": " + s.text;
        while (!text.empty()) {
            size_t cut = text.size() <= columns ? text.size() : text.rfind(' ', columns);
            if (cut == std::string::npos || cut == 0)
                cut = std::min(columns, text.size());
            rows.push_back(text.substr(0, cut));
            text.erase(0, std::min(text.size(), cut + 1));
        }
    }
    int y = bottom - int(rows.size()) * font::kLineHeight;
    if (!rows.empty())
        _overlayBottom = std::min(_overlayBottom, std::max(0, y - 1));
    for (const std::string &row : rows) {
        const int width = int(font::length(row.c_str())) * font::kAdvance + 8;
        const int x = std::max(0, (kFrameW - width) / 2);
        for (int yy = std::max(0, y - 1); yy < std::min(kFrameH, y + font::kLineHeight); ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int xx = x; xx < std::min(kFrameW, x + width); ++xx)
                p[xx * 4 + 3] = std::max<uint8_t>(p[xx * 4 + 3], 170);
        }
        font::drawText(_overlay, x + 4, y, row.c_str(), Color{255, 240, 160});
        y += font::kLineHeight;
    }
}

} // namespace reluva::unreal
