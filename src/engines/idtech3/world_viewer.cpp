// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's World page: a map drawn by WorldRenderer on the GPU,
// with a fly camera starting where the player does.

#include "engines/idtech3/entities.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kSpeed = 320;    // units per second, the original's running speed
constexpr float kEyeHeight = 26; // above a player start's origin

enum Held { Forward, Back, Left, Right, Up, Down, Fast };

} // namespace

WorldViewer::WorldViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            names_.push_back(n);
    }
}

WorldViewer::~WorldViewer() { deactivate(); }

void WorldViewer::deactivate() {
    // The renderer's resources go with it when the engine leaves 3D.
    world_.reset();
    renderer_ = nullptr;
}

void WorldViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        // No 3D here: say so on the 2D framebuffer.
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The World page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        text(4, 4 + 3 * font::kLineHeight, "Tab: the other pages still work.");
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    device_ = renderer_->deviceName();
    world_ = std::make_unique<WorldRenderer>(*renderer_);
    load(index_);
}

void WorldViewer::load(int index) {
    index_ = index;
    error_.clear();
    info_.clear();
    const std::string& name = names_[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");

    std::vector<uint8_t> data;
    Bsp bsp;
    Error err = ctx_.files.read(name, data);
    if (!err)
        err = parseBsp(data, bsp);
    if (!err)
        err = world_->load(bsp, ctx_.files, ctx_.shaders);
    if (err) {
        error_ = err.message;
        world_->clear();
    }
    loadedMs_ = ctx_.sys.getMillis();

    // Start where the player does, or above the middle of the map.
    start_ = {0, 0, 0};
    startYaw_ = 0;
    if (!bsp.models.empty())
        for (int i = 0; i < 3; ++i)
            start_[size_t(i)] = (bsp.models[0].mins[size_t(i)] + bsp.models[0].maxs[size_t(i)]) / 2;
    std::vector<Entity> entities;
    if (!parseEntities(bsp.entities, entities)) {
        const Entity* start = nullptr;
        for (const char* cls : {"info_player_start", "info_player_deathmatch"}) {
            for (const Entity& e : entities)
                if (e.classname() == cls) {
                    start = &e;
                    break;
                }
            if (start)
                break;
        }
        float origin[3];
        if (start && start->vector("origin", origin)) {
            start_ = {origin[0], origin[1], origin[2] + kEyeHeight};
            startYaw_ = std::strtof(start->get("angle", "0").c_str(), nullptr) * kPi / 180;
        }
    }
    toStart();

    info_.push_back(name);
    if (error_.empty()) {
        char line[160];
        std::snprintf(line, sizeof line, "%zu triangles in %zu batches, %zu images (%d missing), %zu lightmaps",
                      world_->triangleCount(), world_->batchCount(), world_->textureCount(), world_->missingImages(),
                      bsp.lightmapCount());
        info_.push_back(line);
        // What the map's shaders use that isn't drawn yet.
        std::string missing;
        for (const auto& [feature, n] : world_->unsupported())
            missing += (missing.empty() ? "" : ", ") + feature + (n > 1 ? " x" + std::to_string(n) : "");
        if (!missing.empty())
            info_.push_back("Not drawn yet: " + missing);
    }
}

void WorldViewer::toStart() {
    pos_ = start_;
    yaw_ = startYaw_;
    pitch_ = 0;
}

void WorldViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    auto hold = [&](uint32_t k, bool down) {
        switch (k) {
        case 'w': held_[Forward] = down; return true;
        case 's': held_[Back] = down; return true;
        case 'a': held_[Left] = down; return true;
        case 'd': held_[Right] = down; return true;
        case key::Space: case 'e': held_[Up] = down; return true;
        case 'c': case 'q': held_[Down] = down; return true;
        case key::LShift: case key::RShift: held_[Fast] = down; return true;
        default: return false;
        }
    };
    if (ev.type == EventType::KeyUp) {
        hold(ev.key, false);
    } else if (ev.type == EventType::KeyDown) {
        if (hold(ev.key, true) || ev.repeat)
            return;
        const int n = static_cast<int>(names_.size());
        if (ev.key == key::Left || ev.key == key::Right)
            load((index_ + (ev.key == key::Left ? n - 1 : 1)) % n);
        else if (ev.key == 'r')
            toStart();
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.005f;
        pitch_ = std::clamp(pitch_ - float(ev.mouseY - lastY_) * 0.005f, -1.55f, 1.55f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    }
}

void WorldViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;

    const gfx::Vec3 forward{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 right{std::sin(yaw_), -std::cos(yaw_), 0};
    const float step = kSpeed * (held_[Fast] ? 3.0f : 1.0f) * dt;
    for (int i = 0; i < 3; ++i) {
        const size_t k = size_t(i);
        pos_[k] += forward[k] * step * float(held_[Forward] - held_[Back]);
        pos_[k] += right[k] * step * float(held_[Right] - held_[Left]);
    }
    pos_[2] += step * float(held_[Up] - held_[Down]);

    // 90 degrees across, like the original's default.
    const float aspect = float(kFrameW) / float(kFrameH);
    const float fovY = 2 * std::atan(std::tan(kPi / 4) / aspect);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 32768), gfx::lookAlong(pos_, forward, {0, 0, 1}));
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    renderer_->beginFrame(viewProj, pos_, {40, 48, 64});
    world_->draw(float(nowMs - loadedMs_) / 1000.0f);
    drawOverlay(nowMs);
    const auto t1 = Clock::now();
    renderer_->endFrame(&overlay_);
    const auto t2 = Clock::now();
    drawSum_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
    presentSum_ += std::chrono::duration<double, std::milli>(t2 - t1).count();
}

void WorldViewer::drawOverlay(uint64_t nowMs) {
    ++frames_;
    if (nowMs - fpsStart_ >= 1000) {
        fps_ = float(frames_) * 1000.0f / float(nowMs - fpsStart_);
        drawMs_ = float(drawSum_ / frames_);
        presentMs_ = float(presentSum_ / frames_);
        fpsStart_ = nowMs;
        frames_ = 0;
        drawSum_ = presentSum_ = 0;
    }
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (!showInfo_ && error_.empty())
        return;
    std::vector<std::string> lines = info_;
    const size_t errorLine = error_.empty() ? size_t(-1) : lines.size();
    if (!error_.empty())
        lines.push_back(error_);
    char where[160];
    std::snprintf(where, sizeof where, "at %.0f %.0f %.0f, %.0f fps: %.1f ms drawing, %.1f ms presenting", pos_[0],
                  pos_[1], pos_[2], fps_, drawMs_, presentMs_);
    lines.push_back(where);
    lines.push_back("GPU: " + device_);
    // A translucent box behind the text keeps it readable.
    const int lh = font::kLineHeight;
    size_t widest = 0;
    for (const std::string& l : lines)
        widest = std::max(widest, font::length(l));
    const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
    for (int y = 0; y < boxH; ++y) {
        uint8_t* p = overlay_.row(y);
        for (int x = 0; x < boxW; ++x)
            p[x * 4 + 3] = 150;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
}

} // namespace reluva::idtech3
