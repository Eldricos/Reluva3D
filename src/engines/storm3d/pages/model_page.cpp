// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/model_page.h"

#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace reluva::storm3d {

namespace {

constexpr Color kClear = {12, 14, 22};
constexpr float kTurnPerSecond = 0.5f; // radians
constexpr float kFieldOfView = 0.9f;   // radians, vertically

// The number after "lod" in a node's name, or -1.
int lodNumber(const std::string &name) {
    if (name.size() < 4 || !text::equalsIgnoreCase(name.substr(0, 3), "lod"))
        return -1;
    for (size_t i = 3; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9')
            return -1;
    return std::atoi(name.c_str() + 3);
}

// The index of the first node of that name, or -1.
int findNode(const SodModel &model, const std::string &name) {
    for (size_t i = 0; i < model.nodes.size(); ++i)
        if (model.nodes[i].name == name)
            return int(i);
    return -1;
}

} // namespace

gfx::Mat4 ModelPage::toMatrix(const SodModel::Transform &t) {
    gfx::Mat4 m = gfx::identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[size_t(i * 4 + j)] = t[size_t(i * 3 + j)];
    for (int j = 0; j < 3; ++j)
        m[size_t(12 + j)] = t[size_t(9 + j)];
    return m;
}

std::vector<gfx::Mat4> ModelPage::worldMatrices(const SodModel &model) {
    std::vector<gfx::Mat4> world(model.nodes.size(), gfx::identity());
    std::vector<int> state(model.nodes.size(), 0); // 0 not done, 1 being done, 2 done
    std::function<void(size_t)> place = [&](size_t i) {
        if (state[i] == 2)
            return;
        state[i] = 1;
        const SodModel::Node &n = model.nodes[i];
        const int parent = n.parent.empty() ? -1 : findNode(model, n.parent);
        gfx::Mat4 base = gfx::identity();
        // A parent that is its own ancestor is taken as a root.
        if (parent >= 0 && state[size_t(parent)] != 1) {
            place(size_t(parent));
            base = world[size_t(parent)];
        }
        world[i] = gfx::multiply(base, toMatrix(n.transform));
        state[i] = 2;
    };
    for (size_t i = 0; i < model.nodes.size(); ++i)
        place(i);
    return world;
}

std::vector<int> ModelPage::detailLevels(const SodModel &model) {
    std::vector<int> levels;
    for (const SodModel::Node &n : model.nodes)
        if (const int lod = lodNumber(n.name); lod >= 0 && std::find(levels.begin(), levels.end(), lod) == levels.end())
            levels.push_back(lod);
    std::sort(levels.begin(), levels.end());
    return levels;
}

bool ModelPage::shownAtLevel(const SodModel &model, size_t node, int level) {
    // Up the parents, at most as many as there are nodes, against loops.
    int at = int(node);
    for (size_t steps = 0; at >= 0 && steps <= model.nodes.size(); ++steps) {
        const SodModel::Node &n = model.nodes[size_t(at)];
        if (const int lod = lodNumber(n.name); lod >= 0 && lod != level)
            return false;
        at = n.parent.empty() ? -1 : findNode(model, n.parent);
    }
    return true;
}

ModelPage::ModelPage(System &sys, Surface &screen, const GameFiles &files) : _sys(sys), _screen(screen), _files(files) {
    for (const std::string &name : files.list())
        if (text::hasSuffixIgnoreCase(name, ".sod"))
            _names.push_back(name);
}

ModelPage::~ModelPage() {
    unload();
}

void ModelPage::activate() {
    if (_names.empty())
        return;
    if (Error err = _sys.init3D(_screen.w, _screen.h, _renderer)) {
        _renderer = nullptr;
        showMessage("The Models page needs 3D rendering, which isn't available: " + err.message);
        return;
    }
    _overlay.create(_screen.w, _screen.h, PixelFormat::kRGBA32);
    _lastMillis = _sys.getMillis();
    load();
    draw();
}

void ModelPage::deactivate() {
    unload();
    _renderer = nullptr;
}

void ModelPage::handleKey(uint32_t key) {
    const size_t before = _current;
    switch (key) {
    case key::kLeft:
        _current = stepIndex(_current, -1, _names.size());
        break;
    case key::kRight:
        _current = stepIndex(_current, 1, _names.size());
        break;
    case key::kPageUp:
        _current = stepIndex(_current, -10, _names.size());
        break;
    case key::kPageDown:
        _current = stepIndex(_current, 10, _names.size());
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = _names.empty() ? 0 : _names.size() - 1;
        break;
    case key::kUp:
        _pitch = std::min(1.5f, _pitch + 0.15f);
        break;
    case key::kDown:
        _pitch = std::max(-1.5f, _pitch - 0.15f);
        break;
    case '+':
    case '=':
        _distance = std::max(_radius * 0.3f, _distance * 0.8f);
        break;
    case '-':
        _distance = std::min(_radius * 20, _distance * 1.25f);
        break;
    case key::kSpace:
        _turning = !_turning;
        break;
    case 'l':
        if (_levels.size() > 1) {
            _level = (_level + _levels.size() - 1) % _levels.size();
            load();
        }
        break;
    default:
        return;
    }
    if (_current != before) {
        _level = SIZE_MAX; // the most detailed, once loaded
        load();
    }
    draw();
}

void ModelPage::update(uint64_t millis) {
    const float seconds = float(millis - _lastMillis) / 1000;
    _lastMillis = millis;
    if (_turning)
        _yaw += seconds * kTurnPerSecond;
    if (_renderer)
        draw();
}

gfx::TextureId ModelPage::texture(const std::string &name) {
    const std::string key = text::toLower(name);
    if (const auto it = _textures.find(key); it != _textures.end())
        return it->second;
    gfx::TextureId id = 0;
    // The full-colour texture first, as the Images page shows them.
    for (const char *folder : {"Textures/RGB/", "Textures/Index8/"}) {
        std::vector<uint8_t> data;
        image::Image img;
        if (_files.read(folder + name + ".tga", data) || image::decode(data, img))
            continue;
        if (_renderer->createTexture(img.w, img.h, img.rgba.data(), gfx::TextureParams{}, id))
            id = 0;
        break;
    }
    _textures[key] = id;
    return id;
}

void ModelPage::unload() {
    if (_renderer) {
        for (const gfx::MeshId m : _meshes)
            _renderer->destroyMesh(m);
        for (const auto &[name, id] : _textures)
            if (id)
                _renderer->destroyTexture(id);
    }
    _meshes.clear();
    _textures.clear();
    _parts.clear();
}

void ModelPage::load() {
    if (!_renderer)
        return;
    for (const gfx::MeshId m : _meshes)
        _renderer->destroyMesh(m);
    _meshes.clear();
    _parts.clear();
    _error.clear();

    std::vector<uint8_t> data;
    if (Error err = _files.read(_names[_current], data); err || (err = _model.parse(data))) {
        _model = SodModel{};
        _error = err.message;
    }
    // The higher a level's number, the more detailed it is: so in all 83
    // models that have levels.
    _levels = detailLevels(_model);
    _level = std::min(_level, _levels.empty() ? 0 : _levels.size() - 1);
    const int level = _levels.empty() ? -1 : _levels[_level];
    const std::vector<gfx::Mat4> world = worldMatrices(_model);

    gfx::Vec3 lo{1e30f, 1e30f, 1e30f};
    gfx::Vec3 hi{-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < _model.nodes.size(); ++i) {
        const SodModel::Node &node = _model.nodes[i];
        if (node.mesh < 0 || (level >= 0 && !shownAtLevel(_model, i, level)))
            continue;
        const SodModel::Mesh &mesh = _model.meshes[size_t(node.mesh)];
        // Normals by vertex, the faces' around it added up, for smooth
        // shading.
        std::vector<gfx::Vec3> normals(mesh.vertices.size(), gfx::Vec3{0, 0, 0});
        for (const SodModel::Group &g : mesh.groups)
            for (const SodModel::Face &f : g.faces) {
                const auto &a = mesh.vertices[f.vertex[0]];
                const auto &b = mesh.vertices[f.vertex[1]];
                const auto &c = mesh.vertices[f.vertex[2]];
                const gfx::Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const gfx::Vec3 v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const gfx::Vec3 n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                for (const uint16_t k : f.vertex)
                    for (int d = 0; d < 3; ++d)
                        normals[k][size_t(d)] += n[size_t(d)];
            }
        std::vector<gfx::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Part> parts;
        for (const SodModel::Group &g : mesh.groups) {
            Part part;
            part.firstIndex = uint32_t(indices.size());
            for (const SodModel::Face &f : g.faces)
                for (int k = 0; k < 3; ++k) {
                    gfx::Vertex v{};
                    const auto &p = mesh.vertices[f.vertex[size_t(k)]];
                    const auto &uv = mesh.uvs[f.uv[size_t(k)]];
                    gfx::Vec3 n = normals[f.vertex[size_t(k)]];
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    for (int d = 0; d < 3; ++d) {
                        v.pos[d] = p[size_t(d)];
                        v.normal[d] = len > 0 ? n[size_t(d)] / len : 0;
                    }
                    v.uv[0] = uv[0];
                    v.uv[1] = uv[1];
                    std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
                    indices.push_back(uint32_t(vertices.size()));
                    vertices.push_back(v);
                    const gfx::Vec3 at = gfx::transformPoint(world[i], {p[0], p[1], p[2]});
                    for (int d = 0; d < 3; ++d) {
                        lo[size_t(d)] = std::min(lo[size_t(d)], at[size_t(d)]);
                        hi[size_t(d)] = std::max(hi[size_t(d)], at[size_t(d)]);
                    }
                }
            part.indexCount = uint32_t(indices.size()) - part.firstIndex;
            for (const SodModel::LightingMaterial &m : _model.materials)
                if (m.name == g.material)
                    part.colour = {std::min(1.0f, m.diffuse[0]), std::min(1.0f, m.diffuse[1]),
                                   std::min(1.0f, m.diffuse[2]), 1};
            parts.push_back(part);
        }
        if (indices.empty())
            continue;
        gfx::MeshId id = 0;
        if (_renderer->createMesh(vertices, indices, id))
            continue;
        _meshes.push_back(id);
        const gfx::TextureId tex = texture(mesh.texture);
        for (Part &part : parts) {
            part.mesh = id;
            part.texture = tex;
            part.world = world[i];
            part.drawMode = mesh.drawMode;
            _parts.push_back(part);
        }
    }
    if (lo[0] <= hi[0]) {
        _centre = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
        const float dx = hi[0] - lo[0];
        const float dy = hi[1] - lo[1];
        const float dz = hi[2] - lo[2];
        _radius = std::max(0.01f, std::sqrt(dx * dx + dy * dy + dz * dz) / 2);
    } else {
        _centre = {0, 0, 0};
        _radius = 1;
    }
    _distance = _radius / std::tan(kFieldOfView / 2) * 1.1f;
}

void ModelPage::draw() {
    if (!_renderer)
        return;
    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{_centre[0] + offset[0] * _distance, _centre[1] + offset[1] * _distance,
                        _centre[2] + offset[2] * _distance};
    const float zNear = std::max(0.01f, _distance - _radius * 2);
    const float zFar = _distance + _radius * 2;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(kFieldOfView, float(_screen.w) / float(_screen.h), zNear, zFar),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, kClear);
    // Opaque parts first, then the additive ones over them.
    for (int pass = 0; pass < 2; ++pass)
        for (const Part &part : _parts) {
            const bool additive = part.drawMode == "additive" || part.drawMode == "wormhole";
            if (additive != (pass == 1))
                continue;
            gfx::DrawCall call;
            call.mesh = part.mesh;
            call.firstIndex = part.firstIndex;
            call.indexCount = part.indexCount;
            call.model = part.world;
            call.texture = part.texture;
            call.color = part.colour;
            // Which way the faces wind isn't known yet: both sides drawn.
            call.state.cull = gfx::Cull::kNone;
            if (additive) {
                call.state.src = gfx::Blend::kOne;
                call.state.dst = gfx::Blend::kOne;
                call.state.depthWrite = false;
            } else {
                call.rgbSource = gfx::ColorSource::kLighting;
                call.ambientLight = {0.35f, 0.35f, 0.38f};
                call.directedLight = {0.8f, 0.8f, 0.75f};
                call.lightDirection = {0.42f, 0.74f, 0.52f};
                if (part.drawMode == "alphathreshold")
                    call.state.alphaTest = gfx::AlphaTest::kGe128;
            }
            _renderer->draw(call);
        }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    size_t faces = 0;
    for (const SodModel::Mesh &m : _model.meshes)
        for (const SodModel::Group &g : m.groups)
            faces += g.faces.size();
    std::string line = text::format("%zu/%zu  %s  ", _current + 1, _names.size(), _names[_current].c_str());
    if (!_error.empty())
        line += "not read: " + _error;
    else
        line += text::format("%s %.1f, %zu nodes, %zu meshes, %zu triangles", _model.magic.c_str(),
                             double(_model.version), _model.nodes.size(), _model.meshes.size(), faces);
    if (_levels.size() > 1)
        line += text::format("  lod%d (L: next)", _levels[_level]);
    drawStatus(_overlay, line);
    _renderer->endFrame(&_overlay);
    _sys.setWindowTitle("Reluva viewer - Models - " + line);
}

void ModelPage::showMessage(const std::string &line) {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kClear);
    font::drawText(_screen, 8, 8, line, {255, 140, 120});
    _sys.setWindowTitle("Reluva viewer - Models - 3D rendering not available");
}

} // namespace reluva::storm3d
