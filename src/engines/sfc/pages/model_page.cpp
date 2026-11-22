// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: a .mod model with its textures, lit by a
// fixed light, turned with the mouse; its named points marked over it.

#include "common/fs.h"
#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::sfc {

namespace {

// Direct3D, which the game draws with, is left-handed and Reluva's renderer
// right-handed: z is turned round so a model isn't seen mirrored.
gfx::Vec3 toWorld(const std::array<float, 3> &p) {
    return {p[0], p[1], -p[2]};
}

} // namespace

ModelPage::ModelPage(ViewerContext &ctx) : Page(ctx) {
    _files = filesWithExtension(ctx.root, ".mod");
}

ModelPage::~ModelPage() {
    release();
}

void ModelPage::release() {
    if (!_renderer)
        return;
    if (_mesh)
        _renderer->destroyMesh(_mesh);
    _mesh = 0;
    for (const auto &[name, id] : _textures)
        if (id)
            _renderer->destroyTexture(id);
    _textures.clear();
    _draws.clear();
}

void ModelPage::deactivate() {
    release();
    _renderer = nullptr;
}

void ModelPage::activate() {
    if (Error err = _ctx.sys.init3D(kScreenW, kScreenH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32);
        clear({20, 22, 28});
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
    load();
}

void ModelPage::load() {
    _error.clear();
    _level = 0;
    std::vector<uint8_t> data;
    if (!readFile(_ctx.root / _files[_current], data))
        _error = "Can't read it";
    else if (Error err = _model.read(data))
        _error = err.message;
    if (!_error.empty())
        _model = Model();
    _distance = std::max(0.1f, _model.radius()) * 2.6f;
    setTitle(_files[_current].filename().string());
    build();
}

void ModelPage::build() {
    release();
    _missing.clear();
    if (_model.meshes().empty())
        return;
    const Model::Mesh &mesh = _model.meshes()[_level];
    // A draw for each material's triangles; the vertices aren't shared, as
    // each corner has its own texture coordinates.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (uint32_t m = 0; m < _model.materials().size(); ++m) {
        Draw draw;
        draw.firstIndex = static_cast<uint32_t>(indices.size());
        for (const Model::Triangle &t : mesh.triangles) {
            if (t.material != m)
                continue;
            const gfx::Vec3 n = toWorld(t.normal);
            // Reversed, so their winding survives z being turned round.
            for (int k = 2; k >= 0; --k) {
                const Model::Corner &c = t.corners[size_t(k)];
                const gfx::Vec3 p = toWorld(mesh.vertices[c.vertex]);
                gfx::Vertex v{};
                v.pos[0] = p[0];
                v.pos[1] = p[1];
                v.pos[2] = p[2];
                v.uv[0] = c.u;
                v.uv[1] = c.v;
                v.normal[0] = n[0];
                v.normal[1] = n[1];
                v.normal[2] = n[2];
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.push_back(v);
            }
        }
        draw.indexCount = static_cast<uint32_t>(indices.size()) - draw.firstIndex;
        if (draw.indexCount == 0)
            continue;
        const std::string &tex = _model.materials()[m].texture;
        if (!tex.empty()) {
            auto it = _textures.find(tex);
            if (it == _textures.end()) {
                gfx::TextureId id = 0;
                const auto path = fs::findFile((_ctx.root / _files[_current]).parent_path(), tex + ".bmp");
                std::vector<uint8_t> bytes;
                image::Image rgba;
                if (path && readFile(*path, bytes) && !image::decode(bytes, rgba))
                    _renderer->createTexture(rgba.w, rgba.h, rgba.rgba.data(), gfx::TextureParams(), id);
                else
                    _missing.push_back(tex);
                it = _textures.emplace(tex, id).first;
            }
            draw.texture = it->second;
        }
        _draws.push_back(draw);
    }
    if (!vertices.empty())
        if (Error err = _renderer->createMesh(vertices, indices, _mesh))
            _error = err.message;
}

