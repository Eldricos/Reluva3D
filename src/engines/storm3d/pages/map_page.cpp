// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/map_page.h"

#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::storm3d {

namespace {

constexpr Color kClear = {4, 4, 10};
constexpr float kTurnPerSecond = 0.15f; // radians
// The game's camera sees 45 degrees from top to bottom.
constexpr float kFieldOfView = 0.7853982f; // radians
constexpr float kStep = 0.1f;              // of the distance, a W/A/S/D press
// The game's overview camera, from RTS_CFG.h: its height above the plane,
// at first and at most and least, and the parabola it rides.
constexpr float kOverviewInitHeight = 450; // OVERVIEW_INIT_HEIGHT
constexpr float kOverviewMinHeight = 10;   // OVERVIEW_MIN_HEIGHT
constexpr float kOverviewMaxHeight = 700;  // OVERVIEW_MAX_HEIGHT
constexpr float kOverviewFocus = 200;      // OVERVIEW_PARABOLA_FOCUS
constexpr float kOverviewZoomStep = 0.8f;  // a + press's height, of the last
// The races a skirmish's players choose from, by their definitions, and
// as teamcolor.odf names them.
constexpr const char *kRaces[] = {"fed", "kling", "rom", "borg"};
constexpr const char *kRaceNames[] = {"federation", "klingon", "romulan", "borg"};

} // namespace

MapPage::MapPage(System &sys, Surface &screen, const GameFiles &files)
    : _sys(sys), _screen(screen), _files(files), _scene(files) {
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
    _scene.attach(nullptr);
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
    _scene.attach(_renderer);
    _lastMillis = _sys.getMillis();
    load();
    draw();
}

void MapPage::deactivate() {
    _scene.attach(nullptr);
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
        for (size_t i = 1; i <= _scene.map().objects.size(); ++i) {
            const size_t at = (_focus == SIZE_MAX ? i - 1 : _focus + i) % _scene.map().objects.size();
            if (_scene.map().objects[at].team != 0) {
                _focus = at;
                _target = _scene.map().objects[at].position;
                _distance = 400;
                break;
            }
        }
        break;
    case 'p': {
        const BznMap &map = _scene.map();
        if (!map.paths.empty()) {
            _path = _path == SIZE_MAX ? 0 : (_path + 1) % map.paths.size();
            if (!map.paths[_path].points.empty())
                _target = map.paths[_path].points[0];
        }
        break;
    }
    case 'g':
        _overview = Overview{kOverviewInitHeight, 0};
        draw();
        return;
    case 'r': // the next race's starting units, then none (SIZE_MAX + 1 wraps to the first)
        _race = _race + 1 < std::size(kRaces) ? _race + 1 : SIZE_MAX;
        load();
        break;
    case 'h':
        _showGrid = !_showGrid;
        break;
    case 'f':
        _showShroud = !_showShroud;
        load();
        break;
    case 'v': // the damage emitters: venting plasma and gas
        _scene.meshes().showDamage(!_scene.meshes().showingDamage());
        break;
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
    _focus = SIZE_MAX;
    _path = SIZE_MAX;
    _overview.reset();
    _error.clear();
    BznMap map;
    std::vector<uint8_t> data;
    if (Error err = _files.read(_names[_current], data); err || (err = map.parse(data))) {
        map = BznMap{};
        _error = err.message;
    }
    const bool racing = _race < std::size(kRaces);
    const std::vector<MapScene::StartUnit> units =
        racing ? MapScene::startUnitsOf(map, MapScene::standardUnitsOf(kRaces[_race], _files.files()))
               : std::vector<MapScene::StartUnit>{};
    _scene.build(map, units, racing ? kRaceNames[_race] : "",
                 _showShroud && !units.empty() ? _scene.sightsOf(map, units, -1) : std::vector<MapScene::Sight>{});
    // From above the area's middle, far enough to see it whole.
    _target = {(map.area[0] + map.area[1]) / 2, 0, (map.area[4] + map.area[5]) / 2};
    _distance = std::max(1000.0f, std::max(map.area[1] - map.area[0], map.area[5] - map.area[4]));
}

MapScene::Camera MapPage::orbit() const {
    // Round the target: yaw 0 faces +z, pitch looks down.
    const float sy = std::sin(_yaw);
    const float cy = std::cos(_yaw);
    const float sp = std::sin(_pitch);
    const float cp = std::cos(_pitch);
    MapScene::Camera c;
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
    const MapScene::Camera camera = _fixed      ? *_fixed
                                    : _overview ? MapScene::overviewOf(_target, _overview->height, _overview->turn)
                                                : orbit();
    _scene.draw(camera, double(_lastMillis) / 1000, std::max(_distance, 500.0f), kClear, _showGrid);

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::string line = text::format("%zu/%zu  %s  ", _current + 1, _names.size(), _names[_current].c_str());
    if (!_error.empty())
        line += "not read: " + _error;
    else
        line += text::format("\"%s\", %zu objects, %zu drawn, %zu with nothing to draw, %zu models not found",
                             _scene.map().description.c_str(), _scene.map().objects.size(), _scene.drawn(),
                             _scene.effects(), _scene.unmodelled());
    if (_race < std::size(kRaces))
        line += text::format(", %zu starting units (%s)", _scene.starting(), kRaces[_race]);
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
