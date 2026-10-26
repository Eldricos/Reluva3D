// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/font.h"
#include "common/fs.h"
#include "engines/awe/model_draw.h"
#include "engines/awe/viewers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace reluva::awe {

namespace {

std::string folderOf(const std::string& name) {
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

std::string fileOf(const std::string& name) {
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

} // namespace

ModelPage::ModelPage(ViewerContext& ctx) : Page(ctx) {
    // Each folder of Models: its models (<who>_model.gr2, and the detailed
    // <who>_model_1024.gr2), each with the folder's other files as its
    // animations.
    std::map<std::string, std::vector<std::string>> folders;
    for (const std::string& n : ctx.files.list())
        if (fs::toLower(n).rfind("models/", 0) == 0 && fs::toLower(n).size() > 4 &&
            fs::toLower(n).compare(n.size() - 4, 4, ".gr2") == 0)
            folders[fs::toLower(folderOf(n))].push_back(n);
    for (auto& [folder, files] : folders) {
        std::sort(files.begin(), files.end(),
                  [](const std::string& a, const std::string& b) { return fs::toLower(a) < fs::toLower(b); });
        std::vector<std::string> models, animations;
        for (const std::string& f : files)
            (fs::toLower(fileOf(f)).find("_model") != std::string::npos ? models : animations).push_back(f);
        for (const std::string& m : models)
            models_.push_back({m, animations});
    }
}

ModelPage::~ModelPage() { releaseGpu(); }

void ModelPage::releaseGpu() {
    if (renderer_)
        for (gfx::MeshId m : meshes_)
            renderer_->destroyMesh(m);
    meshes_.clear();
    gpu_.reset();
}

void ModelPage::deactivate() {
    releaseGpu();
    renderer_ = nullptr;
}

void ModelPage::activate() {
    if (Error err = ctx_.sys.init3D(kScreenW, kScreenH, renderer_)) {
        renderer_ = nullptr;
        begin2D();
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        help("");
        present();
        return;
    }
    overlay_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
    load();
}

void ModelPage::load() {
    releaseGpu();
    error_.clear();
    animation_ = 0;
    hasAnim_ = false;
    time_ = 0;
    if (models_.empty() || !renderer_)
        return;
    const Entry& e = models_[size_t(index_)];
    ctx_.sys.setWindowTitle("Models: " + e.model);
    // Decoding the textures in granny2.dll takes a few seconds: say so.
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    font::drawText(overlay_, 8, 8, "Loading " + e.model + "...", {255, 230, 150}, 2);
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {30, 32, 38});
    renderer_->endFrame(&overlay_);

    if (!ctx_.granny) {
        error_ = "granny2.dll couldn't be loaded, and the models need it";
        return;
    }
    Gr2File file;
    Model loaded;
    Error err = openGr2(ctx_.files, e.model, *ctx_.granny, file);
    if (!err)
        err = loadModel(file, ctx_.granny, loaded);
    if (err) {
        error_ = err.message;
        return;
    }
    gpu_ = std::make_unique<GpuModel>(*renderer_, std::move(loaded));
    const Model& model = gpu_->model();
    // Look at the middle of the model at rest, from far enough to see it all.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const ModelMesh& m : model.meshes)
        for (const ModelVertex& v : m.vertices)
            for (int k = 0; k < 3; k++)
                lo[k] = std::min(lo[k], v.position[k]), hi[k] = std::max(hi[k], v.position[k]);
    float size = 1;
    for (int k = 0; k < 3; k++) {
        center_[k] = lo[k] <= hi[k] ? (lo[k] + hi[k]) / 2 : 0;
        size = std::max(size, hi[k] - lo[k]);
    }
    distance_ = size * 1.6f;
}

void ModelPage::loadAnimation() {
    hasAnim_ = false;
    time_ = 0;
    error_.clear();
    if (animation_ == 0 || models_.empty() || !ctx_.granny)
        return;
    const std::string& name = models_[size_t(index_)].animations[size_t(animation_ - 1)];
    Gr2File file;
    Error err = openGr2(ctx_.files, name, *ctx_.granny, file);
    if (!err)
        err = awe::loadAnimation(file, anim_, ctx_.granny);
    if (err)
        error_ = fileOf(name) + ": " + err.message;
    else
        hasAnim_ = true;
}

void ModelPage::handleEvent(const Event& ev) {
    if (!renderer_ || models_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int animations = int(models_[size_t(index_)].animations.size());
        if (const int d = step(ev.key, index_, int(models_.size()))) {
            index_ += d;
            load();
        } else if (ev.key == key::Up || ev.key == key::Down) {
            const int n = animations + 1;
            animation_ = ((animation_ + (ev.key == key::Down ? 1 : -1)) % n + n) % n;
            loadAnimation();
        } else if (ev.key == key::Space) {
            playing_ = !playing_;
        } else if (ev.key == 'f') {
            helpers_ = !helpers_;
        } else if (ev.key == 'r') {
            yaw_ = -1.5708f, pitch_ = 0.15f;
            time_ = 0;
        } else if (ev.key == ',' || ev.key == '.') {
            time_ = std::max(0.0f, time_ + (ev.key == '.' ? 1 : -1) / 30.0f);
        }
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true, dragX_ = ev.mouseX, dragY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - dragX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - dragY_) * 0.01f, -1.5f, 1.5f);
        dragX_ = ev.mouseX, dragY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ *= std::pow(0.9f, float(ev.wheelY));
    }
}