void ModelPage::handleEvent(const Event &ev) {
    if (!_renderer || _files.empty())
        return;
    if (ev.type == EventType::kKeyDown) {
        const size_t n = _files.size();
        size_t next = _current;
        if (ev.key == key::kLeft)
            next = (_current + n - 1) % n;
        else if (ev.key == key::kRight)
            next = (_current + 1) % n;
        else if (ev.key == key::kPageUp)
            next = _current >= 10 ? _current - 10 : 0;
        else if (ev.key == key::kPageDown)
            next = std::min(n - 1, _current + 10);
        else if (ev.key == key::kHome)
            next = 0;
        else if (ev.key == key::kEnd)
            next = n - 1;
        else if (ev.key == 'l' && _model.meshes().size() > 1) {
            _level = (_level + 1) % _model.meshes().size();
            build();
        } else if (ev.key == 't')
            _textured = !_textured;
        else if (ev.key == 'p')
            _points = !_points;
        else if (ev.key == 'i')
            _info = !_info;
        else if (ev.key == 'r') {
            _yaw = 0.6f;
            _pitch = 0.4f;
            _distance = std::max(0.1f, _model.radius()) * 2.6f;
        } else if (ev.key == '+' || ev.key == '=')
            _distance *= 0.9f;
        else if (ev.key == '-')
            _distance /= 0.9f;
        if (next != _current) {
            _current = next;
            load();
        }
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance *= std::pow(0.9f, float(ev.wheelY));
    }
}

void ModelPage::drawInfo() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    const int lh = font::kLineHeight;
    int y = 4;
    const auto line = [&](const std::string &s, Color c) {
        font::drawText(_overlay, 4, y, s, c);
        y += lh;
    };
    line(_files[_current].generic_string() + "  (" + std::to_string(_current + 1) + " of " +
             std::to_string(_files.size()) + ")",
         {150, 200, 255});
    if (!_error.empty()) {
        line(_error, {255, 120, 120});
        return;
    }
    if (!_info)
        return;
    const Model::Mesh &mesh = _model.meshes()[_level];
    char buf[200];
    std::snprintf(buf, sizeof buf, "level %zu of %zu (L): %zu vertices, %zu triangles; radius %.2f; %zu materials",
                  _level + 1, _model.meshes().size(), mesh.vertices.size(), mesh.triangles.size(), _model.radius(),
                  _model.materials().size());
    line(buf, {230, 230, 230});
    std::snprintf(buf, sizeof buf, "%zu named points (P: %s); textures %s (T)", _model.points().size(),
                  _points ? "shown" : "hidden", _textured ? "on" : "off");
    line(buf, {230, 230, 230});
    for (const std::string &m : _missing)
        line("texture not found: " + m + ".bmp", {255, 120, 120});
    font::drawText(_overlay, 4, kScreenH - lh - 2,
                   "Left/Right, PgUp/PgDn: models  L: level  T: textures  P: points  R: reset  I: details  "
                   "drag, wheel: turn, zoom",
                   {170, 170, 170});
}

void ModelPage::update(uint64_t) {
    if (!_renderer)
        return;
    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{offset[0] * _distance, offset[1] * _distance, offset[2] * _distance};
    const float radius = std::max(0.1f, _model.radius());
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.8f, float(kScreenW) / float(kScreenH), radius * 0.01f, radius * 50),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, {30, 32, 40});
    for (const Draw &d : _draws) {
        gfx::DrawCall call;
        call.mesh = _mesh;
        call.firstIndex = d.firstIndex;
        call.indexCount = d.indexCount;
        call.texture = _textured ? d.texture : 0;
        call.rgbSource = gfx::ColorSource::kLighting;
        call.ambientLight = {0.45f, 0.45f, 0.45f};
        call.directedLight = {0.7f, 0.7f, 0.7f};
        call.lightDirection = {0.4f, 0.8f, 0.45f};
        // Which way the game culls isn't known yet: both sides are drawn.
        call.state.cull = gfx::Cull::kNone;
        _renderer->draw(call);
    }
    drawInfo();
    if (_points && _error.empty())
        for (const Model::Point &p : _model.points()) {
            const gfx::Vec3 s = gfx::transformPoint(viewProj, toWorld(p.position));
            if (s[2] < 0 || s[2] > 1)
                continue;
            const int x = int((s[0] + 1) * 0.5f * kScreenW);
            const int y = int((1 - s[1]) * 0.5f * kScreenH);
            font::drawText(_overlay, x - 2, y - font::kLineHeight / 2, "+ " + p.name, {255, 220, 80});
        }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::sfc
