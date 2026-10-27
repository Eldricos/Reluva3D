// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/player_animation.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return entries_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    struct Entry {
        std::string name; // the .md3, or the player's folder
        bool player = false;
    };
    void show(int index);
    void resetView();

    std::vector<Entry> entries_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_;
    Surface overlay_;
    std::vector<std::string> info_;
    std::string error_;
    int model_ = -1;                     // a single model
    int lower_ = -1, upper_ = -1, head_ = -1; // a player's parts
    const Skin* skins_[3] = {};
    std::array<float, 3> center_{};
    float radius_ = 32;
    float yaw_ = 0, pitch_ = 0, distance_ = 100;
    bool dragging_ = false, animate_ = false, showInfo_ = true;
    bool torsoOnTag_ = true; // a player's torso placed on the legs' tag_torso (T toggles)
    int lastX_ = 0, lastY_ = 0;
    int frame_ = 0; // when not animating
    uint64_t startMs_ = 0, lastMs_ = 0;
    // A player's animations (animation.cfg) and the pair playing.
    std::vector<PlayerAnimation> anims_;
    int legsAnim_ = 0, torsoAnim_ = 0;
    float animTime_ = 0; // seconds into them
    void nextAnimation(int step);
};

} // namespace reluva::idtech3
