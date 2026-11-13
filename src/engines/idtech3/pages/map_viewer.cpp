// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a wireframe of a .bsp's world surfaces
// (polygons, triangle soups and patch control grids) with an orbit camera,
// and a summary of its lumps and entities. A stand-in until the engine has
// a real renderer.

#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/pages/map_viewer.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace reluva::idtech3 {

namespace {

using Vec3 = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;

Vec3 sub(const Vec3 &a, const Vec3 &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float dot(const Vec3 &a, const Vec3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(const Vec3 &v) {
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? Vec3{v[0] / len, v[1] / len, v[2] / len} : v;
}

constexpr float kNear = 4.0f;

} // namespace

MapViewer::MapViewer(ViewerContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            _names.push_back(n);
    }
}

void MapViewer::activate() {
    if (!_names.empty() && _edges.empty() && _error.empty())
        load(_index);
    _dirty = true;
}

void MapViewer::load(int index) {
    _index = index;
    _error.clear();
    _info.clear();
    _edges.clear();
    const std::string &name = _names[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");

    std::vector<uint8_t> data;
    if (Error err = _ctx.files.read(name, data)) {
        _error = err.message;
        return;
    }
    if (Error err = parseBsp(data, _bsp)) {
        _error = err.message;
        return;
    }
    std::vector<Entity> entities;
    Error entErr = parseEntities(_bsp.entities, entities);

    _info.push_back(name + "  (IBSP " + std::to_string(_bsp.version) + ")");
    _info.push_back(std::to_string(_bsp.shaders.size()) + " shaders, " + std::to_string(_bsp.surfaces.size()) +
                    " surfaces, " + std::to_string(_bsp.vertices.size()) + " vertices, " +
                    std::to_string(_bsp.models.size()) + " models");
    _info.push_back(std::to_string(_bsp.brushes.size()) + " brushes, " + std::to_string(_bsp.leafs.size()) +
                    " leafs, " + std::to_string(_bsp.visClusters) + " clusters, " +
                    std::to_string(_bsp.lightmapCount()) + " lightmaps, " + std::to_string(_bsp.fogs.size()) + " fogs");
    if (const auto &f = _bsp.fixes; f.total())
        _info.push_back("Corrected, as the original does: " + std::to_string(f.lightmaps) + " lightmap, " +
                        std::to_string(f.fogs) + " fog, " + std::to_string(f.flares) + " flare/bad, " +
                        std::to_string(f.types) + " type");
    int missing = 0;
    for (const BspShader &s : _bsp.shaders)
        if (!_ctx.shaders.find(s.name) && !_ctx.files.has(s.name + ".tga") && !_ctx.files.has(s.name + ".jpg"))
            ++missing;
    _info.push_back(std::to_string(missing) + " shaders with neither a script nor an image");
    if (entErr) {
        _info.push_back("Entities: " + entErr.message);
    } else {
        if (!entities.empty() && entities[0].has("message"))
            _info.push_back("\"" + entities[0].get("message") + "\"");
        std::map<std::string, int> classes;
        for (const Entity &e : entities)
            ++classes[e.classname()];
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto &[c, n] : classes)
            sorted.emplace_back(-n, c);
        std::sort(sorted.begin(), sorted.end());
        std::string line = std::to_string(entities.size()) + " entities:";
        for (size_t i = 0; i < sorted.size() && i < 6; ++i)
            line += " " + sorted[i].second + " x" + std::to_string(-sorted[i].first);
        _info.push_back(line);
    }

    if (_bsp.models.empty()) {
        _error = "The map has no world model";
        return;
    }
    const BspModel &world = _bsp.models[0];
    _mins = world.mins;
    _maxs = world.maxs;
    for (int si = world.firstSurface; si < world.firstSurface + world.numSurfaces; ++si) {
        const BspSurface &s = _bsp.surfaces[size_t(si)];
        const ShaderDef *def = _ctx.shaders.find(_bsp.shaders[size_t(s.shader)].name);
        if (def && (def->sky || def->hasSurfaceParm("nodraw")))
            continue;
        const BspVertex *v = &_bsp.vertices[size_t(s.firstVertex)];
        if (s.type == SurfaceType::kPlanar || s.type == SurfaceType::kTriangleSoup) {
            Color c{90, 200, 110};
            if (s.type == SurfaceType::kPlanar) {
                // Floors and ceilings brighter than walls.
                const float shade = 0.45f + 0.55f * std::fabs(s.lightmapVecs[2][2]);
                c = {uint8_t(110 * shade), uint8_t(160 * shade), uint8_t(255 * shade)};
            }
            std::set<std::pair<int, int>> seen;
            const int32_t *idx = &_bsp.indices[size_t(s.firstIndex)];
            for (int t = 0; t + 2 < s.numIndices; t += 3)
                for (int k = 0; k < 3; ++k) {
                    int a = idx[t + k];
                    int b = idx[t + (k + 1) % 3];
                    if (a > b)
                        std::swap(a, b);
                    if (seen.insert({a, b}).second)
                        _edges.push_back({v[a].pos, v[b].pos, c, false});
                }
        } else if (s.type == SurfaceType::kPatch) {
            const Color c{255, 170, 60};
            const int w = s.patchWidth;
            const int h = s.patchHeight;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    if (x + 1 < w)
                        _edges.push_back({v[y * w + x].pos, v[y * w + x + 1].pos, c, true});
                    if (y + 1 < h)
                        _edges.push_back({v[y * w + x].pos, v[(y + 1) * w + x].pos, c, true});
                }
        }
    }
    resetView();
}

