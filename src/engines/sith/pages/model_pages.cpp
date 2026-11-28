// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of 3D: the .3do models.

#include "common/fs.h"
#include "engines/sith/formats/keyframe.h"
#include "engines/sith/formats/model.h"
#include "engines/sith/pages/pages.h"
#include "engines/sith/render/draw3d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr float kTurn = 3.14159265f / 12;

// The middle of a model's posed meshes, and how far from it they reach.
void bounds(const Model &model, const std::vector<Transform> &nodes, Vec3 &centre, float &radius) {
    Vec3 lo{1e9f, 1e9f, 1e9f};
    Vec3 hi{-1e9f, -1e9f, -1e9f};
    bool any = false;
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const int m = model.nodes[n].mesh;
        if (m < 0 || model.geosets.empty() || m >= int(model.geosets[0].size()))
            continue;
        const Vec3 &p = model.nodes[n].pivot;
        for (const Vec3 &v : model.geosets[0][size_t(m)].vertices) {
            const Vec3 w = nodes[n].apply({v.x + p.x, v.y + p.y, v.z + p.z});
            lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
            any = true;
        }
    }
    if (!any) {
        centre = {};
        radius = 0.1f;
        return;
    }
    centre = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    const Vec3 d{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    radius = std::max(0.01f, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) / 2);
}

// Whether every node an animation moves is one of the model's, by name,
// and one of them more than a dummy ("$$$dummy"), which every model has.
bool fits(const Keyframes &key, const Model &model) {
    bool real = false;
    for (const Keyframes::Node &kn : key.nodes)
        real = real || kn.meshName.rfind("$$$", 0) != 0;
    if (!real)
        return false;
    for (const Keyframes::Node &kn : key.nodes) {
        bool found = false;
        for (const Model::Node &mn : model.nodes)
            found = found || fs::toLower(mn.name) == fs::toLower(kn.meshName);
        if (!found)
            return false;
    }
    return true;
}

// The model's nodes at a frame of an animation: each node the animation
// has takes its position and rotation, matched by its number when the
// names agree there, else by name; the rest keep the model's.
std::vector<std::array<Vec3, 2>> pose(const Model &model, const Keyframes &key, float frame) {
    std::vector<std::array<Vec3, 2>> out;
    for (const Model::Node &n : model.nodes)
        out.push_back({n.position, n.rotation});
    for (const Keyframes::Node &kn : key.nodes) {
        int target = -1;
        const std::string name = fs::toLower(kn.meshName);
        if (kn.number >= 0 && kn.number < int(model.nodes.size()) &&
            fs::toLower(model.nodes[size_t(kn.number)].name) == name)
            target = kn.number;
        for (size_t i = 0; target < 0 && i < model.nodes.size(); ++i)
            if (fs::toLower(model.nodes[i].name) == name)
                target = int(i);
        if (target >= 0)
            key.at(kn, frame, out[size_t(target)][0], out[size_t(target)][1]);
    }
    return out;
}

/**
 * The .3do models, drawn through a colormap (dflt.cmp's at first; C steps
 * through them), turned about with A/D and W/S, nearer or further with +/-;
 * K steps through the animations that fit the model, Space plays one.
 */
class ModelPage : public Page {
public:
    explicit ModelPage(const ViewerData &data)
        : _data(data), _files(filesEnding(data.files, ".3do")), _materials(data.files) {
        _colormap = std::max(0, data.findColormap("dflt.cmp"));
    }