void ModelPage::update(uint64_t nowMs) {
    if (!renderer_)
        return Page::update(nowMs);
    if (playing_ && lastMs_ != 0)
        time_ += float(std::min<uint64_t>(nowMs - lastMs_, 100)) / 1000.0f;
    lastMs_ = nowMs;
    float t = time_;
    if (hasAnim_ && anim_.duration > 0)
        t = std::fmod(time_, anim_.duration);

    const gfx::Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 eye{center_[0] + offset[0] * distance_, center_[1] + offset[1] * distance_,
                        center_[2] + offset[2] * distance_};
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.7f, float(kScreenW) / float(kScreenH), distance_ / 100, distance_ * 10),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    renderer_->beginFrame(viewProj, eye, {48, 52, 60});

    for (gfx::MeshId m : meshes_)
        renderer_->destroyMesh(m);
    meshes_.clear();
    size_t triangles = 0;
    if (gpu_)
        triangles = gpu_->draw(poseAt(gpu_->model(), hasAnim_ ? &anim_ : nullptr, t), gfx::identity(), helpers_,
                               meshes_);

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!models_.empty()) {
        const Entry& e = models_[size_t(index_)];
        lines.push_back(e.model + "  (" + std::to_string(index_ + 1) + "/" + std::to_string(models_.size()) + ")");
        char buf[200];
        const Model empty;
        const Model& model = gpu_ ? gpu_->model() : empty;
        std::snprintf(buf, sizeof buf, "%zu bones, %zu meshes, %zu triangles drawn, %zu textures",
                      model.bones.size(), model.meshes.size(), triangles, model.textures.size());
        lines.push_back(buf);
        if (animation_ == 0) {
            lines.push_back("rest pose (" + std::to_string(e.animations.size()) + " animations: Up/Down)");
        } else {
            std::snprintf(buf, sizeof buf, "animation %d/%zu: %s, %.2f / %.2f s%s", animation_, e.animations.size(),
                          fileOf(e.animations[size_t(animation_ - 1)]).c_str(), t, hasAnim_ ? anim_.duration : 0.0f,
                          playing_ ? "" : " (paused)");
            lines.push_back(buf);
        }
    }
    if (!error_.empty())
        lines.push_back(error_);
    lines.push_back("Left/Right: model  Up/Down: animation  Space: pause  ,/.: step  F: helper meshes  drag, wheel: "
                    "view  R: reset  Tab: next page  Esc: launcher");
    for (size_t i = 0; i < lines.size(); i++) {
        const int y = 4 + int(i) * font::kLineHeight;
        for (int yy = y - 1; yy < y + font::kLineHeight - 1; yy++)
            for (int x = 0; x < int(font::length(lines[i])) * font::kAdvance + 8 && x < kScreenW; x++)
                overlay_.row(yy)[x * 4 + 3] = 150;
        font::drawText(overlay_, 4, y, lines[i], lines[i] == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    prepareScreenshot(renderer_);
    renderer_->endFrame(&overlay_);
    finishScreenshot(renderer_, &overlay_);
}

} // namespace reluva::awe
