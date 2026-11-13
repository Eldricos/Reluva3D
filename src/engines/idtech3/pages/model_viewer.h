// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/formats/player_animation.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext &ctx);
    ~ModelViewer() override;
    const char *name() const override { return "Models"; }
    size_t count() const override { return _entries.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }

private:
    struct Entry {
        std::string name; // the .md3, or the player's folder
        bool player = false;
    };
    void show(int index);
    void resetView();

    std::vector<Entry> _entries;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::unique_ptr<ShaderCache> _shaders;
    std::unique_ptr<ModelCache> _models;
    Surface _overlay;
    std::vector<std::string> _info;
    std::string _error;
    int _model = -1;                          // a single model
    int _lower = -1, _upper = -1, _head = -1; // a player's parts
    const Skin *_skins[3] = {};
    std::array<float, 3> _center{};
    float _radius = 32;
    float _yaw = 0, _pitch = 0, _distance = 100;
    bool _dragging = false, _animate = false, _showInfo = true;
    bool _torsoOnTag = true; // a player's torso placed on the legs' tag_torso (T toggles)
    int _lastX = 0, _lastY = 0;
    int _frame = 0; // when not animating
    uint64_t _startMs = 0, _lastMs = 0;
    // A player's animations (animation.cfg) and the pair playing.
    std::vector<PlayerAnimation> _anims;
    int _legsAnim = 0, _torsoAnim = 0;
    float _animTime = 0; // seconds into them
    void nextAnimation(int step);
};

} // namespace reluva::idtech3
