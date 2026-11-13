// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's World page: a map drawn by WorldRenderer on the GPU,
// with a fly camera starting where the player does.

#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/pages/world_viewer.h"

#include "graphics/font.h"
#include "common/fs.h"

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

enum Held { kForward, kBack, kLeft, kRight, kUp, kDown, kFast };

} // namespace

WorldViewer::WorldViewer(ViewerContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            _names.push_back(n);
    }
}

WorldViewer::~WorldViewer() {
    deactivate();
}

void WorldViewer::deactivate() {
    // The renderer's resources go with it when the engine leaves 3D; the
    // shader cache last, as the others draw through it.
    _placed.clear();
    _models.reset();
    _world.reset();
    _shaders.reset();
    _renderer = nullptr;
}

void WorldViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        // No 3D here: say so on the 2D framebuffer.
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The World page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        text(4, 4 + 3 * font::kLineHeight, "Ctrl+Tab: the other pages still work.");
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    _device = _renderer->deviceName();
    _shaders = std::make_unique<ShaderCache>(*_renderer, _ctx.files, _ctx.shaders);
    _world = std::make_unique<WorldRenderer>(*_renderer, *_shaders);
    _models = std::make_unique<ModelCache>(*_renderer, *_shaders, _ctx.files);
    applyVideoSettings(videoSettings(_ctx.cvars), *_shaders, _world.get(), _models.get());
    load(_index);
}

void WorldViewer::load(int index) {
    _index = index;
    _error.clear();
    _info.clear();
    const std::string &name = _names[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");

    _placed.clear();
    _models->clear();
    _world->clear();
    _shaders->clear();
    std::vector<uint8_t> data;
    Bsp bsp;
    Error err = _ctx.files.read(name, data);
    if (!err)
        err = parseBsp(data, bsp);
    if (!err)
        err = _world->load(bsp, _ctx.shaders);
    if (err) {
        _error = err.message;
        _world->clear();
    }
    _loadedMs = _ctx.sys.getMillis();

    // Start where the player does, or above the middle of the map.
    _start = {0, 0, 0};
    _startYaw = 0;
    if (!bsp.models.empty())
        for (int i = 0; i < 3; ++i)
            _start[size_t(i)] = (bsp.models[0].mins[size_t(i)] + bsp.models[0].maxs[size_t(i)]) / 2;
    std::vector<Entity> entities;
    int unloadable = 0;
    _grid = LightGrid{};
    if (!parseEntities(bsp.entities, entities)) {
        // The light grid, with the worldspawn's cell size if it gives one.
        float cell[3] = {64, 64, 128};
        if (!entities.empty())
            entities[0].vector("gridsize", cell);
        _grid.build(bsp, {cell[0], cell[1], cell[2]});

        // Models named by entities (brush models, "*n", are map geometry).
        // Items and characters get theirs from the game code, so they
        // aren't shown here.
        for (const Entity &e : entities)
            for (const char *key : {"model", "model2"}) {
                const std::string model = e.get(key);
                if (model.size() < 4 || fs::toLower(model.substr(model.size() - 4)) != ".md3")
                    continue;
                const int id = _models->load(model);
                if (id < 0) {
                    ++unloadable;
                    continue;
                }
                float origin[3] = {0, 0, 0};
                float angles[3] = {0, 0, 0};
                e.vector("origin", origin);
                if (!e.vector("angles", angles))
                    angles[1] = std::strtof(e.get("angle", "0").c_str(), nullptr);
                Placed p;
                p.model = id;
                p.transform =
                    placement({origin[0], origin[1], origin[2]}, anglesToAxes({angles[0], angles[1], angles[2]}));
                p.light = _grid.sample({origin[0], origin[1], origin[2]});
                _placed.push_back(p);
            }

        const Entity *start = nullptr;
        for (const char *cls : {"info_player_start", "info_player_deathmatch"}) {
            for (const Entity &e : entities)
                if (e.classname() == cls) {
                    start = &e;
                    break;
                }
            if (start)
                break;
        }
        float origin[3];
        if (start && start->vector("origin", origin)) {
            _start = {origin[0], origin[1], origin[2] + kEyeHeight};
            _startYaw = std::strtof(start->get("angle", "0").c_str(), nullptr) * kPi / 180;
        }
    }
    toStart();

    _info.push_back(name);
    if (_error.empty()) {
        char line[160];
        std::snprintf(line, sizeof line, "%zu triangles in %zu batches, %zu images (%d missing), %zu lightmaps",
                      _world->triangleCount(), _world->batchCount(), _shaders->imageCount(), _shaders->missingImages(),
                      bsp.lightmapCount());
        _info.push_back(line);
        std::snprintf(line, sizeof line, "%zu models placed by entities%s%s", _placed.size(),
                      unloadable ? ", couldn't load " : "", unloadable ? std::to_string(unloadable).c_str() : "");
        _info.push_back(line);
        // What the map's shaders use that isn't drawn yet.
        std::string missing;
        for (const auto &[feature, n] : _shaders->unsupported())
            missing += (missing.empty() ? "" : ", ") + feature + (n > 1 ? " x" + std::to_string(n) : "");
        if (!missing.empty())
            _info.push_back("Not drawn yet: " + missing);
    }
}

void WorldViewer::toStart() {
    _pos = _start;
    _yaw = _startYaw;
    _pitch = 0;
}

void WorldViewer::handleEvent(const Event &ev) {
    if (!_renderer || _names.empty())
        return;
    auto hold = [&](uint32_t k, bool down) {
        switch (k) {
        case 'w':
            _held[kForward] = down;
            return true;
        case 's':
            _held[kBack] = down;
            return true;
        case 'a':
            _held[kLeft] = down;
            return true;
        case 'd':
            _held[kRight] = down;
            return true;
        case key::kSpace:
        case 'e':
            _held[kUp] = down;
            return true;
        case 'c':
        case 'q':
            _held[kDown] = down;
            return true;
        case key::kLShift:
        case key::kRShift:
            _held[kFast] = down;
            return true;
        default:
            return false;
        }
    };
    if (ev.type == EventType::kKeyUp) {
        hold(ev.key, false);
    } else if (ev.type == EventType::kKeyDown) {
        if (hold(ev.key, true) || ev.repeat)
            return;
        const int n = static_cast<int>(_names.size());
        if (ev.key == key::kLeft || ev.key == key::kRight)
            load((_index + (ev.key == key::kLeft ? n - 1 : 1)) % n);
        else if (ev.key == 'r')
            toStart();
        else if (ev.key == 'i')
            _showInfo = !_showInfo;
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.005f;
        _pitch = std::clamp(_pitch - float(ev.mouseY - _lastY) * 0.005f, -1.55f, 1.55f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    }
}

void WorldViewer::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000.0f) : 0.0f;
    _lastMs = nowMs;

    const gfx::Vec3 forward{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 right{std::sin(_yaw), -std::cos(_yaw), 0};
    const float step = kSpeed * (_held[kFast] ? 3.0f : 1.0f) * dt;
    for (int i = 0; i < 3; ++i) {
        const size_t k = size_t(i);
        _pos[k] += forward[k] * step * float(_held[kForward] - _held[kBack]);
        _pos[k] += right[k] * step * float(_held[kRight] - _held[kLeft]);
    }
    _pos[2] += step * float(_held[kUp] - _held[kDown]);

    // 90 degrees across, like the original's default.
    const float aspect = float(kFrameW) / float(kFrameH);
    const float fovY = 2 * std::atan(std::tan(kPi / 4) / aspect);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 32768), gfx::lookAlong(_pos, forward, {0, 0, 1}));
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(viewProj, _pos, {40, 48, 64});
    // Opaque surfaces and models, then translucent ones over them (sort
    // keys above "see through").
    constexpr float kLastOpaque = 5;
    const float time = float(nowMs - _loadedMs) / 1000.0f;
    _world->drawSky(time);
    _world->drawSurfaces(time, 0, kLastOpaque);
    for (const Placed &p : _placed)
        _models->draw(p.model, 0, p.transform, p.light, time, nullptr, 0, kLastOpaque);
    _world->drawSurfaces(time, kLastOpaque + 0.5f);
    for (const Placed &p : _placed)
        _models->draw(p.model, 0, p.transform, p.light, time, nullptr, kLastOpaque + 0.5f);
    drawOverlay(nowMs);
    const auto t1 = Clock::now();
    _renderer->endFrame(&_overlay);
    const auto t2 = Clock::now();
    _drawSum += std::chrono::duration<double, std::milli>(t1 - t0).count();
    _presentSum += std::chrono::duration<double, std::milli>(t2 - t1).count();
}

