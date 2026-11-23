// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page of the .jkl levels, drawn in 3D.

#include "common/fs.h"
#include "engines/sith/formats/level.h"
#include "engines/sith/formats/model.h"
#include "engines/sith/formats/sprite.h"
#include "engines/sith/pages/pages.h"
#include "engines/sith/render/draw3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kSky = {20, 20, 28};
constexpr float kTurn = 3.14159265f / 12;
constexpr float kPi = 3.14159265f;

// A camera at `eye` looking along yaw (radians about z, 0 along +y, as the
// game's things face) and pitch (up from level).
soft3d::OrbitCamera flyCamera(const Vec3 &eye, float yaw, float pitch, int w, int h) {
    // soft3d's orbit camera looks at its centre from `distance` away; from 1
    // away, looking at the point 1 ahead, it's at the eye.
    const Vec3 ahead{-std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
    soft3d::OrbitCamera cam({eye.x + ahead.x, eye.y + ahead.y, eye.z + ahead.z}, yaw, -pitch, 1.0f, w, h);
    cam.nearZ = 0.005f;
    return cam;
}

/**
 * The levels, drawn through their sectors' colormaps, lit by their
 * surfaces' corner lights and tinted by the camera's sector, as the game
 * draws them; from the player's start, moved about with W/S A/D R/F T/G,
 * or seen whole from above (V). M marks the surfaces or sectors with each
 * flag in turn (jkl.md, Behaviour), and J moves the camera to the next
 * sector marked, or holding a surface marked.
 */
class LevelPage : public Page {
public:
    explicit LevelPage(const ViewerData &data) : _data(data), _materials(data.files) {
        // static.jkl in Res2.gob is the shared lists, with no geometry.
        for (const FileRef &f : filesEnding(data.files, ".jkl"))
            if (fs::toLower(f.second).find("static.jkl") == std::string::npos)
                _files.push_back(f);
    }

    const char *name() const override { return "Levels"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override {
        return "Left/Right: level, W/S: forward/back, A/D: turn, R/F: up/down, T/G: look up/down, Q/E: sideways, "
               "V: overview, H: things, M: mark a flag, J: to the next marked sector";
    }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index)) {
            _loaded = false;
            return true;
        }
        const float step = _overview ? 0 : 0.15f;
        const Vec3 ahead{-std::sin(_yaw), std::cos(_yaw), 0};
        const Vec3 side{std::cos(_yaw), std::sin(_yaw), 0};
        if (pressed == 'w')
            _eye = {_eye.x + ahead.x * step, _eye.y + ahead.y * step, _eye.z};
        else if (pressed == 's')
            _eye = {_eye.x - ahead.x * step, _eye.y - ahead.y * step, _eye.z};
        else if (pressed == 'q')
            _eye = {_eye.x - side.x * step, _eye.y - side.y * step, _eye.z};
        else if (pressed == 'e')
            _eye = {_eye.x + side.x * step, _eye.y + side.y * step, _eye.z};
        else if (pressed == 'a')
            _yaw += kTurn;
        else if (pressed == 'd')
            _yaw -= kTurn;
        else if (pressed == 'r')
            _eye.z += step;
        else if (pressed == 'f')
            _eye.z -= step;
        else if (pressed == 't')
            _pitch = std::min(1.5f, _pitch + kTurn);
        else if (pressed == 'g')
            _pitch = std::max(-1.5f, _pitch - kTurn);
        else if (pressed == 'v')
            _overview = !_overview;
        else if (pressed == 'h')
            _showThings = !_showThings;
        else if (pressed == 'm')
            _marking = (_marking + 1) % kMarks.size();
        else if (pressed == 'j')
            jumpToMarked();
        else
            return false;
        load();
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kSky);
        const FileRef &file = _files[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        load();
        if (!_error.empty()) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        const soft3d::OrbitCamera camera = _overview
                                               ? soft3d::OrbitCamera({_centre.x, _centre.y, _centre.z}, _yaw, 0.9f,
                                                                     _radius * 2.2f, kViewerW, kViewerH - 16)
                                               : flyCamera(_eye, _yaw, _pitch, kViewerW, kViewerH - 16);
        const Vec3 camEye{camera.eye.x, camera.eye.y, camera.eye.z};
        const Mark &mark = kMarks[_marking];
        int drawn = 0;
        int marked = 0;
        Draw3D draw(screen, camera, !_colormaps.empty() && _colormaps[0] ? *_colormaps[0] : _fallback);
        // The skies as the game places them, by the camera's turn (jkl.md, Skies).
        constexpr float kDegrees = 180.0f / 3.14159265f;
        Sky horizon;
        horizon.horizonDistance = _level.header.horizonDistance;
        horizon.pixelsPerRev = _level.header.horizonPixelsPerRev;
        horizon.ceilingZ = _level.header.ceilingSkyZ;
        horizon.horizonOffset = _level.header.horizonSkyOffset;
        horizon.ceilingOffset = _level.header.ceilingSkyOffset;
        horizon.yaw = _yaw * kDegrees;
        horizon.pitch = (_overview ? -0.9f : _pitch) * kDegrees;
        Sky ceiling = horizon;
        ceiling.kind = Sky::kCeiling;
        // See-through surfaces (face flag 0x2), drawn last, the farthest first.
        struct Later {
            float distance;
            std::vector<Corner> corners;
            Paint paint;
        };
        std::vector<Later> later;
        for (const Level::Sector &sector : _level.sectors) {
            for (int s = sector.firstSurface; s < sector.firstSurface + sector.surfaceCount; ++s) {
                const Level::Surface &surf = _level.surfaces[size_t(s)];
                if (surf.geometryMode == 0 || surf.material < 0)
                    continue;
                const bool isMarked = mark.mask && (mark.of == Mark::kSector ? (sector.flags & mark.mask)
                                                    : mark.of == Mark::kFace ? (surf.faceFlags & mark.mask)
                                                                             : (surf.surfaceFlags & mark.mask));
                // A surface faces into its sector: seen from behind, from
                // outside the level or another sector, it isn't drawn.
                const Vec3 &p0 = _level.vertices[size_t(surf.vertices[0])];
                if ((camEye.x - p0.x) * surf.normal.x + (camEye.y - p0.y) * surf.normal.y +
                        (camEye.z - p0.z) * surf.normal.z <
                    0)
                    continue;
                Paint paint;
                paint.material = _materials.find(_level.materials[size_t(surf.material)].name);
                if (sector.colormap >= 0 && sector.colormap < int(_colormaps.size()))
                    paint.colormap = _colormaps[size_t(sector.colormap)].get();
                if (surf.geometryMode < 4) {
                    paint.color = paint.material && !paint.material->cels.empty() ? paint.material->cels[0].color : 0;
                    paint.material = nullptr;
                } else if (surf.surfaceFlags & kHorizonSky) {
                    paint.sky = &horizon;
                } else if (surf.surfaceFlags & kCeilingSky) {
                    paint.sky = &ceiling;
                }
                std::vector<Corner> corners;
                corners.reserve(surf.vertices.size());
                for (size_t c = 0; c < surf.vertices.size(); ++c) {
                    Corner corner;
                    corner.at = _level.vertices[size_t(surf.vertices[c])];
                    const int t = surf.texVertices[c];
                    if (t >= 0) {
                        corner.u = _level.texVertices[size_t(t)][0];
                        corner.v = _level.texVertices[size_t(t)][1];
                    }
                    corner.light = cornerLight(sector, surf, surf.intensities[c]);
                    corners.push_back(corner);
                }
                if (isMarked) {
                    paint.mark = &kMarkColor;
                    ++marked;
                }
                if (surf.faceFlags & kFaceTranslucent) {
                    paint.alpha = kTranslucentAlpha;
                    const float dx = p0.x - camEye.x;
                    const float dy = p0.y - camEye.y;
                    const float dz = p0.z - camEye.z;
                    later.push_back({dx * dx + dy * dy + dz * dz, std::move(corners), paint});
                    continue;
                }
                draw.polygon(corners, paint);
                ++drawn;
            }
        }
        std::sort(later.begin(), later.end(), [](const Later &a, const Later &b) { return a.distance > b.distance; });
        for (const Later &l : later) {
            draw.polygon(l.corners, l.paint);
            ++drawn;
        }
        const int things = _showThings ? drawThings(draw) : 0;
        // The camera's sector tints the screen, as it does the game's palette.
        const int in = _overview ? -1 : sectorAt(camEye);
        if (in >= 0)
            tint(screen, _level.sectors[size_t(in)].tint);
        if (mark.mask)
            line += std::string("  marked: ") + mark.name + " (" + std::to_string(marked) + " drawn)";
        if (in >= 0) {
            const Level::Sector &s = _level.sectors[size_t(in)];
            char where[120];
            std::snprintf(where, sizeof(where), "  in sector %d, flags 0x%x, tint %.2f %.2f %.2f;", in, s.flags,
                          double(s.tint.x), double(s.tint.y), double(s.tint.z));
            line += where;
        }
        char info[200];
        std::snprintf(info, sizeof(info),
                      "  %zu sectors, %zu surfaces (%d drawn), %zu things (%d drawn); %s at %.2f %.2f %.2f",
                      _level.sectors.size(), _level.surfaces.size(), drawn, _level.things.size(), things,
                      _overview ? "overview" : "eye", double(_eye.x), double(_eye.y), double(_eye.z));
        drawStatus(screen, line + info);
    }

