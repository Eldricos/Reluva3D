// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/map_page.h"

#include "common/events.h"
#include "common/text.h"
#include "engines/storm3d/formats/odf.h"
#include "engines/storm3d/pages/model_page.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

namespace {

constexpr Color kClear = {4, 4, 10};
constexpr float kTurnPerSecond = 0.15f; // radians
// The game's camera sees 45 degrees from top to bottom: tan 22.5 is in its
// memory, and the planets of a frame land where the original has them.
constexpr float kFieldOfView = 0.7853982f; // radians
constexpr float kStep = 0.1f;              // of the distance, a W/A/S/D press
// The game's overview camera, from RTS_CFG.h: its height above the plane,
// at first and at most and least, and the parabola it rides.
constexpr float kOverviewInitHeight = 450; // OVERVIEW_INIT_HEIGHT
constexpr float kOverviewMinHeight = 10;   // OVERVIEW_MIN_HEIGHT
constexpr float kOverviewMaxHeight = 700;  // OVERVIEW_MAX_HEIGHT
constexpr float kOverviewFocus = 200;      // OVERVIEW_PARABOLA_FOCUS
constexpr float kOverviewZoomStep = 0.8f;  // a + press's height, of the last

} // namespace

MapPage::Look MapPage::lookOf(const std::string &odf, const SearchSet &files) {
    Look look;
    look.model = odf;
    Odf definition;
    const Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
    if (definition.load(odf + ".odf", loader))
        return look; // no definition: the model by the same name, if there is one
    if (definition.number("dont_load_SOD") != 0)
        look.model.clear();
    look.scale = float(definition.number("scaleSOD", 1));
    if (look.scale <= 0)
        look.scale = 1;
    if (text::equalsIgnoreCase(definition.string("classLabel"), "asteroidfield")) {
        look.minAsteroids = int(definition.number("minAsteroids"));
        look.maxAsteroids = std::max(look.minAsteroids, int(definition.number("maxAsteroids")));
        look.fieldRadius = float(definition.number("fieldRadius"));
    }
    return look;
}

std::vector<std::pair<std::string, gfx::Mat4>> MapPage::asteroidsOf(const BznMap::Object &o, const Look &look) {
    std::vector<std::pair<std::string, gfx::Mat4>> out;
    if (look.maxAsteroids <= 0)
        return out;
    // xorshift32, seeded from the object's number.
    uint32_t state = o.handle ^ 0x9e3779b9u;
    if (!state)
        state = 1;
    auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    auto unit = [&next] { return float(next() % 10000) / 10000.0f; };
    const int count = look.minAsteroids + int(next() % uint32_t(look.maxAsteroids - look.minAsteroids + 1));
    for (int i = 0; i < count; ++i) {
        const uint32_t which = next() % 8; // master8 isn't loaded by a mission of 155 fields: not used
        const std::string model = which ? "master" + std::to_string(which) : "master";
        // Within the radius about the field, on its plane, a little above or below.
        const float angle = unit() * 6.2831853f;
        const float r = std::sqrt(unit()) * look.fieldRadius;
        const float turn = unit() * 6.2831853f;
        gfx::Mat4 m = gfx::identity();
        m[0] = std::cos(turn) * look.scale;
        m[2] = -std::sin(turn) * look.scale;
        m[5] = look.scale;
        m[8] = std::sin(turn) * look.scale;
        m[10] = std::cos(turn) * look.scale;
        m[12] = o.position[0] + std::cos(angle) * r;
        m[13] = o.position[1] + (unit() - 0.5f) * look.fieldRadius * 0.2f;
        m[14] = o.position[2] + std::sin(angle) * r;
        out.push_back({model, m});
    }
    return out;
}

gfx::Mat4 MapPage::placeOf(const BznMap::Object &o, float scale) {
    gfx::Mat4 s = gfx::identity();
    s[0] = s[5] = s[10] = scale;
    return gfx::multiply(ModelPage::toMatrix(o.transform), s);
}

MapPage::MapPage(System &sys, Surface &screen, const GameFiles &files)
    : _sys(sys), _screen(screen), _files(files), _meshes(files) {
    for (const std::string &name : files.list())
        if (text::hasSuffixIgnoreCase(name, ".bzn"))
            _names.push_back(name);
}

bool MapPage::select(const std::string &name) {
    for (size_t i = 0; i < _names.size(); ++i)
        if (text::equalsIgnoreCase(_names[i], name)) {
            _current = i;
            load();
            draw();
            return true;
        }
    return false;
}

MapPage::~MapPage() {
    _meshes.clear();
}

void MapPage::activate() {
    if (_names.empty())
        return;
    if (Error err = _sys.init3D(_screen.w, _screen.h, _renderer)) {
        _renderer = nullptr;
        showMessage("The Maps page needs 3D rendering, which isn't available: " + err.message);
        return;
    }
    _overlay.create(_screen.w, _screen.h, PixelFormat::kRGBA32);
    _meshes.attach(_renderer);
    _lastMillis = _sys.getMillis();
    load();
    draw();
}