    const char *name() const override { return "Models"; }
    size_t count() const override { return _data.colormaps.empty() ? 0 : _files.size(); }
    const char *keys() const override {
        return "Left/Right, A/D W/S: turn, +/-: nearer, C: colormap, K: animation, Space: play, .: next frame";
    }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index)) {
            _loaded = false;
            _anim = -1;
            _playing = false;
            return true;
        }
        if (pressed == 'k') {
            load();
            findAnimations();
            _anim = _fitting.empty() ? -1 : (_anim + 2) % int(_fitting.size() + 1) - 1;
            _frame = 0;
            return true;
        }
        if (pressed == ' ' && _anim >= 0) {
            _playing = !_playing;
            _startMillis = _now;
            _startFrame = _frame;
            return true;
        }
        if (pressed == '.' && _anim >= 0) {
            _playing = false;
            const Keyframes &key = _parsed[size_t(_fitting[size_t(_anim)])];
            _frame = std::fmod(std::floor(_frame) + 1, float(key.frames));
            return true;
        }
        if (pressed == 'a')
            _yaw -= kTurn;
        else if (pressed == 'd')
            _yaw += kTurn;
        else if (pressed == 'w')
            _pitch = std::min(1.5f, _pitch + kTurn);
        else if (pressed == 's')
            _pitch = std::max(-1.5f, _pitch - kTurn);
        else if (pressed == '+' || pressed == '=')
            _zoom = std::max(0.3f, _zoom * 0.8f);
        else if (pressed == '-')
            _zoom = std::min(5.0f, _zoom * 1.25f);
        else if (pressed == 'c')
            _colormap = (_colormap + 1) % int(_data.colormaps.size());
        else
            return false;
        return true;
    }

    bool update(uint64_t millis) override {
        _now = millis;
        if (!_playing || _anim < 0)
            return false;
        const Keyframes &key = _parsed[size_t(_fitting[size_t(_anim)])];
        _frame = std::fmod(_startFrame + float(millis - _startMillis) * key.fps / 1000.0f, float(key.frames));
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const FileRef &file = _files[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        load();
        if (!_error.empty()) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        // Framed by the model at rest, so an animation moves it in view.
        Vec3 centre;
        float radius = 0;
        bounds(_model, poseNodes(_model), centre, radius);
        const Keyframes *key = _anim >= 0 ? &_parsed[size_t(_fitting[size_t(_anim)])] : nullptr;
        const std::vector<Transform> nodes = key ? poseNodes(_model, pose(_model, *key, _frame)) : poseNodes(_model);
        const soft3d::OrbitCamera camera({centre.x, centre.y, centre.z}, _yaw, _pitch, radius * 3.2f * _zoom, kViewerW,
                                         kViewerH - 16);
        const auto &[cmpName, cmp] = _data.colormaps[size_t(_colormap)];
        Draw3D draw(screen, camera, cmp);
        drawModel(draw, _materials, _model, nodes, Transform{});
        size_t faces = 0;
        for (const Model::Mesh &m : _model.geosets[0])
            faces += m.faces.size();
        char info[160];
        std::snprintf(info, sizeof(info), "  %zu meshes, %zu faces, %zu nodes, %zu materials, radius %.3f, %s",
                      _model.geosets[0].size(), faces, _model.nodes.size(), _model.materials.size(),
                      double(_model.radius), cmpName.c_str());
        line += info;
        if (key)
            line += ", " + _keyFiles[size_t(_fitting[size_t(_anim)])].second + " frame " +
                    std::to_string(int(_frame) + 1) + "/" + std::to_string(key->frames);
        else if (_searched)
            line += ", " + std::to_string(_fitting.size()) + " animations fit";
        drawStatus(screen, line);
    }

private:
    void load() {
        if (_loaded)
            return;
        _loaded = true;
        _searched = false;
        _fitting.clear();
        const FileRef &file = _files[_index];
        std::vector<uint8_t> bytes;
        Error err = file.first->read(file.second, bytes);
        if (!err)
            err = parseModel(bytes, _model);
        _error = err ? err.message : "";
    }

    // The animations whose nodes are all the model's, every .key read once.
    void findAnimations() {
        if (_searched || !_error.empty())
            return;
        _searched = true;
        if (_parsed.empty()) {
            _keyFiles = filesEnding(_data.files, ".key");
            _parsed.resize(_keyFiles.size());
            for (size_t i = 0; i < _keyFiles.size(); ++i) {
                std::vector<uint8_t> bytes;
                if (_keyFiles[i].first->read(_keyFiles[i].second, bytes) || parseKeyframes(bytes, _parsed[i]))
                    _parsed[i] = Keyframes{};
            }
        }
        for (size_t i = 0; i < _parsed.size(); ++i)
            if (fits(_parsed[i], _model))
                _fitting.push_back(int(i));
    }

    const ViewerData &_data;
    std::vector<FileRef> _files;
    MaterialCache _materials;
    size_t _index = 0;
    bool _loaded = false;
    Model _model;
    std::string _error;
    int _colormap = 0;
    float _yaw = 0.6f;
    float _pitch = 0.35f;
    float _zoom = 1;
    std::vector<FileRef> _keyFiles;
    std::vector<Keyframes> _parsed;
    std::vector<int> _fitting;
    bool _searched = false;
    int _anim = -1;
    float _frame = 0;
    bool _playing = false;
    uint64_t _now = 0;
    uint64_t _startMillis = 0;
    float _startFrame = 0;
};

} // namespace

std::unique_ptr<Page> makeModelPage(const ViewerData &data) {
    return std::make_unique<ModelPage>(data);
}

} // namespace reluva::sith