private:
    // A flag to mark: what it's a flag of, its bits, and its name on jkl.md.
    struct Mark {
        enum Of { kSurface, kFace, kSector };
        Of of;
        uint32_t mask;
        const char *name;
    };
    static constexpr std::array<Mark, 19> kMarks = {{
        {Mark::kSurface, 0, ""},
        {Mark::kSurface, 0x1, "floors"},
        {Mark::kSurface, 0x2, "surfaces whose cog is told"},
        {Mark::kSurface, 0x4, "solid surfaces"},
        {Mark::kSurface, 0x8, "no floor for the next step"},
        {Mark::kSurface, 0x80, "no impact damage"},
        {Mark::kSurface, 0x200 | 0x400, "skies"},
        {Mark::kSurface, 0x800, "scrolling"},
        {Mark::kSurface, 0x1000 | 0x2000, "icy"},
        {Mark::kSurface, 0x10000, "metal"},
        {Mark::kSurface, 0x20000, "water"},
        {Mark::kSurface, 0x40000, "puddles"},
        {Mark::kSurface, 0x80000, "earth"},
        {Mark::kFace, 0x2, "see-through faces"},
        {Mark::kSector, 0x2, "underwater sectors"},
        {Mark::kSector, 0x4, "sectors whose cog is told"},
        {Mark::kSector, 0x10, "sectors left off the map"},
        {Mark::kSector, 0x40, "pits"},
        {Mark::kSector, 0x1000, "sectors with a collide box"},
    }};
    static constexpr Color kMarkColor = {255, 210, 0};

    // Whether the mark picks out `sector`: by its flags, or a surface's.
    bool marks(const Mark &mark, const Level::Sector &sector) const {
        if (!mark.mask)
            return false;
        if (mark.of == Mark::kSector)
            return (sector.flags & mark.mask) != 0;
        for (int s = sector.firstSurface; s < sector.firstSurface + sector.surfaceCount; ++s) {
            const Level::Surface &surf = _level.surfaces[size_t(s)];
            if ((mark.of == Mark::kFace ? surf.faceFlags : surf.surfaceFlags) & mark.mask)
                return true;
        }
        return false;
    }

    // To the centre of the next sector the mark picks out, after the one
    // jumped to last, looking along it.
    void jumpToMarked() {
        const Mark &mark = kMarks[_marking];
        const size_t n = _level.sectors.size();
        for (size_t k = 1; k <= n; ++k) {
            const size_t i = (_jumped + k) % n;
            if (!marks(mark, _level.sectors[i]))
                continue;
            _jumped = i;
            _eye = _level.sectors[i].center;
            _overview = false;
            return;
        }
    }

    // The sector holding `p`, or -1: a sector is convex, its surfaces
    // facing in.
    int sectorAt(const Vec3 &p) const {
        for (size_t i = 0; i < _level.sectors.size(); ++i) {
            const Level::Sector &sector = _level.sectors[i];
            if (p.x < sector.bounds[0].x || p.y < sector.bounds[0].y || p.z < sector.bounds[0].z ||
                p.x > sector.bounds[1].x || p.y > sector.bounds[1].y || p.z > sector.bounds[1].z)
                continue;
            bool inside = sector.surfaceCount > 0;
            for (int s = sector.firstSurface; inside && s < sector.firstSurface + sector.surfaceCount; ++s) {
                const Level::Surface &surf = _level.surfaces[size_t(s)];
                if (surf.vertices.empty())
                    continue;
                const Vec3 &v = _level.vertices[size_t(surf.vertices[0])];
                inside =
                    (p.x - v.x) * surf.normal.x + (p.y - v.y) * surf.normal.y + (p.z - v.z) * surf.normal.z >= -0.0001f;
            }
            if (inside)
                return int(i);
        }
        return -1;
    }

    // Each of red, green and blue times 1 + its tint less half the other
    // two's, each tint held to 0 to 1, as the game changes its palette
    // (0x4c8d30, 0x4290e0); above the status line.
    static void tint(Surface &screen, const Vec3 &t) {
        const float r = std::clamp(t.x, 0.0f, 1.0f);
        const float g = std::clamp(t.y, 0.0f, 1.0f);
        const float b = std::clamp(t.z, 0.0f, 1.0f);
        if (r == 0 && g == 0 && b == 0)
            return;
        const float k[3] = {1 + r - (g + b) / 2, 1 + g - (r + b) / 2, 1 + b - (r + g) / 2};
        for (int y = 0; y < screen.h - 16; ++y) {
            uint8_t *p = screen.row(y);
            for (int x = 0; x < screen.w; ++x, p += 4)
                for (int c = 0; c < 3; ++c)
                    p[c] = uint8_t(std::clamp(float(p[c]) * k[c], 0.0f, 255.0f));
        }
    }

    // A surface's corner light as the game's colormap renderer takes it
    // (jkl.md, Light): the corner's and the surface's, or the sector's extra
    // light where that's more (0x43c693); the skies unlit (0x4e5eb6); light
    // mode 1 the surface's extra light alone.
    // Surface flags: the two skies (0x4f2e30, 0x4f2f60).
    static constexpr uint32_t kHorizonSky = 0x200;
    static constexpr uint32_t kCeilingSky = 0x400;

    static float cornerLight(const Level::Sector &sector, const Level::Surface &surf, float corner) {
        if (surf.lightingMode == 0 || (surf.surfaceFlags & (kHorizonSky | kCeilingSky)))
            return 1.0f;
        const float own = std::clamp((surf.lightingMode == 1 ? 0.0f : corner) + surf.extraLight, 0.0f, 1.0f);
        return std::max(own, std::clamp(sector.extraLight, 0.0f, 1.0f));
    }

    // The things with a model or a sprite, each at its place, lit through
    // its sector's colormap; in the player's own view, not the player.
    int drawThings(Draw3D &draw) {
        int drawn = 0;
        for (const Level::Thing &t : _level.things) {
            if (!_overview && fs::toLower(_level.setting(t, "type")) == "player")
                continue;
            const Colormap *cmp = nullptr;
            if (t.sector >= 0 && t.sector < int(_level.sectors.size())) {
                const int c = _level.sectors[size_t(t.sector)].colormap;
                if (c >= 0 && c < int(_colormaps.size()))
                    cmp = _colormaps[size_t(c)].get();
            }
            const std::string modelName = _level.setting(t, "model3d");
            if (const Model *model = modelName.empty() ? nullptr : findModel(modelName)) {
                drawModel(draw, _materials, *model, poseNodes(*model), Transform::place(t.position, t.rotation), 0,
                          cmp);
                ++drawn;
                continue;
            }
            const std::string spriteName = _level.setting(t, "sprite");
            if (const Sprite *sprite = spriteName.empty() ? nullptr : findSprite(spriteName)) {
                Paint paint;
                paint.material = _materials.find(sprite->material);
                paint.colormap = cmp;
                draw.billboard(t.position, sprite->width, sprite->height, paint);
                ++drawn;
            }
        }
        return drawn;
    }

    // A model or sprite read once, by name; nullptr if it isn't in the archives.
    const Model *findModel(const std::string &name) {
        auto [it, added] = _models.try_emplace(fs::toLower(name));
        if (added) {
            std::vector<uint8_t> bytes;
            auto model = std::make_unique<Model>();
            if (!_data.files.read("3do/" + name, bytes) && !parseModel(bytes, *model))
                it->second = std::move(model);
        }
        return it->second.get();
    }

    const Sprite *findSprite(const std::string &name) {
        auto [it, added] = _sprites.try_emplace(fs::toLower(name));
        if (added) {
            std::vector<uint8_t> bytes;
            auto sprite = std::make_unique<Sprite>();
            if (!_data.files.read("misc/spr/" + name, bytes) && !parseSprite(bytes, *sprite))
                it->second = std::move(sprite);
        }
        return it->second.get();
    }

    void load() {
        if (_loaded)
            return;
        _loaded = true;
        _colormaps.clear();
        const FileRef &file = _files[_index];
        std::vector<uint8_t> bytes;
        Error err = file.first->read(file.second, bytes);
        if (!err)
            err = parseLevel(bytes, _level);
        _error = err ? err.message : "";
        if (!_error.empty())
            return;
        // One that can't be read is left empty, so the rest keep their numbers.
        for (const std::string &name : _level.colormaps) {
            auto cmp = std::make_unique<Colormap>();
            std::vector<uint8_t> cmpBytes;
            if (_data.files.read("misc/cmp/" + name, cmpBytes) || parseColormap(cmpBytes, *cmp))
                cmp.reset();
            _colormaps.push_back(std::move(cmp));
        }
        if (!_data.colormaps.empty())
            _fallback = _data.colormaps[0].second;

        // The whole level's middle and size, for the overview.
        Vec3 lo{1e9f, 1e9f, 1e9f};
        Vec3 hi{-1e9f, -1e9f, -1e9f};
        for (const Vec3 &v : _level.vertices) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        _centre = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
        _radius = std::max(1.0f, std::hypot(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) / 2);

        // The player's start: the first thing of type player, its eye a
        // little above it.
        _eye = _centre;
        _yaw = 0;
        _pitch = 0;
        for (const Level::Thing &t : _level.things)
            if (fs::toLower(_level.setting(t, "type")) == "player") {
                _eye = {t.position.x, t.position.y, t.position.z + 0.04f};
                _yaw = t.rotation.y * kPi / 180;
                break;
            }
    }

    const ViewerData &_data;
    MaterialCache _materials;
    std::vector<FileRef> _files;
    size_t _index = 0;
    bool _loaded = false;
    std::string _error;
    Level _level;
    std::vector<std::unique_ptr<Colormap>> _colormaps;
    Colormap _fallback;
    Vec3 _centre;
    float _radius = 1;
    Vec3 _eye;
    float _yaw = 0;
    float _pitch = 0;
    bool _overview = false;
    bool _showThings = true;
    size_t _marking = 0;
    size_t _jumped = 0;
    std::map<std::string, std::unique_ptr<Model>> _models;
    std::map<std::string, std::unique_ptr<Sprite>> _sprites;
};

} // namespace

std::unique_ptr<Page> makeLevelPage(const ViewerData &data) {
    return std::make_unique<LevelPage>(data);
}

} // namespace reluva::sith
