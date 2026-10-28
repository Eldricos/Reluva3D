// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: a model drawn with the GPU renderer, its
// parts placed by their .mtx files, lit by a fixed light.

#include "engines/age/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;

bool isLevel(const std::string& level) { return level == "H" || level == "M" || level == "L" || level == "VL"; }

} // namespace

ModelViewer::ModelViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("geometry/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".pkg") == 0)
            names_.push_back(n);
    }
}

ModelViewer::~ModelViewer() { deactivate(); }

void ModelViewer::deactivate() {
    if (renderer_)
        destroyModelMesh(*renderer_, mesh_);
    textures_.reset();
    renderer_ = nullptr;
}

void ModelViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    textures_ = std::make_unique<TextureCache>(*renderer_, ctx_.files);
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    show(index_);
}

void ModelViewer::show(int index) {
    index_ = index;
    const std::string& name = names_[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");
    destroyModelMesh(*renderer_, mesh_);
    textures_->clear();
    error_.clear();
    level_ = 0;
    paintJob_ = 0;
    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(name, data))
        error_ = err.message;
    else if (Error e = readPkg(data, pkg_))
        error_ = e.message;
    if (!error_.empty()) {
        pkg_ = Pkg{};
        levels_.clear();
        return;
    }
    levels_ = modelLevels(pkg_);
    build();
    // Frame the finest level.
    center_ = mesh_.center;
    radius_ = std::max(0.1f, mesh_.radius);
    resetView();
}

void ModelViewer::build() {
    destroyModelMesh(*renderer_, mesh_);
    const std::string level = levels_.empty() ? std::string() : levels_[size_t(level_)];
    if (Error err = buildModelMesh(*renderer_, *textures_, ctx_.files, names_[size_t(index_)], pkg_, level, paintJob_,
                                   showLights_, mesh_))
        error_ = err.message;
}

void ModelViewer::resetView() {
    // From the front left, a little above: the cars face -z.
    yaw_ = -2.4f;
    pitch_ = 0.3f;
    distance_ = radius_ * 2.4f;
}

void ModelViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int n = static_cast<int>(names_.size());
        int i = index_;
        if (ev.key == key::Left)
            i = (i + n - 1) % n;
        else if (ev.key == key::Right)
            i = (i + 1) % n;
        else if (ev.key == key::PageUp)
            i = std::max(0, i - 10);
        else if (ev.key == key::PageDown)
            i = std::min(n - 1, i + 10);
        else if (ev.key == key::Home)
            i = 0;
        else if (ev.key == key::End)
            i = n - 1;
        else if (ev.key == key::Up || ev.key == key::Down) {
            // The first model of the next (or previous) kind.
            const int step = ev.key == key::Down ? 1 : n - 1;
            const std::string kind = kindOf(names_[size_t(i)]);
            do
                i = (i + step) % n;
            while (i != index_ && kindOf(names_[size_t(i)]) == kind);
            if (ev.key == key::Up)
                while (i > 0 && kindOf(names_[size_t(i - 1)]) == kindOf(names_[size_t(i)]))
                    --i;
        } else if (ev.key == 'l' && levels_.size() > 1) {
            level_ = (level_ + 1) % int(levels_.size());
            build();
        } else if (ev.key == 'p' && pkg_.paintJobs > 1) {
            paintJob_ = (paintJob_ + 1) % pkg_.paintJobs;
            build();
        } else if (ev.key == 'g') {
            showLights_ = !showLights_;
            build();
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        } else if (ev.key == '+' || ev.key == '=') {
            distance_ = std::max(radius_ * 0.2f, distance_ * 0.9f);
        } else if (ev.key == '-') {
            distance_ /= 0.9f;
        }
        if (i != index_)
            show(i);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::max(radius_ * 0.2f, distance_ * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::drawInfo() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    std::vector<std::pair<std::string, Color>> lines;
    const Color white{230, 230, 230}, blue{150, 200, 255}, red{255, 120, 120};
    lines.push_back({names_[size_t(index_)] + "  (" + std::to_string(index_ + 1) + "/" +
                         std::to_string(names_.size()) + ")",
                     blue});
    if (!error_.empty()) {
        lines.push_back({error_, red});
    } else if (showInfo_) {
        std::string lv;
        for (size_t i = 0; i < levels_.size(); ++i)
            lv += (i ? " " : "") + (int(i) == level_ ? "[" + levels_[i] + "]" : levels_[i]);
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "level %s (L), paint job %u of %u (P), lit parts %s (G); %zu vertices, %zu triangles",
                      lv.empty() ? "-" : lv.c_str(), paintJob_ + 1, std::max(pkg_.paintJobs, 1u),
                      showLights_ ? "shown" : "hidden", mesh_.vertices, mesh_.triangles);
        lines.push_back({buf, white});
        std::string parts;
        for (const PkgMesh& m : pkg_.meshes)
            if (isLightPart(m.part()) ? showLights_
                                      : levels_.empty() || m.level() == levels_[size_t(level_)] || !isLevel(m.level()))
                parts += (parts.empty() ? "" : " ") + m.name;
        lines.push_back({"meshes: " + parts, white});
        std::snprintf(buf, sizeof buf, "%u shaders a paint job; %zu placed models", pkg_.shadersPerJob,
                      pkg_.xrefs.size());
        lines.push_back({buf, white});
        for (const PkgXref& x : pkg_.xrefs)
            if (lines.size() < 12)
                lines.push_back({"  " + x.name + " at " + std::to_string(x.matrix[9]) + " " +
                                     std::to_string(x.matrix[10]) + " " + std::to_string(x.matrix[11]),
                                 white});
        for (const std::string& m : textures_->missing())
            lines.push_back({"missing texture: " + m, red});
    }
    int y = 4;
    for (const auto& [line, color] : lines) {
        if (y > kFrameH - lh)
            break;
        font::drawText(overlay_, 4, y, line, color);
        y += lh;
    }
}

void ModelViewer::update(uint64_t) {
    if (!renderer_)
        return;
    const gfx::Vec3 offset{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
    const gfx::Vec3 eye{center_[0] + offset[0] * distance_, center_[1] + offset[1] * distance_,
                        center_[2] + offset[2] * distance_};
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), std::max(0.01f, radius_ * 0.01f), radius_ * 50),
        gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye, {70, 74, 82});
    // Opaque first, then the translucent over them.
    const SceneLight light;
    drawModelMesh(*renderer_, mesh_, gfx::identity(), light, false);
    drawModelMesh(*renderer_, mesh_, gfx::identity(), light, true);
    drawInfo();
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
