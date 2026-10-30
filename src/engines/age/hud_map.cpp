// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The .mmHudMap fields are read from the files; how they're used (the
// field of view, icon sizes as lengths, the map turning with the car) is
// Reluva's reading. See docs/engines/age-driving.md.

#include "engines/age/hud_map.h"

#include "engines/age/car_actor.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kFov = 30;          // degrees, up and down: a guess
constexpr float kTriLength = 28;    // hudmap_tri, nose to tail
constexpr float kFullScreen = 0.9f; // full screen: this share of the frame's height, square
constexpr float kShown = 0.92f;     // of its Size the corner map is drawn

} // namespace

Error parseHudMapSetup(std::string_view text, HudMapSetup& out) {
    out = HudMapSetup{};
    TuneNode root;
    if (Error err = parseTune(text, root))
        return err;
    if (root.children.empty())
        return Error::fail("empty HUD map settings");
    const TuneNode& n = root.children.front();
    out.width = n.number("Size", 0, out.width);
    out.height = n.number("Size", 1, out.height);
    out.left = n.number("Pos", 0, out.left);
    out.top = n.number("Pos", 1, out.top);
    out.zoomIn = n.number("ZoomIn") != 0;
    // "Approach Rate" and "Ocean Color" are two words: the name's second
    // word comes as the first value.
    if (const TuneNode* a = n.child("Approach"); a && a->values.size() >= 2)
        out.approachRate = std::strtof(a->values[1].c_str(), nullptr);
    if (const TuneNode* o = n.child("Ocean"); o && o->values.size() >= 4)
        for (size_t k = 0; k < 3; ++k)
            out.ocean[k] = std::strtof(o->values[k + 1].c_str(), nullptr);
    out.zoomInDist = n.number("ZoomInDist", 0, out.zoomInDist);
    out.zoomOutDist = n.number("ZoomOutDist", 0, out.zoomOutDist);
    out.iconScaleMin = n.number("IconScaleMin", 0, out.iconScaleMin);
    out.iconScaleMax = n.number("IconScaleMax", 0, out.iconScaleMax);
    out.zoomInDistFS = n.number("ZoomInDistFS", 0, out.zoomInDistFS);
    out.zoomOutDistFS = n.number("ZoomOutDistFS", 0, out.zoomOutDistFS);
    out.iconScaleMinFS = n.number("IconScaleMinFS", 0, out.iconScaleMinFS);
    out.iconScaleMaxFS = n.number("IconScaleMaxFS", 0, out.iconScaleMaxFS);
    return Error::success();
}

bool HudMap::load(gfx::Renderer3D& renderer, const SearchSet& files, ModelCache& models, const std::string& map) {
    unload();
    renderer_ = &renderer;
    std::vector<uint8_t> data;
    if (!files.read("tune/" + map + ".mmHudMap", data))
        parseHudMapSetup(std::string(data.begin(), data.end()), setup_);
    else
        setup_ = HudMapSetup{};
    model_ = "hudmap_" + map;
    zoomIn_ = setup_.zoomIn;
    full_ = false;
    distance_ = targetDistance();
    // A square, 1 across, for the ocean under the map.
    std::vector<gfx::Vertex> v(4);
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (size_t i = 0; i < 4; ++i) {
        v[i].pos[0] = corners[i][0];
        v[i].pos[2] = corners[i][1];
        v[i].normal[1] = 1;
        v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
    }
    renderer.createMesh(v, {0, 1, 2, 0, 2, 3}, quad_);
    ok_ = models.get(model_).ok;
    return ok_;
}

void HudMap::unload() {
    if (renderer_ && quad_)
        renderer_->destroyMesh(quad_);
    quad_ = 0;
    renderer_ = nullptr;
    ok_ = false;
}

float HudMap::targetDistance() const {
    if (full_)
        return zoomIn_ ? setup_.zoomInDistFS : setup_.zoomOutDistFS;
    return zoomIn_ ? setup_.zoomInDist : setup_.zoomOutDist;
}