void WorldViewer::drawOverlay(uint64_t nowMs) {
    ++_frames;
    if (nowMs - _fpsStart >= 1000) {
        _fps = float(_frames) * 1000.0f / float(nowMs - _fpsStart);
        _drawMs = float(_drawSum / _frames);
        _presentMs = float(_presentSum / _frames);
        _fpsStart = nowMs;
        _frames = 0;
        _drawSum = _presentSum = 0;
    }
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    if (!_showInfo && _error.empty())
        return;
    std::vector<std::string> lines = _info;
    const size_t errorLine = _error.empty() ? size_t(-1) : lines.size();
    if (!_error.empty())
        lines.push_back(_error);
    char where[160];
    std::snprintf(where, sizeof where, "at %.0f %.0f %.0f, %.0f fps: %.1f ms drawing, %.1f ms presenting", _pos[0],
                  _pos[1], _pos[2], _fps, _drawMs, _presentMs);
    lines.push_back(where);
    lines.push_back("GPU: " + _device);
    // A translucent box behind the text keeps it readable.
    const int lh = font::kLineHeight;
    size_t widest = 0;
    for (const std::string &l : lines)
        widest = std::max(widest, font::length(l));
    const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8);
    const int boxH = int(lines.size()) * lh + 6;
    for (int y = 0; y < boxH; ++y) {
        uint8_t *p = _overlay.row(y);
        for (int x = 0; x < boxW; ++x)
            p[x * 4 + 3] = 150;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(_overlay, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
}

} // namespace reluva::idtech3
