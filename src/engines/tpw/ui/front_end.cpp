// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/front_end.h"

#include "engines/tpw/ui/opening.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

// The camera's node, under `CAMERA1`, which the animation moves.
constexpr const char *kCameraNode = "Camera01";
// Its animation is number 5, the main one, as its script plays it.
constexpr const char *kAnimation = "cameram.MD2";
// The focal length, in screen heights: the recording's hall measured
// against ours, a field of view 53 degrees high.
constexpr float kFocalPerHeight = 1.0f;
constexpr float kNear = 1;

// Black where the scene has nothing, as below the hall's floor in the
// recording.
void clear(Surface &screen) {
    for (int y = 0; y < screen.h; ++y) {
        uint8_t *row = screen.row(y);
        for (int x = 0; x < screen.w; ++x) {
            row[4 * x] = row[4 * x + 1] = row[4 * x + 2] = 0;
            row[4 * x + 3] = 255;
        }
    }
}

// What a frame waits for the next, in milliseconds.
constexpr uint32_t kFrameMillis = 40;

soft3d::Vec column(const gfx::Mat4 &m, int c) {
    return toViewerWorld({m[size_t(4 * c)], m[size_t(4 * c + 1)], m[size_t(4 * c + 2)]});
}

} // namespace

Error FrontEndScene::load(const SearchSet &files) {
    if (Error err = _model.load(files, kModel))
        return err;
    const auto &nodes = _model.model.nodes();
    bool found = false;
    for (size_t i = 0; i < nodes.size() && !found; ++i)
        if (nodes[i].name == kCameraNode) {
            _camera = i;
            found = true;
        }
    if (!found)
        return Error::fail(std::string(kModel) + " has no camera (" + kCameraNode + ")");
    _animation = -1;
    for (size_t i = 0; i < _model.animationNames.size(); ++i)
        if (_model.animationNames[i] == kAnimation)
            _animation = int(i);
    return Error::success();
}

float FrontEndScene::frames() const {
    return _animation >= 0 ? float(_model.animations[size_t(_animation)].frames()) : 0;
}

soft3d::OrbitCamera FrontEndScene::camera(float frame, int w, int h) const {
    const gfx::Mat4 at = nodeTransforms(_model, _animation, frame)[_camera];
    soft3d::OrbitCamera camera({0, 0, 0}, 0, 0, 1, w, h);
    camera.eye = toViewerWorld(gfx::transformPoint(at, {0, 0, 0}));
    // A 3ds Max camera looks down its own -z, its y up; the game's files
    // swap y and z.
    camera.forward = soft3d::normalize(column(at, 1) * -1.0f);
    camera.up = soft3d::normalize(column(at, 2));
    camera.right = soft3d::normalize(soft3d::cross(camera.forward, camera.up));
    camera.focal = float(h) * kFocalPerHeight;
    camera.nearZ = kNear;
    return camera;
}

void FrontEndScene::draw(Surface &screen, soft3d::Rasterizer &rasterizer, float frame) const {
    draw(screen, rasterizer, camera(frame, screen.w, screen.h), meshTransforms(frame));
}

std::vector<gfx::Mat4> FrontEndScene::meshTransforms(float frame) const {
    return tpw::meshTransforms(_model, _animation, frame);
}

void FrontEndScene::draw(Surface &screen, soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera,
                         const std::vector<gfx::Mat4> &transforms) const {
    clear(screen);
    rasterizer.begin(screen);
    drawMeshes(rasterizer, camera, _model, true, defaultLight(), &transforms);
}

bool playFrontEnd(System &sys, const FrontEndScene &scene) {
    Surface screen;
    screen.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    const uint64_t start = sys.getMillis();
    bool skipped = false;
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if ((ev.type == EventType::kKeyDown && !ev.repeat) || ev.type == EventType::kMouseDown)
                skipped = true;
        }
        const float played = float(sys.getMillis() - start) * Animation::kFramesPerSecond / 1000;
        if (skipped || played >= scene.frames())
            return true;
        scene.draw(screen, rasterizer, played);
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
