// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/pages/car_page.h"

#include "common/fs.h"
#include "engines/nfs/formats/fsh.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace reluva::nfs::nfs2se {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr const char *kLevelNames[] = {"high", "medium", "low", "all"};

// Whether part `p` is in level `level` (geo.h: 0-19 high, 20-22 medium,
// 23-25 low).
bool inLevel(size_t p, int level) {
    constexpr size_t kStart[] = {0, 20, 23, 26};
    return level == 3 || (p >= kStart[level] && p < kStart[level + 1]);
}

} // namespace

CarPage::CarPage(const GameFiles &files) : _files(files) {
    _models = files.withExtensions({".geo"});
}

CarPage::~CarPage() {
    release();
}

void CarPage::activate(System &sys) {
    if (Error err = sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _unavailable = err.message;
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    const char *view = std::getenv("RELUVA_NFS2SE_CARS_VIEW");
    int wanted = _index;
    float yaw = _yaw;
    float pitch = _pitch;
    int level = -1;
    if (view && std::sscanf(view, "%d %f %f %d", &wanted, &yaw, &pitch, &level) >= 1 && !_models.empty()) {
        _index = std::clamp(wanted, 0, static_cast<int>(_models.size()) - 1);
        _yaw = yaw;
        _pitch = pitch;
    }
    load(_index);
    if (level >= 0 && level <= 3) {
        _level = level;
        build();
    }
}

void CarPage::deactivate() {
    release();
    _renderer = nullptr;
}

void CarPage::release() {
    if (_renderer) {
        if (_mesh)
            _renderer->destroyMesh(_mesh);
        for (gfx::TextureId t : _textures)
            if (t)
                _renderer->destroyTexture(t);
    }
    _mesh = 0;
    _textures.clear();
    _pictureNames.clear();
    _runs.clear();
}

void CarPage::load(int index) {
    release();
    _index = index;
    _error.clear();
    _skinName.clear();
    _model = {};
    std::vector<uint8_t> data;
    if (Error err = _files.read(_models[size_t(index)], data)) {
        _error = err.message;
        return;
    }
    if (Error err = parseCarModel(data, _model)) {
        _error = err.message;
        return;
    }
    // The pictures: the car's own .qfs, by the model's name.
    const std::string stem = fs::toLower(std::filesystem::path(_models[size_t(index)].inner).stem().string());
    for (const FileRef &f : _files.withExtensions({".qfs"})) {
        if (fs::toLower(f.name).find("carmodel/pc/" + stem + ".qfs") == std::string::npos)
            continue;
        ShapeBank bank;
        if (_files.read(f, data) || parseShapeBank(std::move(data), bank))
            break;
        _skinName = f.name;
        for (const Shape &s : bank.shapes) {
            image::Image img;
            gfx::TextureId id = 0;
            if (isPaletteType(s.type) || decodeShape(s, img) ||
                _renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(true), id))
                continue;
            _pictureNames.push_back(s.name);
            _textures.push_back(id);
        }
        break;
    }
    // The detailed level if the model has it (the racing cars), else medium.
    _level = std::any_of(_model.parts.begin(), _model.parts.begin() + 20,
                         [](const CarModel::Part &p) { return !p.polygons.empty(); })
                 ? 0
                 : 1;
    build();
}

void CarPage::build() {
    if (_renderer && _mesh)
        _renderer->destroyMesh(_mesh);
    _mesh = 0;
    QuadMesh quads;
    _missing = 0;
    for (size_t p = 0; p < _model.parts.size(); ++p) {
        if (!inLevel(p, _level))
            continue;
        const CarModel::Part &part = _model.parts[p];
        for (const CarModel::Polygon &poly : part.polygons) {
            const auto it = std::find(_pictureNames.begin(), _pictureNames.end(), poly.texture);
            const int texture = it == _pictureNames.end() ? -1 : static_cast<int>(it - _pictureNames.begin());
            _missing += texture < 0;
            std::array<Point, 4> corners;
            for (int k = 0; k < 4; ++k)
                for (int c = 0; c < 3; ++c)
                    corners[size_t(k)][size_t(c)] =
                        part.vertices[poly.vertices[size_t(k)]][size_t(c)] + part.position[size_t(c)];
            quads.add(texture, corners, QuadMesh::Corners::kBottomLeftFirst);
        }
    }
    _quads = quads.quads();
    if (_renderer)
        if (Error err = quads.build(*_renderer, _mesh, _runs))
            _error = err.message;
}

