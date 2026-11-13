// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/font.h"
#include "common/fs.h"
#include "engines/awe/render/model_draw.h"
#include "engines/awe/pages/viewers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace reluva::awe {

namespace {

std::string folderOf(const std::string &name) {
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

std::string fileOf(const std::string &name) {
    const size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

} // namespace

ModelPage::ModelPage(ViewerContext &ctx) : Page(ctx) {
    // Each folder of Models: its models (<who>_model.gr2, and the detailed
    // <who>_model_1024.gr2), each with the folder's other files as its
    // animations.
    std::map<std::string, std::vector<std::string>> folders;
    for (const std::string &n : ctx.files.list())
        if (fs::toLower(n).rfind("models/", 0) == 0 && fs::toLower(n).size() > 4 &&
            fs::toLower(n).compare(n.size() - 4, 4, ".gr2") == 0)
            folders[fs::toLower(folderOf(n))].push_back(n);
    for (auto &[folder, files] : folders) {
        std::sort(files.begin(), files.end(),
                  [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
        std::vector<std::string> models;
        std::vector<std::string> animations;
        for (const std::string &f : files)
            (fs::toLower(fileOf(f)).find("_model") != std::string::npos ? models : animations).push_back(f);
        for (const std::string &m : models)
            _models.push_back({m, animations});
    }
}

ModelPage::~ModelPage() {
    releaseGpu();
}

void ModelPage::releaseGpu() {
    if (_renderer)
        for (gfx::MeshId m : _meshes)
            _renderer->destroyMesh(m);
    _meshes.clear();
    _gpu.reset();
}

void ModelPage::deactivate() {
    releaseGpu();
    _renderer = nullptr;
}

void ModelPage::activate() {
    if (Error err = _ctx.sys.init3D(kScreenW, kScreenH, _renderer)) {
        _renderer = nullptr;
        begin2D();
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        help("");
        present();
        return;
    }
    _overlay.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
    load();
}

void ModelPage::load() {
    releaseGpu();
    _error.clear();
    _animation = 0;
    _hasAnim = false;
    _time = 0;
    if (_models.empty() || !_renderer)
        return;
    const Entry &e = _models[size_t(_index)];
    _ctx.setTitle("Models: " + e.model);
    // Decoding the textures in granny2.dll takes a few seconds: say so.
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    font::drawText(_overlay, 8, 8, "Loading " + e.model + "...", {255, 230, 150}, 2);
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {30, 32, 38});
    _renderer->endFrame(&_overlay);

    Gr2File file;
    Model loaded;
    Error err = openGr2(_ctx.files, e.model, file);
    if (!err)
        err = loadModel(file, loaded);
    if (err) {
        _error = err.message;
        return;
    }
    _gpu = std::make_unique<GpuModel>(*_renderer, std::move(loaded));
    const Model &model = _gpu->model();
    // Look at the middle of the model at rest, from far enough to see it all.
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const ModelMesh &m : model.meshes)
        for (const ModelVertex &v : m.vertices)
            for (int k = 0; k < 3; k++)
                lo[k] = std::min(lo[k], v.position[k]), hi[k] = std::max(hi[k], v.position[k]);
    float size = 1;
    for (int k = 0; k < 3; k++) {
        _center[k] = lo[k] <= hi[k] ? (lo[k] + hi[k]) / 2 : 0;
        size = std::max(size, hi[k] - lo[k]);
    }
    _distance = size * 1.6f;
}

void ModelPage::loadAnimation() {
    _hasAnim = false;
    _time = 0;
    _error.clear();
    if (_animation == 0 || _models.empty())
        return;
    const std::string &name = _models[size_t(_index)].animations[size_t(_animation - 1)];
    Gr2File file;
    Error err = openGr2(_ctx.files, name, file);
    if (!err)
        err = awe::loadAnimation(file, _anim);
    if (err)
        _error = fileOf(name) + ": " + err.message;
    else
        _hasAnim = true;
}

void ModelPage::handleEvent(const Event &ev) {
    if (!_renderer || _models.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int animations = int(_models[size_t(_index)].animations.size());
        if (const int d = step(ev.key, _index, int(_models.size()))) {
            _index += d;
            load();
        } else if (ev.key == key::Up || ev.key == key::Down) {
            const int n = animations + 1;
            _animation = ((_animation + (ev.key == key::Down ? 1 : -1)) % n + n) % n;
            loadAnimation();
        } else if (ev.key == key::Space) {
            _playing = !_playing;
        } else if (ev.key == 'f') {
            _helpers = !_helpers;
        } else if (ev.key == 'r') {
            _yaw = -1.5708f, _pitch = 0.15f;
            _time = 0;
        } else if (ev.key == ',' || ev.key == '.') {
            _time = std::max(0.0f, _time + (ev.key == '.' ? 1 : -1) / 30.0f);
        }
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        _dragging = true, _dragX = ev.mouseX, _dragY = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        _dragging = false;
    } else if (ev.type == EventType::MouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _dragX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _dragY) * 0.01f, -1.5f, 1.5f);
        _dragX = ev.mouseX, _dragY = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        _distance *= std::pow(0.9f, float(ev.wheelY));
    }
}

