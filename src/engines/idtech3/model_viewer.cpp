// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: every .md3 in the game drawn on the GPU
// with its shaders, players put together from their three parts.

#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kAnimationFps = 15;

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

ModelViewer::ModelViewer(ViewerContext& ctx) : Page(ctx) {
    std::set<std::string> names;
    for (const std::string& n : ctx.files.list())
        names.insert(normalizeArchiveName(n));
    std::set<std::string> players;
    for (const std::string& n : names) {
        if (!endsWith(n, ".md3"))
            continue;
        // Lower-detail copies of a model.
        if (endsWith(n, "_1.md3") || endsWith(n, "_2.md3"))
            continue;
        const std::string dir = folderOf(n);
        const bool part = endsWith(n, "/lower.md3") || endsWith(n, "/upper.md3") || endsWith(n, "/head.md3");
        if (part && names.count(dir + "/lower.md3") && names.count(dir + "/upper.md3") &&
            names.count(dir + "/head.md3")) {
            if (players.insert(dir).second)
                entries_.push_back({dir, true});
            continue;
        }
        entries_.push_back({n, false});
    }
}

ModelViewer::~ModelViewer() { deactivate(); }

void ModelViewer::deactivate() {
    models_.reset();
    shaders_.reset();
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
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    show(index_);
}

void ModelViewer::show(int index) {
    index_ = index;
    const Entry& e = entries_[size_t(index)];
    setTitle(e.name + " (" + std::to_string(index + 1) + "/" + std::to_string(entries_.size()) + ")");
    models_->clear();
    shaders_->clear();
    info_.clear();
    error_.clear();
    model_ = lower_ = upper_ = head_ = -1;
    skins_[0] = skins_[1] = skins_[2] = nullptr;
    frame_ = 0;
    startMs_ = ctx_.sys.getMillis();

    std::vector<const Md3Model*> parts;
    if (e.player) {
        lower_ = models_->load(e.name + "/lower.md3", &error_);
        upper_ = models_->load(e.name + "/upper.md3", &error_);
        head_ = models_->load(e.name + "/head.md3", &error_);
        if (lower_ < 0 || upper_ < 0 || head_ < 0) {
            info_.push_back(e.name);
            return;
        }
        skins_[0] = &models_->skin(e.name + "/lower_default.skin");
        skins_[1] = &models_->skin(e.name + "/upper_default.skin");
        skins_[2] = &models_->skin(e.name + "/head_default.skin");
        parts = {&models_->model(lower_), &models_->model(upper_), &models_->model(head_)};
    } else {
        model_ = models_->load(e.name, &error_);
        if (model_ < 0) {
            info_.push_back(e.name);
            return;
        }
        parts = {&models_->model(model_)};
    }

    // Frame the first frame's bounds (a player stands about 60 units
    // above its legs' origin).
    const Md3Frame& f = parts[0]->frames.front();
    for (int i = 0; i < 3; ++i)
        center_[size_t(i)] = (f.mins[size_t(i)] + f.maxs[size_t(i)]) / 2;
    radius_ = std::max(8.0f, f.radius);
    if (e.player) {
        center_[2] += 16;
        radius_ = std::max(radius_, 40.0f);
    }
    resetView();

    info_.push_back(e.name);
    for (const Md3Model* m : parts) {
        size_t verts = 0, tris = 0;
        std::set<std::string> shaderNames;
        for (const Md3Surface& s : m->surfaces) {
            verts += size_t(s.vertexCount);
            tris += s.indices.size() / 3;
            for (const std::string& sh : s.shaders)
                shaderNames.insert(sh);
        }
        std::string tags;
        for (int t = 0; t < m->tagCount; ++t)
            tags += (t ? " " : "") + m->tags[size_t(t)].name;
        char line[256];
        std::snprintf(line, sizeof line, "%s: %zu frames, %zu surfaces, %zu vertices, %zu triangles%s%s",
                      m->name.empty() ? "model" : m->name.c_str(), m->frames.size(), m->surfaces.size(), verts, tris,
                      tags.empty() ? "" : ", tags: ", tags.c_str());
        info_.push_back(line);
    }
}

