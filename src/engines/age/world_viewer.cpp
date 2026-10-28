// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's World page: a city (CityScene) flown through freely.

#include "engines/age/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kFov = 1.0f; // radians, vertically
constexpr float kFar = 4000; // metres
constexpr float kSpeed = 40; // metres a second, four times with Shift

enum : uint32_t { kMoveForward = 1, kMoveBack = 2, kMoveLeft = 4, kMoveRight = 8, kMoveUp = 16, kMoveDown = 32,
                  kMoveFast = 64 };

} // namespace

WorldViewer::WorldViewer(ViewerContext& ctx) : Page(ctx), cities_(findCities(ctx.files)) {}

WorldViewer::~WorldViewer() { deactivate(); }

void WorldViewer::deactivate() {
    scene_.reset();
    renderer_ = nullptr;
}

void WorldViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The World page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    scene_ = std::make_unique<CityScene>(*renderer_, ctx_.files);
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    load(index_);
}

void WorldViewer::load(int index) {
    index_ = index;
    const CityInfo& city = cities_[size_t(index)];
    setTitle(city.name + " (" + city.map + ")");
    error_ = scene_->load(city.map).message;
    scene_->setLighting(lighting_);
    resetView();
}

void WorldViewer::resetView() {
    // A car's height above the start, facing along the road.
    eye_ = {scene_->start[0], scene_->start[1] + 2.5f, scene_->start[2]};
    yaw_ = scene_->startYaw;
    pitch_ = -0.05f;
}

void WorldViewer::handleEvent(const Event& ev) {
    if (!renderer_ || cities_.empty())
        return;
    auto moveKey = [](uint32_t k) -> uint32_t {
        switch (k) {
        case 'w': return kMoveForward;
        case 's': return kMoveBack;
        case 'a': return kMoveLeft;
        case 'd': return kMoveRight;
        case ' ':
        case 'e': return kMoveUp;
        case 'c':
        case 'q': return kMoveDown;
        case key::LShift:
        case key::RShift: return kMoveFast;
        default: return 0;
        }
    };
    if (ev.type == EventType::KeyDown) {
        if (const uint32_t m = moveKey(ev.key)) {
            keys_ |= m;
            return;
        }
        const int n = int(cities_.size());
        if (ev.key == key::Left || ev.key == key::Right) {
            load((index_ + (ev.key == key::Right ? 1 : n - 1)) % n);
        } else if (ev.key == 't') {
            lighting_ = (lighting_ + 1) % 16;
            scene_->setLighting(lighting_);
        } else if (ev.key == 'm') {
            showModels_ = !showModels_;
        } else if (ev.key == 'v') {
            scene_->setVisibility(!scene_->visibility());
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        } else if (ev.key == 'r') {
            resetView();
        }
    } else if (ev.type == EventType::KeyUp) {
        keys_ &= ~moveKey(ev.key);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.005f;
        pitch_ = std::clamp(pitch_ - float(ev.mouseY - lastY_) * 0.005f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    }
}

void WorldViewer::drawInfo(const std::string& stats) {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const CityInfo& city = cities_[size_t(index_)];
    font::drawText(overlay_, 4, 4,
                   city.name + "  (Left/Right: city; " + scene_->lightingName() + ", T: time and weather)",
                   {150, 200, 255});
    int y = 4 + lh;
    if (!error_.empty()) {
        font::drawText(overlay_, 4, y, error_, {255, 120, 120});
        return;
    }
    if (!showInfo_)
        return;
    std::vector<std::string> lines = scene_->notes();
    lines.push_back(stats);
    char pos[160];
    std::snprintf(pos, sizeof pos, "at %.0f %.0f %.0f; W/A/S/D, Space/C, Shift, drag to look; M models, R start",
                  eye_[0], eye_[1], eye_[2]);
    lines.push_back(pos);
    if (scene_->viewBlock())
        std::snprintf(pos, sizeof pos, "in block %zu: %zu blocks can be seen (V: visibility off)", scene_->viewBlock(),
                      scene_->blocksDrawn());
    else
        std::snprintf(pos, sizeof pos, "%zu blocks drawn (V: visibility %s)", scene_->blocksDrawn(),
                      scene_->visibility() ? "on, but not in a block" : "off");
    lines.push_back(pos);
    for (const std::string& m : scene_->textures().missing())
        if (lines.size() < 12)
            lines.push_back("missing texture: " + m);
    for (const std::string& line : lines) {
        font::drawText(overlay_, 4, y, line, {230, 230, 230});
        y += lh;
    }
}

void WorldViewer::update(uint64_t nowMs) {
    if (!renderer_ || cities_.empty())
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;
    const gfx::Vec3 forward{-std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), -std::cos(pitch_) * std::cos(yaw_)};
    const gfx::Vec3 right{std::cos(yaw_), 0, -std::sin(yaw_)};
    const float step = kSpeed * (keys_ & kMoveFast ? 4 : 1) * dt;
    for (size_t k = 0; k < 3; ++k) {
        eye_[k] += forward[k] * step * (float(bool(keys_ & kMoveForward)) - float(bool(keys_ & kMoveBack)));
        eye_[k] += right[k] * step * (float(bool(keys_ & kMoveRight)) - float(bool(keys_ & kMoveLeft)));
    }
    eye_[1] += step * (float(bool(keys_ & kMoveUp)) - float(bool(keys_ & kMoveDown)));

    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(kFov, float(kFrameW) / float(kFrameH), 0.5f, kFar),
                                             gfx::lookAlong(eye_, forward, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye_, {150, 170, 200});
    const size_t drawn = scene_->draw(viewProj, eye_, showModels_);
    char stats[80];
    std::snprintf(stats, sizeof stats, "%zu of %zu placed models drawn", drawn, scene_->placedCount());
    drawInfo(stats);
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
