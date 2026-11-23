// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/ui/model_view.h"
#include "graphics/soft3d/rasterizer.h"
#include "graphics/soft3d/view3d.h"

#include <vector>

namespace reluva::tpw {

class FrontEndEffects;

/**
 * The front end's scene: the Intro World's camera path (`Camera Path`,
 * `levels/wIntro/obstacles/Camera/camera.MD2`), its hall, boardroom and
 * globe in one model, seen through its own camera as its main animation
 * moves it. See docs/engines/tpw/games/simcoaster/systems/front-end.md.
 */
class FrontEndScene {
public:
    static constexpr const char *kModel = "levels/wIntro/obstacles/Camera/camera.MD2";

    /** Reads the model, its textures and its animation. Fails without the model or its camera. */
    Error load(const SearchSet &files);

    const LoadedModel &model() const { return _model; }
    /** The animation's length, in frames; 0 without it. */
    float frames() const;

    /** The model's camera at `frame` of its animation, for a screen of w × h. */
    soft3d::OrbitCamera camera(float frame, int w, int h) const;

    /** Draws the scene at `frame` over the whole of an RGBA32 surface. */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, float frame) const;

    /** Each node's place at `frame` of the animation (nodeTransforms'), by Model::nodes(). */
    std::vector<gfx::Mat4> nodeTransforms(float frame) const;
    /** Each mesh's place at `frame` of the animation (meshTransforms'). */
    std::vector<gfx::Mat4> meshTransforms(float frame) const;
    /** Each mesh's positions at `frame` of the animation (meshPositions'). */
    std::vector<std::vector<gfx::Vec3>> meshPositions(float frame) const;

    /**
     * Draws the scene over the whole of an RGBA32 surface, its meshes at
     * `transforms` with their positions `positions`, through `camera`.
     */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera,
              const std::vector<gfx::Mat4> &transforms, const std::vector<std::vector<gfx::Vec3>> &positions) const;

private:
    LoadedModel _model;
    int _animation = -1; // in _model.animations
    size_t _camera = 0;  // in _model.model.nodes()
};

/**
 * The front end's opening, as the recording shows it: the camera's path
 * through the hall into the boardroom and down to the globe, at the
 * animation's rate; a key or a click skips to its end, as the game's skip
 * button does; with `effects` (loaded for `scene`), the particles its
 * script starts over it.
 * @return False if the window was closed (or the launcher asked for) meanwhile.
 */
bool playFrontEnd(System &sys, const FrontEndScene &scene, FrontEndEffects *effects = nullptr);

} // namespace reluva::tpw
