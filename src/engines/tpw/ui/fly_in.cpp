// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/fly_in.h"

#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/park_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr const char *kPath = "/obstacles/Camera/camera.MD2";
constexpr const char *kAnimation = "cameram.MD2";
// The spline's node: a type the models use for it alone.
constexpr uint32_t kSplineNode = 0x208;
// A knot and its two handles, in, then the knot, then out.
constexpr size_t kPerKnot = 3;
// As the front end's camera: a field of view 53 degrees high.
constexpr float kFocalPerHeight = 1.0f;
constexpr float kNear = 1;
// The camera holds on the gates so long before the briefing: from 1:47.5
// to 1:50.5 in the recording (inferred).
constexpr uint32_t kHoldMillis = 3000;
// How often the screen is drawn.
constexpr uint32_t kDrawMillis = 40;

gfx::Vec3 mix(const gfx::Vec3 &a, const gfx::Vec3 &b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

soft3d::Vec column(const gfx::Mat4 &m, int c) {
    return toViewerWorld({m[size_t(4 * c)], m[size_t(4 * c + 1)], m[size_t(4 * c + 2)]});
}

} // namespace

Error FlyIn::load(const SearchSet &files, const std::string &worldFolder) {
    if (Error err = _path.load(files, worldFolder + kPath))
        return err;
    const auto &nodes = _path.model.nodes();
    bool found = false;
    for (size_t i = 0; i < nodes.size() && !found; ++i)
        if (nodes[i].type == kSplineNode) {
            _spline = i;
            found = true;
        }
    if (!found || _path.model.spline().size() < 2 * kPerKnot)
        return Error::fail(worldFolder + kPath + " has no spline");
    // The dummy is the spline's child.
    found = false;
    for (size_t i = 0; i < nodes.size() && !found; ++i)
        if (nodes[i].parent == int(_spline)) {
            _dummy = i;
            found = true;
        }
    if (!found)
        return Error::fail(worldFolder + kPath + " has no camera on its spline");
    _animation = -1;
    for (size_t i = 0; i < _path.animationNames.size(); ++i)
        if (_path.animationNames[i] == kAnimation)
            _animation = int(i);
    if (_animation < 0)
        return Error::fail(worldFolder + kPath + " has no animation");
    return Error::success();
}

float FlyIn::frames() const {
    return _animation >= 0 ? float(_path.animations[size_t(_animation)].frames()) : 0;
}

// A cubic Bézier from a knot by its out handle and the next's in handle;
// u counts the stretches between knots.
gfx::Vec3 FlyIn::at(float u) const {
    const std::vector<gfx::Vec3> &points = _path.model.spline();
    const size_t knots = points.size() / kPerKnot;
    u = std::clamp(u, 0.0f, float(knots - 1));
    const size_t k = std::min(size_t(u), knots - 2);
    const float t = u - float(k);
    const gfx::Vec3 &p0 = points[k * kPerKnot + 1];
    const gfx::Vec3 &p1 = points[k * kPerKnot + 2];
    const gfx::Vec3 &p2 = points[(k + 1) * kPerKnot];
    const gfx::Vec3 &p3 = points[(k + 1) * kPerKnot + 1];
    const gfx::Vec3 a = mix(p0, p1, t);
    const gfx::Vec3 b = mix(p1, p2, t);
    const gfx::Vec3 c = mix(p2, p3, t);
    return mix(mix(a, b, t), mix(b, c, t), t);
}

// Each stretch between knots taken in the same time, as 3ds Max's path
// constraint goes without constant velocity: so the camera keeps up with
// the recording's, which an even speed falls behind (inferred).
gfx::Vec3 FlyIn::along(float frame) const {
    const float share = std::clamp(frame / std::max(1.0f, frames()), 0.0f, 1.0f);
    return at(share * float(_path.model.spline().size() / kPerKnot - 1));
}

soft3d::OrbitCamera FlyIn::camera(float frame, int w, int h) const {
    const std::vector<gfx::Mat4> nodes = nodeTransforms(_path, _animation, frame);
    soft3d::OrbitCamera camera({0, 0, 0}, 0, 0, 1, w, h);
    camera.eye = toViewerWorld(gfx::transformPoint(nodes[_spline], along(frame)));
    // The dummy looks along its own z, its y up.
    const gfx::Mat4 &turn = nodes[_dummy];
    camera.forward = soft3d::normalize(column(turn, 2));
    camera.up = soft3d::normalize(column(turn, 1));
    camera.right = soft3d::normalize(soft3d::cross(camera.forward, camera.up));
    camera.focal = float(h) * kFocalPerHeight;
    camera.nearZ = kNear;
    return camera;
}

float FlyIn::yaw(float frame) const {
    const gfx::Mat4 turn = nodeTransforms(_path, _animation, frame)[_dummy];
    // An orbit camera at yaw y looks along (−sin y, cos y) in x and z.
    return std::atan2(-turn[8], turn[10]);
}

bool playFlyIn(System &sys, const FlyIn &flyIn, ParkScreen &screen, Park &park) {
    Surface surface;
    surface.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    const uint64_t start = sys.getMillis();
    uint32_t ran = 0;
    const uint32_t length = uint32_t(flyIn.frames()) * FlyIn::kFrameMillis + kHoldMillis;
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if ((ev.type == EventType::kKeyDown && !ev.repeat) || ev.type == EventType::kMouseDown)
                return true;
        }
        const uint32_t ms = uint32_t(sys.getMillis() - start);
        if (ms >= length)
            return true;
        // The park's clock stands still; its objects move.
        screen.advanceScripts(park, ms - ran);
        ran = ms;
        const float frame = std::min(float(ms) / float(FlyIn::kFrameMillis), flyIn.frames());
        screen.drawScene(surface, rasterizer, park, flyIn.camera(frame, surface.w, surface.h), flyIn.yaw(frame));
        sys.updateScreen(surface);
        sys.delayMillis(kDrawMillis);
    }
}

} // namespace reluva::tpw