void MapPage::deactivate() {
    _meshes.clear();
    _meshes.attach(nullptr);
    _placed.clear();
    _background = nullptr;
    _renderer = nullptr;
}

void MapPage::handleKey(uint32_t key) {
    // As the game's overview: + and - along the parabola, W/A/S/D over the
    // map along the way it faces; other keys leave it.
    if (_overview && (key == '+' || key == '=' || key == '-' || key == 'w' || key == 'a' || key == 's' || key == 'd')) {
        Overview &o = *_overview;
        const float st = std::sin(o.turn);
        const float ct = std::cos(o.turn);
        const float step = std::sqrt(kOverviewFocus * o.height);
        if (key == '+' || key == '=')
            o.height = std::max(kOverviewMinHeight, o.height * kOverviewZoomStep);
        else if (key == '-')
            o.height = std::min(kOverviewMaxHeight, o.height / kOverviewZoomStep);
        else if (key == 'w')
            _target[0] += st * step, _target[2] += ct * step;
        else if (key == 's')
            _target[0] -= st * step, _target[2] -= ct * step;
        else if (key == 'd') // right, in the game's left-handed coordinates
            _target[0] += ct * step, _target[2] -= st * step;
        else
            _target[0] -= ct * step, _target[2] += st * step;
        draw();
        return;
    }
    _overview.reset();
    const size_t before = _current;
    // W/A/S/D move over the map along the way the camera faces, in the
    // game's coordinates: forward (-sin, cos) and right (cos, sin) in x, z.
    const float step = _distance * kStep;
    const float sy = std::sin(_yaw);
    const float cy = std::cos(_yaw);
    switch (key) {
    case key::kLeft:
        _current = stepIndex(_current, -1, _names.size());
        break;
    case key::kRight:
        _current = stepIndex(_current, 1, _names.size());
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = _names.empty() ? 0 : _names.size() - 1;
        break;
    case key::kUp:
        _pitch = std::min(1.5f, _pitch + 0.1f);
        break;
    case key::kDown:
        _pitch = std::max(0.05f, _pitch - 0.1f);
        break;
    case '+':
    case '=':
        _distance = std::max(50.0f, _distance * 0.8f);
        break;
    case '-':
        _distance = std::min(40000.0f, _distance * 1.25f);
        break;
    case 'w':
        _target[0] -= sy * step, _target[2] += cy * step;
        break;
    case 's':
        _target[0] += sy * step, _target[2] -= cy * step;
        break;
    case 'a':
        _target[0] -= cy * step, _target[2] -= sy * step;
        break;
    case 'd':
        _target[0] += cy * step, _target[2] += sy * step;
        break;
    case 't': // from straight above, x to the right and z up the screen, as the map's picture
        _yaw = 0;
        _pitch = 1.5f;
        _turning = false;
        break;
    case 'n':
        for (size_t i = 1; i <= _map.objects.size(); ++i) {
            const size_t at = (_focus == SIZE_MAX ? i - 1 : _focus + i) % _map.objects.size();
            if (_map.objects[at].team != 0) {
                _focus = at;
                _target = _map.objects[at].position;
                _distance = 400;
                break;
            }
        }
        break;
    case 'p':
        if (!_map.paths.empty()) {
            _path = _path == SIZE_MAX ? 0 : (_path + 1) % _map.paths.size();
            if (!_map.paths[_path].points.empty())
                _target = _map.paths[_path].points[0];
        }
        break;
    case 'g':
        _overview = Overview{kOverviewInitHeight, 0};
        draw();
        return;
    case key::kSpace:
        _turning = !_turning;
        break;
    default:
        return;
    }
    _fixed.reset();
    if (_current != before)
        load();
    draw();
}

void MapPage::update(uint64_t millis) {
    const float seconds = float(millis - _lastMillis) / 1000;
    _lastMillis = millis;
    if (_turning)
        _yaw += seconds * kTurnPerSecond;
    if (_renderer)
        draw();
}

void MapPage::load() {
    if (!_renderer)
        return;
    _meshes.clear();
    _placed.clear();
    _background = nullptr;
    _unmodelled = 0;
    _drawn = 0;
    _effects = 0;
    _focus = SIZE_MAX;
    _path = SIZE_MAX;
    _overview.reset();
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _files.read(_names[_current], data); err || (err = _map.parse(data))) {
        _map = BznMap{};
        _error = err.message;
    }
    for (const BznMap::Object &o : _map.objects) {
        const std::string key = text::toLower(o.odf);
        auto look = _looks.find(key);
        if (look == _looks.end())
            look = _looks.emplace(key, lookOf(o.odf, _files.files())).first;
        const size_t before = _placed.size();
        for (const auto &[model, place] : asteroidsOf(o, look->second))
            if (const SodMeshes::Built *built = _meshes.model(model))
                _placed.push_back({built, place});
        if (_placed.size() > before)
            ++_drawn;
        // A sprite node named for the definition is the object's too, at
        // its centre: the planets' glows ("Nimbus for planets", planets.spr).
        if (const SodMeshes::Built *glow = _meshes.spriteNode(o.odf))
            _placed.push_back({glow, placeOf(o, look->second.scale)});
        if (look->second.model.empty())
            continue;
        const SodMeshes::Built *built = _meshes.model(look->second.model);
        if (!built) {
            ++_unmodelled;
            continue;
        }
        if (built->parts.empty() && built->sprites.empty()) {
            ++_effects;
            continue;
        }
        _placed.push_back({built, placeOf(o, look->second.scale)});
        ++_drawn;
    }
    std::string background = _map.background;
    if (text::hasSuffixIgnoreCase(background, ".sod"))
        background.resize(background.size() - 4);
    _background = background.empty() ? nullptr : _meshes.model(background);
    if (_background && _background->parts.empty() && _background->sprites.empty())
        _background = nullptr;
    // From above the area's middle, far enough to see it whole.
    _target = {(_map.area[0] + _map.area[1]) / 2, 0, (_map.area[4] + _map.area[5]) / 2};
    _distance = std::max(1000.0f, std::max(_map.area[1] - _map.area[0], _map.area[5] - _map.area[4]));
}

