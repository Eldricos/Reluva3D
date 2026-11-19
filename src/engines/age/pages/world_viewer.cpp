// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's World page: a city (CityScene) flown through freely.

#include "engines/age/pages/viewers.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr float kFov = 1.0f; // radians, vertically
constexpr float kFar = 4000; // metres
constexpr float kSpeed = 40; // metres a second, four times with Shift

enum : uint32_t {
    kMoveForward = 1,
    kMoveBack = 2,
    kMoveLeft = 4,
    kMoveRight = 8,
    kMoveUp = 16,
    kMoveDown = 32,
    kMoveFast = 64
};

} // namespace

WorldViewer::WorldViewer(ScreenContext &ctx) : Page(ctx), _cities(findCities(ctx.files)) {
}

WorldViewer::~WorldViewer() {
    deactivate();
}

void WorldViewer::deactivate() {
    _scene.reset();
    _renderer = nullptr;
}

void WorldViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The World page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _scene = std::make_unique<CityScene>(*_renderer, _ctx.files);
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    load(_index);
}

void WorldViewer::load(int index) {
    _index = index;
    const CityInfo &city = _cities[size_t(index)];
    setTitle(city.name + " (" + city.map + ")");
    _scene->textures().setHalvings(textureHalvings());
    _error = _scene->load(city.map).message;
    _scene->setLighting(_lighting);
    resetView();
}

void WorldViewer::resetView() {
    // A car's height above the start, facing along the road.
    _eye = {_scene->start[0], _scene->start[1] + 2.5f, _scene->start[2]};
    _yaw = _scene->startYaw;
    _pitch = -0.05f;
}

void WorldViewer::handleEvent(const Event &ev) {
    if (!_renderer || _cities.empty())
        return;
    auto moveKey = [](uint32_t k) -> uint32_t {
        switch (k) {
        case 'w':
            return kMoveForward;
        case 's':
            return kMoveBack;
        case 'a':
            return kMoveLeft;
        case 'd':
            return kMoveRight;
        case ' ':
        case 'e':
            return kMoveUp;
        case 'c':
        case 'q':
            return kMoveDown;
        case key::kLShift:
        case key::kRShift:
            return kMoveFast;
        default:
            return 0;
        }
    };
    if (ev.type == EventType::kKeyDown) {
        if (const uint32_t m = moveKey(ev.key)) {
            _keys |= m;
            return;
        }
        const int n = int(_cities.size());
        if (ev.key == key::kLeft || ev.key == key::kRight) {
            load((_index + (ev.key == key::kRight ? 1 : n - 1)) % n);
        } else if (ev.key == 't') {
            _lighting = (_lighting + 1) % 16;
            _scene->setLighting(_lighting);
        } else if (ev.key == 'm') {
            _showModels = !_showModels;
        } else if (ev.key == 'v') {
            _scene->setVisibility(!_scene->visibility());
        } else if (ev.key == 'i') {
            _showInfo = !_showInfo;
        } else if (ev.key == 'r') {
            resetView();
        }
    } else if (ev.type == EventType::kKeyUp) {
        _keys &= ~moveKey(ev.key);
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.005f;
        _pitch = std::clamp(_pitch - float(ev.mouseY - _lastY) * 0.005f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    }
}

void WorldViewer::drawInfo(const std::string &stats) {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const CityInfo &city = _cities[size_t(_index)];
    font::drawText(_overlay, 4, 4,
                   city.name + "  (Left/Right: city; " + _scene->lightingName() + ", T: time and weather)",
                   {150, 200, 255});
    int y = 4 + lh;
    if (!_error.empty()) {
        font::drawText(_overlay, 4, y, _error, {255, 120, 120});
        return;
    }
    if (!_showInfo)
        return;
    std::vector<std::string> lines = _scene->notes();
    lines.push_back(stats);
    char pos[160];
    std::snprintf(pos, sizeof pos, "at %.0f %.0f %.0f; W/A/S/D, Space/C, Shift, drag to look; M models, R start",
                  _eye[0], _eye[1], _eye[2]);
    lines.push_back(pos);
    if (_scene->viewBlock())
        std::snprintf(pos, sizeof pos, "in block %zu: %zu blocks can be seen (V: visibility off)", _scene->viewBlock(),
                      _scene->blocksDrawn());
    else
        std::snprintf(pos, sizeof pos, "%zu blocks drawn (V: visibility %s)", _scene->blocksDrawn(),
                      _scene->visibility() ? "on, but not in a block" : "off");
    lines.push_back(pos);
    for (const std::string &m : _scene->textures().missing())
        if (lines.size() < 12)
            lines.push_back("missing texture: " + m);
    for (const std::string &line : lines) {
        font::drawText(_overlay, 4, y, line, {230, 230, 230});
        y += lh;
    }
}

void WorldViewer::update(uint64_t nowMs) {
    if (!_renderer || _cities.empty())
        return;
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000) : 0;
    _lastMs = nowMs;
    const gfx::Vec3 forward{-std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), -std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 right{std::cos(_yaw), 0, -std::sin(_yaw)};
    const float step = kSpeed * (_keys & kMoveFast ? 4 : 1) * dt;
    for (size_t k = 0; k < 3; ++k) {
        _eye[k] += forward[k] * step * (float(bool(_keys & kMoveForward)) - float(bool(_keys & kMoveBack)));
        _eye[k] += right[k] * step * (float(bool(_keys & kMoveRight)) - float(bool(_keys & kMoveLeft)));
    }
    _eye[1] += step * (float(bool(_keys & kMoveUp)) - float(bool(_keys & kMoveDown)));

    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(kFov, float(kFrameW) / float(kFrameH), 0.5f, kFar),
                                             gfx::lookAlong(_eye, forward, {0, 1, 0}));
    _renderer->beginFrame(viewProj, _eye, {150, 170, 200});
    const size_t drawn = _scene->draw(viewProj, _eye, _showModels);
    char stats[80];
    std::snprintf(stats, sizeof stats, "%zu of %zu placed models drawn", drawn, _scene->placedCount());
    drawInfo(stats);
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::age