void ModelPage::update(uint64_t nowMs) {
    if (!_renderer)
        return Page::update(nowMs);
    if (_playing && _lastMs != 0)
        _time += float(std::min<uint64_t>(nowMs - _lastMs, 100)) / 1000.0f;
    _lastMs = nowMs;
    float t = _time;
    if (_hasAnim && _anim.duration > 0)
        t = std::fmod(_time, _anim.duration);

    const gfx::Vec3 offset{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 eye{_center[0] + offset[0] * _distance, _center[1] + offset[1] * _distance,
                        _center[2] + offset[2] * _distance};
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.7f, float(kScreenW) / float(kScreenH), _distance / 100, _distance * 10),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    _renderer->beginFrame(viewProj, eye, {48, 52, 60});

    for (gfx::MeshId m : _meshes)
        _renderer->destroyMesh(m);
    _meshes.clear();
    size_t triangles = 0;
    if (_gpu)
        triangles =
            _gpu->draw(poseAt(_gpu->model(), _hasAnim ? &_anim : nullptr, t), gfx::identity(), _helpers, _meshes);

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!_models.empty()) {
        const Entry &e = _models[size_t(_index)];
        lines.push_back(e.model + "  (" + std::to_string(_index + 1) + "/" + std::to_string(_models.size()) + ")");
        char buf[200];
        const Model empty;
        const Model &model = _gpu ? _gpu->model() : empty;
        std::snprintf(buf, sizeof buf, "%zu bones, %zu meshes, %zu triangles drawn, %zu textures", model.bones.size(),
                      model.meshes.size(), triangles, model.textures.size());
        lines.push_back(buf);
        if (_animation == 0) {
            lines.push_back("rest pose (" + std::to_string(e.animations.size()) + " animations: Up/Down)");
        } else {
            std::snprintf(buf, sizeof buf, "animation %d/%zu: %s, %.2f / %.2f s%s", _animation, e.animations.size(),
                          fileOf(e.animations[size_t(_animation - 1)]).c_str(), t, _hasAnim ? _anim.duration : 0.0f,
                          _playing ? "" : " (paused)");
            lines.push_back(buf);
        }
    }
    if (!_error.empty())
        lines.push_back(_error);
    lines.push_back("Left/Right: model  Up/Down: animation  Space: pause  ,/.: step  F: helper meshes  drag, wheel: "
                    "view  R: reset  Tab: next page  Esc: launcher");
    for (size_t i = 0; i < lines.size(); i++) {
        const int y = 4 + int(i) * font::kLineHeight;
        for (int yy = y - 1; yy < y + font::kLineHeight - 1; yy++)
            for (int x = 0; x < int(font::length(lines[i])) * font::kAdvance + 8 && x < kScreenW; x++)
                _overlay.row(yy)[x * 4 + 3] = 150;
        font::drawText(_overlay, 4, y, lines[i], lines[i] == _error ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    prepareScreenshot(_renderer);
    _renderer->endFrame(&_overlay);
    finishScreenshot(_renderer, &_overlay);
}

} // namespace reluva::awe