MapPage::Camera MapPage::overviewOf(const gfx::Vec3 &target, float height, float turn) {
    // On a parabola: as far behind the target as sqrt(focus x height), as
    // the game's memory shows at three heights.
    height = std::clamp(height, kOverviewMinHeight, kOverviewMaxHeight);
    const float back = std::sqrt(kOverviewFocus * height);
    Camera c;
    c.eye = {target[0] - std::sin(turn) * back, height, target[2] - std::cos(turn) * back};
    const gfx::Vec3 to{target[0] - c.eye[0], target[1] - c.eye[1], target[2] - c.eye[2]};
    const float len = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    c.forward = {to[0] / len, to[1] / len, to[2] / len};
    // Up: square to forward, in the plane of forward and the world's up.
    const float fy = c.forward[1];
    c.up = {-fy * c.forward[0], 1 - fy * fy, -fy * c.forward[2]};
    const float upLen = std::sqrt(c.up[0] * c.up[0] + c.up[1] * c.up[1] + c.up[2] * c.up[2]);
    for (float &v : c.up)
        v /= upLen;
    c.fovY = kFieldOfView;
    return c;
}

MapPage::Camera MapPage::orbit() const {
    // Round the target: yaw 0 faces +z, pitch looks down.
    const float sy = std::sin(_yaw);
    const float cy = std::cos(_yaw);
    const float sp = std::sin(_pitch);
    const float cp = std::cos(_pitch);
    Camera c;
    c.forward = {-cp * sy, -sp, cp * cy};
    c.up = {-sp * sy, cp, sp * cy};
    c.eye = {_target[0] - c.forward[0] * _distance, _target[1] - c.forward[1] * _distance,
             _target[2] - c.forward[2] * _distance};
    c.fovY = kFieldOfView;
    return c;
}

void MapPage::draw() {
    if (!_renderer)
        return;
    const Camera camera = _fixed      ? *_fixed
                          : _overview ? overviewOf(_target, _overview->height, _overview->turn)
                                      : orbit();
    // The renderer is right-handed, the game left-handed: the camera's
    // place and axes have z reversed for it (SodMeshes::mirrorZ), and what
    // it draws is in the game's coordinates.
    auto flip = [](const gfx::Vec3 &v) { return gfx::Vec3{v[0], v[1], -v[2]}; };
    const gfx::Vec3 &eye = camera.eye;
    const float range = std::max(_distance, 500.0f);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::multiply(gfx::perspective(camera.fovY, float(_screen.w) / float(_screen.h), range * 0.02f,
                                                     range * 4 + 20000),
                                    gfx::lookAlong(flip(eye), flip(camera.forward), flip(camera.up))),
                      SodMeshes::mirrorZ());
    _renderer->beginFrame(viewProj, eye, kClear);
    // Right, in the game's left-handed coordinates, is up × forward.
    const gfx::Vec3 &u = camera.up;
    const gfx::Vec3 &f = camera.forward;
    _meshes.setView({u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]}, u,
                    double(_lastMillis) / 1000);
    // The background round the eye, behind everything: how the game
    // places it isn't known.
    if (_background) {
        gfx::Mat4 at = gfx::identity();
        at[12] = eye[0], at[13] = eye[1], at[14] = eye[2];
        _meshes.draw(*_background, at, true);
    }
    _meshes.draw(_placed);

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::string line = text::format("%zu/%zu  %s  ", _current + 1, _names.size(), _names[_current].c_str());
    if (!_error.empty())
        line += "not read: " + _error;
    else
        line += text::format("\"%s\", %zu objects, %zu drawn, %zu emitters only (not drawn), %zu models not found",
                             _map.description.c_str(), _map.objects.size(), _drawn, _effects, _unmodelled);
    drawStatus(_overlay, line);
    _renderer->endFrame(&_overlay);
    _sys.setWindowTitle("Reluva viewer - Maps - " + line);
}

void MapPage::showMessage(const std::string &line) {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kClear);
    font::drawText(_screen, 8, 8, line, {255, 140, 120});
    _sys.setWindowTitle("Reluva viewer - Maps - 3D rendering not available");
}

} // namespace reluva::storm3d
