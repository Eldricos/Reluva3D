// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: a model drawn with the GPU renderer, its
// parts placed by their .mtx files, lit by a fixed light.

#include "engines/age/viewers.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;

bool isLevel(const std::string& level) { return level == "H" || level == "M" || level == "L" || level == "VL"; }

} // namespace

ModelViewer::ModelViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("geometry/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".pkg") == 0)
            _names.push_back(n);
    }
}

ModelViewer::~ModelViewer() { deactivate(); }

void ModelViewer::deactivate() {
    if (_renderer)
        destroyModelMesh(*_renderer, _mesh);
    _textures.reset();
    _renderer = nullptr;
}

void ModelViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRgbA32);
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _textures = std::make_unique<TextureCache>(*_renderer, _ctx.files);
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRgbA32);
    show(_index);
}

void ModelViewer::show(int index) {
    _index = index;
    const std::string& name = _names[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");
    destroyModelMesh(*_renderer, _mesh);
    _textures->clear();
    _error.clear();
    _level = 0;
    _paintJob = 0;
    std::vector<uint8_t> data;
    if (Error err = _ctx.files.read(name, data))
        _error = err.message;
    else if (Error e = readPkg(data, _pkg))
        _error = e.message;
    if (!_error.empty()) {
        _pkg = Pkg{};
        _levels.clear();
        return;
    }
    _levels = modelLevels(_pkg);
    build();
    // Frame the finest level.
    _center = _mesh.center;
    _radius = std::max(0.1f, _mesh.radius);
    resetView();
}

void ModelViewer::build() {
    destroyModelMesh(*_renderer, _mesh);
    const std::string level = _levels.empty() ? std::string() : _levels[size_t(_level)];
    if (Error err = buildModelMesh(*_renderer, *_textures, _ctx.files, _names[size_t(_index)], _pkg, level, _paintJob,
                                   _showLights, _mesh))
        _error = err.message;
}

void ModelViewer::resetView() {
    // From the front left, a little above: the cars face -z.
    _yaw = -2.4f;
    _pitch = 0.3f;
    _distance = _radius * 2.4f;
}

void ModelViewer::handleEvent(const Event& ev) {
    if (!_renderer || _names.empty())
        return;
    if (ev.type == EventType::kKeyDown) {
        const int n = static_cast<int>(_names.size());
        int i = _index;
        if (ev.key == key::kLeft)
            i = (i + n - 1) % n;
        else if (ev.key == key::kRight)
            i = (i + 1) % n;
        else if (ev.key == key::kPageUp)
            i = std::max(0, i - 10);
        else if (ev.key == key::kPageDown)
            i = std::min(n - 1, i + 10);
        else if (ev.key == key::kHome)
            i = 0;
        else if (ev.key == key::kEnd)
            i = n - 1;
        else if (ev.key == key::kUp || ev.key == key::kDown) {
            // The first model of the next (or previous) kind.
            const int step = ev.key == key::kDown ? 1 : n - 1;
            const std::string kind = kindOf(_names[size_t(i)]);
            do
                i = (i + step) % n;
            while (i != _index && kindOf(_names[size_t(i)]) == kind);
            if (ev.key == key::kUp)
                while (i > 0 && kindOf(_names[size_t(i - 1)]) == kindOf(_names[size_t(i)]))
                    --i;
        } else if (ev.key == 'l' && _levels.size() > 1) {
            _level = (_level + 1) % int(_levels.size());
            build();
        } else if (ev.key == 'p' && _pkg.paintJobs > 1) {
            _paintJob = (_paintJob + 1) % _pkg.paintJobs;
            build();
        } else if (ev.key == 'g') {
            _showLights = !_showLights;
            build();
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            _showInfo = !_showInfo;
        } else if (ev.key == '+' || ev.key == '=') {
            _distance = std::max(_radius * 0.2f, _distance * 0.9f);
        } else if (ev.key == '-') {
            _distance /= 0.9f;
        }
        if (i != _index)
            show(i);
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::max(_radius * 0.2f, _distance * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::drawInfo() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    const int lh = font::kLineHeight;
    std::vector<std::pair<std::string, Color>> lines;
    const Color white{230, 230, 230}, blue{150, 200, 255}, red{255, 120, 120};
    lines.push_back({_names[size_t(_index)] + "  (" + std::to_string(_index + 1) + "/" +
                         std::to_string(_names.size()) + ")",
                     blue});
    if (!_error.empty()) {
        lines.push_back({_error, red});
    } else if (_showInfo) {
        std::string lv;
        for (size_t i = 0; i < _levels.size(); ++i)
            lv += (i ? " " : "") + (int(i) == _level ? "[" + _levels[i] + "]" : _levels[i]);
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "level %s (L), paint job %u of %u (P), lit parts %s (G); %zu vertices, %zu triangles",
                      lv.empty() ? "-" : lv.c_str(), _paintJob + 1, std::max(_pkg.paintJobs, 1u),
                      _showLights ? "shown" : "hidden", _mesh.vertices, _mesh.triangles);
        lines.push_back({buf, white});
        std::string parts;
        for (const PkgMesh& m : _pkg.meshes)
            if (isLightPart(m.part()) ? _showLights
                                      : _levels.empty() || m.level() == _levels[size_t(_level)] || !isLevel(m.level()))
                parts += (parts.empty() ? "" : " ") + m.name;
        lines.push_back({"meshes: " + parts, white});
        std::snprintf(buf, sizeof buf, "%u shaders a paint job; %zu placed models", _pkg.shadersPerJob,
                      _pkg.xrefs.size());
        lines.push_back({buf, white});
        for (const PkgXref& x : _pkg.xrefs)
            if (lines.size() < 12)
                lines.push_back({"  " + x.name + " at " + std::to_string(x.matrix[9]) + " " +
                                     std::to_string(x.matrix[10]) + " " + std::to_string(x.matrix[11]),
                                 white});
        for (const std::string& m : _textures->missing())
            lines.push_back({"missing texture: " + m, red});
    }
    int y = 4;
    for (const auto& [line, color] : lines) {
        if (y > kFrameH - lh)
            break;
        font::drawText(_overlay, 4, y, line, color);
        y += lh;
    }
}

void ModelViewer::update(uint64_t) {
    if (!_renderer)
        return;
    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{_center[0] + offset[0] * _distance, _center[1] + offset[1] * _distance,
                        _center[2] + offset[2] * _distance};
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), std::max(0.01f, _radius * 0.01f), _radius * 50),
        gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});
    // Opaque first, then the translucent over them.
    const SceneLight light;
    drawModelMesh(*_renderer, _mesh, gfx::identity(), light, false);
    drawModelMesh(*_renderer, _mesh, gfx::identity(), light, true);
    drawInfo();
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::age
