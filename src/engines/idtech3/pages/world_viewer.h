// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/render/world_renderer.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class WorldViewer : public Page {
public:
    explicit WorldViewer(ViewerContext &ctx);
    ~WorldViewer() override;
    const char *name() const override { return "World"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }

private:
    void load(int index);
    void toStart();
    void drawOverlay(uint64_t nowMs);

    std::vector<std::string> _names;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;  // valid while this page is shown
    std::unique_ptr<ShaderCache> _shaders; // the next three draw through it
    std::unique_ptr<WorldRenderer> _world;
    std::unique_ptr<ModelCache> _models;
    LightGrid _grid;
    // Models the map's entities name, lit by the light grid where they stand.
    struct Placed {
        int model = -1;
        gfx::Mat4 transform{};
        PointLight light;
    };
    std::vector<Placed> _placed;
    Surface _overlay;
    std::string _error;
    std::vector<std::string> _info;
    std::array<float, 3> _start{};
    float _startYaw = 0;

    // Fly camera, in the map's units (Z up); yaw 0 looks along +X.
    std::array<float, 3> _pos{};
    float _yaw = 0;
    float _pitch = 0;
    bool _held[8] = {}; // forward, back, left, right, up, down, fast, (unused)
    bool _dragging = false;
    int _lastX = 0;
    int _lastY = 0;
    bool _showInfo = true;
    uint64_t _lastMs = 0;
    uint64_t _loadedMs = 0; // shader animations run from when the map was loaded
    uint64_t _fpsStart = 0;
    int _frames = 0;
    float _fps = 0;
    // Per second: average milliseconds per frame spent issuing draws, and
    // presenting (which includes waiting for vsync).
    double _drawSum = 0;
    double _presentSum = 0;
    float _drawMs = 0;
    float _presentMs = 0;
    std::string _device;
};

} // namespace reluva::idtech3
