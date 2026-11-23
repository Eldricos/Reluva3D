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
 * surfaces' corner lights; from the player's start, moved about with
 * W/S A/D R/F T/G, or seen whole from above (V).
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
               "V: overview, H: things";
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
        int drawn = 0;
        Draw3D draw(screen, camera, !_colormaps.empty() && _colormaps[0] ? *_colormaps[0] : _fallback);
        for (const Level::Sector &sector : _level.sectors) {
            for (int s = sector.firstSurface; s < sector.firstSurface + sector.surfaceCount; ++s) {
                const Level::Surface &surf = _level.surfaces[size_t(s)];
                if (surf.geometryMode == 0 || surf.material < 0)
                    continue;
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
                    corner.light = std::clamp(surf.intensities[c] + sector.extraLight + surf.extraLight, 0.0f, 1.0f);
                    corners.push_back(corner);
                }
                draw.polygon(corners, paint);
                ++drawn;
            }
        }
        const int things = _showThings ? drawThings(draw) : 0;
        char info[200];
        std::snprintf(info, sizeof(info),
                      "  %zu sectors, %zu surfaces (%d drawn), %zu things (%d drawn); %s at %.2f %.2f %.2f",
                      _level.sectors.size(), _level.surfaces.size(), drawn, _level.things.size(), things,
                      _overview ? "overview" : "eye", double(_eye.x), double(_eye.y), double(_eye.z));
        drawStatus(screen, line + info);
    }

private:
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
    std::map<std::string, std::unique_ptr<Model>> _models;
    std::map<std::string, std::unique_ptr<Sprite>> _sprites;
};

} // namespace

std::unique_ptr<Page> makeLevelPage(const ViewerData &data) {
    return std::make_unique<LevelPage>(data);
}

} // namespace reluva::sith