float HudMap::iconLength() const {
    // From the near height's length to the far's.
    const float nearD = full_ ? setup_.zoomInDistFS : setup_.zoomInDist;
    const float farD = full_ ? setup_.zoomOutDistFS : setup_.zoomOutDist;
    const float lo = full_ ? setup_.iconScaleMinFS : setup_.iconScaleMin;
    const float hi = full_ ? setup_.iconScaleMaxFS : setup_.iconScaleMax;
    const float t = farD > nearD ? std::clamp((distance_ - nearD) / (farD - nearD), 0.0f, 1.0f) : 0;
    return lo + (hi - lo) * t;
}

void HudMap::update(float dt) {
    distance_ += (targetDistance() - distance_) * std::min(1.0f, setup_.approachRate * dt);
}

void HudMap::draw(gfx::Renderer3D& renderer, ModelCache& models, const Vec3& center, const Vec3& forward,
                  const std::vector<HudMapIcon>& icons, int frameW, int frameH) const {
    if (!ok_)
        return;
    int x, y, w, h;
    if (full_) {
        h = int(float(frameH) * kFullScreen);
        w = h;
        x = (frameW - w) / 2;
        y = (frameH - h) / 2;
    } else {
        x = int(setup_.left * float(frameW));
        y = int(setup_.top * float(frameH));
        // The game draws it a little smaller than its Size: 124 x 110 of
        // 0.21 x 0.25 at 640 x 480, 245 x 165 at 1280 x 720 (from its own
        // screens).
        w = std::min(frameW - x, int(setup_.width * kShown * float(frameW)));
        h = std::min(frameH - y, int(setup_.height * kShown * float(frameH)));
    }
    if (w <= 0 || h <= 0)
        return;
    // From above, the way the player faces up the map.
    Vec3 up{forward[0], 0, forward[2]};
    up = length(up) > 1e-4f ? normalized(up) : Vec3{0, 0, -1};
    const Vec3 eye{center[0], distance_, center[2]};
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(kFov * 3.14159265f / 180, float(w) / float(h), 1, 1e5f),
                                             gfx::lookAlong(eye, {0, -1, 0}, up));
    renderer.clearDepth();
    renderer.setView(viewProj, eye);
    renderer.setViewport(x, y, w, h);
    auto flat = [&](gfx::MeshId mesh, const ModelDraw& d, const gfx::Mat4& model, const std::array<float, 4>& color) {
        gfx::DrawCall call;
        call.mesh = mesh;
        call.firstIndex = d.firstIndex;
        call.indexCount = d.indexCount;
        call.model = model;
        call.texture = d.texture;
        call.color = color;
        call.state.depthTest = false;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::None;
        renderer.draw(call);
    };
    // The ocean, as far as can be seen, then the map.
    ModelDraw whole;
    whole.indexCount = 6;
    const float reach = distance_ * 4;
    flat(quad_, whole, matrixOf(Mat3{{reach, 0, 0}, {0, 1, 0}, {0, 0, reach}}, {center[0], 0, center[2]}),
         {setup_.ocean[0], setup_.ocean[1], setup_.ocean[2], 1});
    const ModelCache::Model& map = models.get(model_);
    for (const ModelDraw& d : map.levels.front().draws)
        flat(map.levels.front().mesh, d, gfx::identity(), d.color);
    // The icons, the dots under the cars.
    const float scale = iconLength() / kTriLength;
    for (bool dots : {true, false})
        for (const HudMapIcon& icon : icons) {
            if (icon.dot != dots)
                continue;
            const ModelCache::Model& m = models.get(dots ? "hudmap_square" : "hudmap_tri", uint32_t(icon.color));
            if (!m.ok)
                continue;
            Vec3 f{icon.forward[0], 0, icon.forward[2]};
            f = length(f) > 1e-4f ? normalized(f) : Vec3{0, 0, -1};
            // Its -z along `forward`, flat.
            const Mat3 turn{Vec3{-f[2], 0, f[0]} * scale, Vec3{0, 1, 0} * scale, f * -scale};
            const gfx::Mat4 at = matrixOf(turn, {icon.position[0], 1, icon.position[2]});
            for (const ModelDraw& d : m.levels.front().draws)
                flat(m.levels.front().mesh, d, at, d.color);
        }
    renderer.setViewport(0, 0, 0, 0);
}

} // namespace reluva::age