void MapViewer::resetView() {
    for (int i = 0; i < 3; ++i)
        _target[size_t(i)] = (_mins[size_t(i)] + _maxs[size_t(i)]) / 2;
    const Vec3 size = sub(_maxs, _mins);
    _distance = std::max(64.0f, std::sqrt(dot(size, size)) * 0.8f);
    _yaw = kPi / 4;
    _pitch = kPi / 5;
    _dirty = true;
}

void MapViewer::handleEvent(const Event &ev) {
    if (_names.empty())
        return;
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        _dragging = false;
    } else if (ev.type == EventType::MouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
        _dirty = true;
    } else if (ev.type == EventType::MouseWheel) {
        _distance = std::max(16.0f, _distance * std::pow(0.9f, float(ev.wheelY)));
        _dirty = true;
    } else if (ev.type == EventType::KeyDown) {
        const bool shift = ev.mods & keymod::Shift;
        // Moves along the ground, relative to where the camera looks.
        const float step = _distance * 0.05f;
        const Vec3 fwd{-std::cos(_yaw), -std::sin(_yaw), 0};
        const Vec3 side{std::sin(_yaw), -std::cos(_yaw), 0};
        auto move = [&](const Vec3 &dir, float amount) {
            for (int i = 0; i < 3; ++i)
                _target[size_t(i)] += dir[size_t(i)] * amount;
        };
        if (shift && ev.key == key::Left)
            _yaw += 0.1f;
        else if (shift && ev.key == key::Right)
            _yaw -= 0.1f;
        else if (ev.key == key::Left || ev.key == key::Right) {
            const int n = static_cast<int>(_names.size());
            load((_index + (ev.key == key::Left ? n - 1 : 1)) % n);
        } else if (shift && ev.key == key::Up)
            _pitch = std::min(1.5f, _pitch + 0.1f);
        else if (shift && ev.key == key::Down)
            _pitch = std::max(-1.5f, _pitch - 0.1f);
        else if (ev.key == '+' || ev.key == '=')
            _distance = std::max(16.0f, _distance * 0.9f);
        else if (ev.key == '-')
            _distance /= 0.9f;
        else if (ev.key == 'w')
            move(fwd, step);
        else if (ev.key == 's')
            move(fwd, -step);
        else if (ev.key == 'a')
            move(side, -step);
        else if (ev.key == 'd')
            move(side, step);
        else if (ev.key == 'q')
            move({0, 0, 1}, -step);
        else if (ev.key == 'e')
            move({0, 0, 1}, step);
        else if (ev.key == 'r')
            resetView();
        else if (ev.key == 'p')
            _showPatches = !_showPatches;
        else if (ev.key == 'i')
            _showInfo = !_showInfo;
        else
            return;
        _dirty = true;
    }
}