void ModelViewer::resetView() {
    yaw_ = kPi * 0.85f;
    pitch_ = 0.25f;
    distance_ = radius_ * 2.6f;
}

void ModelViewer::handleEvent(const Event& ev) {
    if (!renderer_ || entries_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int n = static_cast<int>(entries_.size());
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
        else if (ev.key == 'a')
            animate_ = !animate_;
        else if (ev.key == ',')
            --frame_;
        else if (ev.key == '.')
            ++frame_;
        else if (ev.key == 'r')
            resetView();
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
        else if (ev.key == '+' || ev.key == '=')
            distance_ = std::max(radius_ * 0.3f, distance_ * 0.9f);
        else if (ev.key == '-')
            distance_ /= 0.9f;
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
        distance_ = std::max(radius_ * 0.3f, distance_ * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    const float time = float(nowMs - startMs_) / 1000.0f;
    const int frame = animate_ ? int(time * kAnimationFps) : frame_;

    const gfx::Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 eye{center_[0] + offset[0] * distance_, center_[1] + offset[1] * distance_,
                        center_[2] + offset[2] * distance_};
    const float aspect = float(kFrameW) / float(kFrameH);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.9f, aspect, 1, 8192),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    renderer_->beginFrame(viewProj, eye, {70, 74, 82});

    // No map, so no light grid: a fixed light, as the original uses then.
    const PointLight light = LightGrid{}.sample({0, 0, 0});
    std::string frames;
    if (model_ >= 0) {
        models_->draw(model_, frame, gfx::identity(), light, time);
        const int n = int(models_->model(model_).frames.size());
        frames = "frame " + std::to_string(((frame % n) + n) % n + 1) + "/" + std::to_string(n);
    } else if (lower_ >= 0) {
        // Legs at the origin, the torso on the legs' tag_torso, the head
        // on the torso's tag_head.
        const Md3Model& legs = models_->model(lower_);
        const Md3Model& torso = models_->model(upper_);
        const int nl = int(legs.frames.size()), nt = int(torso.frames.size());
        const int fl = ((frame % nl) + nl) % nl, ft = ((frame % nt) + nt) % nt;
        const gfx::Mat4 base = gfx::identity();
        models_->draw(lower_, fl, base, light, time, skins_[0]);
        gfx::Mat4 torsoAt = base;
        if (const Md3Tag* t = legs.tag(fl, "tag_torso"))
            torsoAt = gfx::multiply(base, tagPlacement(*t));
        models_->draw(upper_, ft, torsoAt, light, time, skins_[1]);
        gfx::Mat4 headAt = torsoAt;
        if (const Md3Tag* t = torso.tag(ft, "tag_head"))
            headAt = gfx::multiply(torsoAt, tagPlacement(*t));
        models_->draw(head_, 0, headAt, light, time, skins_[2]);
        frames = "legs frame " + std::to_string(fl + 1) + "/" + std::to_string(nl) + ", torso " +
                 std::to_string(ft + 1) + "/" + std::to_string(nt);
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (showInfo_ || !error_.empty()) {
        std::vector<std::string> lines = info_;
        if (!error_.empty())
            lines.push_back(error_);
        if (!frames.empty())
            lines.push_back(frames + (animate_ ? " (A: stop)" : " (A: animate, ,/.: step)") + ", " +
                            std::to_string(shaders_->missingImages()) + " images missing");
        const int lh = font::kLineHeight;
        size_t widest = 0;
        for (const std::string& l : lines)
            widest = std::max(widest, font::length(l));
        const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                overlay_.row(y)[x * 4 + 3] = 150;
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                           !error_.empty() && lines[i] == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::idtech3
