// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of 3D: the .3do models.

#include "common/fs.h"
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

/**
 * The .3do models, drawn through a colormap (dflt.cmp's at first; C steps
 * through them), turned about with A/D and W/S, nearer or further with +/-.
 */
class ModelPage : public Page {
public:
    explicit ModelPage(const ViewerData &data)
        : _data(data), _files(filesEnding(data.files, ".3do")), _materials(data.files) {
        _colormap = std::max(0, data.findColormap("dflt.cmp"));
    }

    const char *name() const override { return "Models"; }
    size_t count() const override { return _data.colormaps.empty() ? 0 : _files.size(); }
    const char *keys() const override { return "Left/Right, A/D W/S: turn, +/-: nearer, C: colormap"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index)) {
            _loaded = false;
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

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const FileRef &file = _files[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        if (!_loaded) {
            _loaded = true;
            std::vector<uint8_t> bytes;
            Error err = file.first->read(file.second, bytes);
            if (!err)
                err = parseModel(bytes, _model);
            _error = err ? err.message : "";
        }
        if (!_error.empty()) {
            drawStatus(screen, line + "  " + _error);
            return;
        }
        const std::vector<Transform> nodes = poseNodes(_model);
        Vec3 centre;
        float radius = 0;
        bounds(_model, nodes, centre, radius);
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
        drawStatus(screen, line + info);
    }

private:
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
};

} // namespace

std::unique_ptr<Page> makeModelPage(const ViewerData &data) {
    return std::make_unique<ModelPage>(data);
}

} // namespace reluva::sith