void MapViewer::update(uint64_t) {
    if (!_dirty)
        return;
    _dirty = false;
    render();
}

void MapViewer::render() {
    clear(8, 10, 16);
    const int lh = font::kLineHeight;
    if (!_error.empty()) {
        for (size_t i = 0; i < _info.size(); ++i)
            text(4, 4 + int(i) * lh, _info[i]);
        text(4, 4 + int(_info.size()) * lh, _error, {255, 120, 120});
        return;
    }

    Surface &s = _ctx.screen;
    // Z is up in id Tech 3 maps.
    const Vec3 offset{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    for (int i = 0; i < 3; ++i)
        _cam.pos[size_t(i)] = _target[size_t(i)] + offset[size_t(i)] * _distance;
    _cam.forward = normalize(sub(_target, _cam.pos));
    _cam.right = normalize(cross(_cam.forward, {0, 0, 1}));
    _cam.up = cross(_cam.right, _cam.forward);
    _cam.cx = float(s.w) / 2;
    _cam.cy = float(s.h) / 2;
    _cam.focal = float(s.w) / 2; // 90 degree horizontal field of view

    for (const Edge &e : _edges)
        if (_showPatches || !e.patch)
            drawLine3d(e.a, e.b, e.color);

    if (_showInfo) {
        for (size_t i = 0; i < _info.size(); ++i)
            text(4, 4 + int(i) * lh, _info[i]);
        text(4, s.h - lh - 2, "Blue: polygons  Green: triangle soups  Orange: patch control grids", {150, 150, 150});
    }
}

void MapViewer::drawLine3d(const Vec3 &a, const Vec3 &b, Color c) {
    const Vec3 ra = sub(a, _cam.pos);
    const Vec3 rb = sub(b, _cam.pos);
    Vec3 pa{dot(ra, _cam.right), dot(ra, _cam.up), dot(ra, _cam.forward)};
    Vec3 pb{dot(rb, _cam.right), dot(rb, _cam.up), dot(rb, _cam.forward)};
    // Clip against the near plane.
    if (pa[2] < kNear && pb[2] < kNear)
        return;
    if (pa[2] < kNear || pb[2] < kNear) {
        const float t = (kNear - pa[2]) / (pb[2] - pa[2]);
        const Vec3 m{pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t, kNear};
        (pa[2] < kNear ? pa : pb) = m;
    }
    drawLine2d(_cam.cx + pa[0] * _cam.focal / pa[2], _cam.cy - pa[1] * _cam.focal / pa[2],
               _cam.cx + pb[0] * _cam.focal / pb[2], _cam.cy - pb[1] * _cam.focal / pb[2], c);
}

void MapViewer::drawLine2d(float x0, float y0, float x1, float y1, Color c) {
    Surface &s = _ctx.screen;
    // Liang-Barsky clip to the screen, then step one pixel at a time.
    float t0 = 0;
    float t1 = 1;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0, float(s.w - 1) - x0, y0, float(s.h - 1) - y0};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return;
        } else {
            const float t = q[i] / p[i];
            if (p[i] < 0)
                t0 = std::max(t0, t);
            else
                t1 = std::min(t1, t);
        }
    }
    if (t0 > t1)
        return;
    const float sx = x0 + dx * t0;
    const float sy = y0 + dy * t0;
    const float ex = x0 + dx * t1;
    const float ey = y0 + dy * t1;
    const int steps = std::max(1, int(std::max(std::fabs(ex - sx), std::fabs(ey - sy))));
    for (int i = 0; i <= steps; ++i) {
        const int x = int(sx + (ex - sx) * float(i) / float(steps) + 0.5f);
        const int y = int(sy + (ey - sy) * float(i) / float(steps) + 0.5f);
        if (x < 0 || y < 0 || x >= s.w || y >= s.h)
            continue;
        uint8_t *px = s.row(y) + x * 4;
        px[0] = c.r;
        px[1] = c.g;
        px[2] = c.b;
        px[3] = 255;
    }
}

} // namespace reluva::idtech3