void CarPage::handleEvent(const Event &ev) {
    const int n = static_cast<int>(_models.size());
    if (ev.type == EventType::kKeyDown) {
        switch (ev.key) {
        case key::kRight:
            if (_renderer)
                load((_index + 1) % n);
            break;
        case key::kLeft:
            if (_renderer)
                load((_index + n - 1) % n);
            break;
        case 'a':
            _yaw -= 0.15f;
            break;
        case 'd':
            _yaw += 0.15f;
            break;
        case 'w':
            _pitch = std::min(_pitch + 0.1f, 1.5f);
            break;
        case 's':
            _pitch = std::max(_pitch - 0.1f, -1.5f);
            break;
        case '+':
        case '=':
            _distance = std::max(1.5f, _distance * 0.9f);
            break;
        case '-':
            _distance /= 0.9f;
            break;
        case 'l':
            _level = (_level + 1) % 4;
            build();
            break;
        case 't':
            _textured = !_textured;
            break;
        case ' ':
            _spin = !_spin;
            break;
        case 'r':
            _yaw = 0.6f;
            _pitch = 0.3f;
            _distance = 7;
            break;
        default:
            break;
        }
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw += static_cast<float>(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + static_cast<float>(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::max(1.5f, _distance * std::pow(0.9f, static_cast<float>(ev.wheelY)));
    }
}

void CarPage::draw(Surface &s) {
    std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
    font::drawText(s, 16, 16, "The Cars page needs 3D rendering, which isn't available:", page::kWarnColor);
    font::drawText(s, 16, 16 + font::kLineHeight, _unavailable, page::kTextColor);
}

void CarPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    if (_spin && _lastMs)
        _yaw += static_cast<float>(std::min<uint64_t>(nowMs - _lastMs, 100)) * 0.0006f;
    _lastMs = nowMs;
    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{offset[0] * _distance, offset[1] * _distance + 0.5f, offset[2] * _distance};
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), 0.05f, 500.0f),
                                             gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});
    if (_mesh) {
        gfx::DrawCall call;
        call.mesh = _mesh;
        call.rgbSource = gfx::ColorSource::kLighting;
        call.ambientLight = {0.5f, 0.5f, 0.5f};
        call.directedLight = {0.6f, 0.6f, 0.6f};
        call.lightDirection = {0.36f, 0.8f, 0.48f};
        // Which way the quads face isn't worked out: both sides drawn.
        call.state.cull = gfx::Cull::kNone;
        call.state.alphaTest = gfx::AlphaTest::kGe128;
        for (const QuadMesh::Run &r : _runs) {
            call.texture = _textured && r.texture >= 0 ? _textures[size_t(r.texture)] : 0;
            call.firstIndex = r.firstIndex;
            call.indexCount = r.indexCount;
            _renderer->draw(call);
        }
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Cars  " + std::to_string(_index + 1) + "/" + std::to_string(_models.size()) +
                                      "  " + _models[size_t(_index)].name};
    lines.push_back(!_error.empty() ? _error
                                    : std::string(kLevelNames[_level]) + " detail, " + std::to_string(_quads) +
                                          " polygons  pictures: " + (_skinName.empty() ? "none" : _skinName) +
                                          (_missing ? "  (" + std::to_string(_missing) + " not in it)" : ""));
    lines.push_back("Left/Right: model  drag/WASD: turn  wheel: zoom  L: level  T: textures  Space: spin");
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y =
            i + 1 == lines.size() ? kFrameH - font::kLineHeight - 4 : 4 + static_cast<int>(i) * font::kLineHeight;
        for (int yy = y - 2; yy < y + font::kLineHeight - 1; ++yy)
            for (int x = 0; x < kFrameW; ++x)
                _overlay.row(yy)[x * 4 + 3] = 140;
        font::drawText(_overlay, 6, y, lines[i], i == 1 && !_error.empty() ? page::kWarnColor : page::kTextColor);
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::nfs::nfs2se
