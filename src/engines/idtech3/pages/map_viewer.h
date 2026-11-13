// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"
#include "engines/idtech3/formats/bsp.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class MapViewer : public Page {
public:
    explicit MapViewer(PageContext &ctx);
    const char *name() const override { return "Maps"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Edge {
        std::array<float, 3> a;
        std::array<float, 3> b;
        Color color;
        bool patch;
    };
    // The orbit camera's basis, rebuilt by render().
    struct Camera {
        std::array<float, 3> pos{};
        std::array<float, 3> right{};
        std::array<float, 3> up{};
        std::array<float, 3> forward{};
        float cx = 0;
        float cy = 0;
        float focal = 0;
    };

    void load(int index);
    void resetView();
    void render();
    void drawLine3d(const std::array<float, 3> &a, const std::array<float, 3> &b, Color c);
    void drawLine2d(float x0, float y0, float x1, float y1, Color c);

    std::vector<std::string> _names;
    int _index = 0;
    Bsp _bsp;
    std::string _error; // why the map couldn't be shown
    std::vector<std::string> _info;
    std::vector<Edge> _edges;
    std::array<float, 3> _mins{};
    std::array<float, 3> _maxs{};

    // Orbit camera around `target_`.
    std::array<float, 3> _target{};
    float _yaw = 0;
    float _pitch = 0;
    float _distance = 0;
    bool _showPatches = true;
    bool _showInfo = true;
    bool _dragging = false;
    int _lastX = 0;
    int _lastY = 0;
    bool _dirty = true;
    Camera _cam;
};

} // namespace reluva::idtech3
