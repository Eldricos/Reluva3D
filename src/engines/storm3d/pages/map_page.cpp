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
#include <cstdio>
#include <cstring>

namespace reluva::storm3d {

namespace {

constexpr Color kClear = {4, 4, 10};
constexpr float kTurnPerSecond = 0.15f; // radians
// The game's camera sees 45 degrees from top to bottom: tan 22.5 is in its
// memory, and the planets of a frame land where the original has them.
constexpr float kFieldOfView = 0.7853982f; // radians
constexpr float kStep = 0.1f;              // of the distance, a W/A/S/D press
// The map's lights, its settings' colour and its directional lights',
// are this much brighter in what the game draws: one factor fits the
// planet Barisa's 198 vertex colours in a frame of "Premonitions" to
// within 0.014 on average. Where it comes from isn't known.
constexpr float kLightScale = 1.75f;
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
// pi as the game has it, a float.
constexpr double kPi = 3.1415927410125732;
// The starting units' spiral (0x440f41): its first turn, out each try, and
// the most; past that a unit is made at the middle.
constexpr double kSpiralStart = 1;
constexpr double kSpiralStep = 1.0 / 12;
constexpr double kSpiralEnd = 40;
constexpr double kSpiralRadius = 100; // units out, a turn
// The players' slots the game makes units for: Camera_0 to Camera_8.
constexpr int kSlots = 9;
// The game's grid (its draws in a skirmish): lines each 100 units in x and
// z on the plane y = 0, added on in this colour.
constexpr float kGridSpacing = 100;
constexpr uint8_t kGridColour[3] = {38, 33, 0};
// Texels across a square of the grid's texture: its line one wide.
constexpr int kGridTexels = 64;

// Reads definitions (an .odf and what it includes) from the game's files.
Odf::Loader loaderFor(const SearchSet &files) {
    return [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
}

} // namespace

MapPage::Look MapPage::lookOf(const std::string &odf, const SearchSet &files) {
    Look look;
    look.model = odf;
    Odf definition;
    const Odf::Loader loader = loaderFor(files);
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

std::optional<gfx::Vec3> MapPage::teamColourOf(const std::string &name, const SearchSet &files) {
    Odf colours;
    const Odf::Loader loader = loaderFor(files);
    if (colours.load("teamcolor.odf", loader))
        return std::nullopt;
    const Odf::Entry *entry = colours.find(name);
    gfx::Vec3 c{};
    if (!entry || std::sscanf(entry->value.c_str(), "%f %f %f", &c[0], &c[1], &c[2]) != 3)
        return std::nullopt;
    return c;
}

std::vector<std::string> MapPage::standardUnitsOf(const std::string &race, const SearchSet &files) {
    std::vector<std::string> units;
    Odf definition;
    const Odf::Loader loader = loaderFor(files);
    if (definition.load(race + ".odf", loader))
        return units;
    for (int i = 1; i <= 9; ++i) {
        std::string name = definition.string("standardUnits" + std::to_string(i));
        // The game takes the name up to its dot, 8 letters at most.
        name = name.substr(0, std::min<size_t>(name.find('.'), 8));
        if (!name.empty())
            units.push_back(name);
    }
    return units;
}

std::vector<MapPage::StartUnit> MapPage::startUnitsOf(const BznMap &map, const std::vector<std::string> &units) {
    std::vector<StartUnit> out;
    for (int slot = 0; slot < kSlots; ++slot) {
        const std::string name = "Camera_" + std::to_string(slot);
        auto path = std::find_if(map.paths.begin(), map.paths.end(),
                                 [&name](const BznMap::Path &p) { return p.name == name && !p.points.empty(); });
        if (path == map.paths.end())
            continue;
        const gfx::Vec3 middle = path->points[0];
        double r = kSpiralStart;
        for (const std::string &odf : units) {
            StartUnit unit{odf, middle};
            if (r <= kSpiralEnd) {
                const double angle = r * kPi * 2;
                unit.at[0] = float(middle[0] + std::sin(angle) * r * kSpiralRadius);
                unit.at[2] = float(middle[2] + std::cos(angle) * r * kSpiralRadius);
                r += kSpiralStep;
            }
            out.push_back(unit);
        }
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
    freeGrid();
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
    case 'r': // the next race's starting units, then none (SIZE_MAX + 1 wraps to the first)
        _race = _race + 1 < std::size(kRaces) ? _race + 1 : SIZE_MAX;
        load();
        break;
    case 'h':
        _showGrid = !_showGrid;
        break;
    case 'v': // the damage emitters: venting plasma and gas
        _meshes.showDamage(!_meshes.showingDamage());
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

void MapPage::freeGrid() {
    if (_renderer) {
        if (_gridMesh)
            _renderer->destroyMesh(_gridMesh);
        if (_gridTexture)
            _renderer->destroyTexture(_gridTexture);
    }
    _gridMesh = 0;
    _gridTexture = 0;
}

void MapPage::makeGrid() {
    freeGrid();
    if (!_renderer || _map.area[1] <= _map.area[0] || _map.area[5] <= _map.area[4])
        return;
    // A square of the grid: its line along two edges, the rest black, which
    // adds nothing.
    std::vector<uint8_t> rgba(size_t(kGridTexels * kGridTexels) * 4, 0);
    for (int y = 0; y < kGridTexels; ++y)
        for (int x = 0; x < kGridTexels; ++x)
            if (x == 0 || y == 0) {
                uint8_t *t = &rgba[size_t(y * kGridTexels + x) * 4];
                t[0] = kGridColour[0], t[1] = kGridColour[1], t[2] = kGridColour[2], t[3] = 255;
            }
    gfx::TextureParams params;
    params.anisotropic = true; // the lines seen at a slant stay lines
    if (_renderer->createTexture(kGridTexels, kGridTexels, rgba.data(), params, _gridTexture))
        _gridTexture = 0;
    // Over the area, out to whole squares.
    const float x0 = std::floor(_map.area[0] / kGridSpacing) * kGridSpacing;
    const float x1 = std::ceil(_map.area[1] / kGridSpacing) * kGridSpacing;
    const float z0 = std::floor(_map.area[4] / kGridSpacing) * kGridSpacing;
    const float z1 = std::ceil(_map.area[5] / kGridSpacing) * kGridSpacing;
    std::vector<gfx::Vertex> corners(4);
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {z0, z0, z1, z1};
    for (size_t i = 0; i < 4; ++i) {
        gfx::Vertex &v = corners[i];
        v.pos[0] = xs[i], v.pos[1] = 0, v.pos[2] = zs[i];
        v.normal[1] = 1;
        v.uv[0] = xs[i] / kGridSpacing;
        v.uv[1] = zs[i] / kGridSpacing;
        std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
    }
    if (_renderer->createMesh(corners, {0, 1, 2, 0, 2, 3}, _gridMesh))
        _gridMesh = 0;
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
        if (built->parts.empty() && built->sprites.empty() && built->emitters.empty()) {
            ++_effects;
            continue;
        }
        _placed.push_back({built, placeOf(o, look->second.scale)});
        ++_drawn;
    }
    _starting = 0;
    if (_race < std::size(kRaces))
        for (const StartUnit &unit : startUnitsOf(_map, standardUnitsOf(kRaces[_race], _files.files()))) {
            const std::string key = text::toLower(unit.odf);
            auto look = _looks.find(key);
            if (look == _looks.end())
                look = _looks.emplace(key, lookOf(unit.odf, _files.files())).first;
            const SodMeshes::Built *built = look->second.model.empty() ? nullptr : _meshes.model(look->second.model);
            if (!built)
                continue;
            // Facing +z, as the game makes them.
            gfx::Mat4 place = gfx::identity();
            place[0] = place[5] = place[10] = look->second.scale;
            place[12] = unit.at[0], place[13] = unit.at[1], place[14] = unit.at[2];
            _placed.push_back({built, place});
            ++_starting;
        }
    // The team parts in the chosen race's colour, or the map's race's: the
    // game's frames show the Federation's grey on the player's ships.
    _meshes.setTeamColour(teamColourOf(_race < std::size(kRaces) ? kRaceNames[_race] : _map.race, _files.files()));
    _meshes.setLighting(lightingOf(_map));
    makeGrid();
    std::string background = _map.background;
    if (text::hasSuffixIgnoreCase(background, ".sod"))
        background.resize(background.size() - 4);
    _background = background.empty() ? nullptr : _meshes.model(background);
    if (_background && _background->parts.empty() && _background->sprites.empty() && _background->emitters.empty())
        _background = nullptr;
    // From above the area's middle, far enough to see it whole.
    _target = {(_map.area[0] + _map.area[1]) / 2, 0, (_map.area[4] + _map.area[5]) / 2};
    _distance = std::max(1000.0f, std::max(_map.area[1] - _map.area[0], _map.area[5] - _map.area[4]));
}

SodMeshes::Lighting MapPage::lightingOf(const BznMap &map) {
    SodMeshes::Lighting lighting;
    SodMeshes::Lighting own;
    lighting.lights.clear();
    for (const BznMap::Object &o : map.objects) {
        if (!text::equalsIgnoreCase(o.odf, "dlight") || o.firstField + 9 >= map.fields.size())
            continue;
        SodMeshes::Lighting::Directed light;
        for (size_t k = 0; k < 3; ++k) {
            const BznMap::Field &f = map.fields[o.firstField + 7 + k];
            if (f.type == BznMap::kFloat && f.data.size() == 4)
                std::memcpy(&light.colour[k], f.data.data(), 4);
            light.colour[k] *= kLightScale;
        }
        // It shines along its front axis, the third row of its transform.
        float len = 0;
        for (size_t k = 0; k < 3; ++k)
            len += o.transform[6 + k] * o.transform[6 + k];
        len = std::sqrt(len);
        if (len <= 0)
            continue;
        for (size_t k = 0; k < 3; ++k)
            light.toLight[k] = -o.transform[6 + k] / len;
        lighting.lights.push_back(light);
    }
    // A map with no lights of its own is lit as the Models page is.
    if (lighting.lights.empty())
        return own;
    for (size_t k = 0; k < 3; ++k)
        lighting.ambient[k] = map.light[k] * kLightScale;
    return lighting;
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
    if (_showGrid && _gridMesh && _gridTexture) {
        gfx::DrawCall grid;
        grid.mesh = _gridMesh;
        grid.indexCount = 6;
        grid.model = gfx::identity();
        grid.texture = _gridTexture;
        grid.state.cull = gfx::Cull::kNone;
        grid.state.src = gfx::Blend::kOne;
        grid.state.dst = gfx::Blend::kOne;
        grid.state.depthWrite = false;
        _renderer->draw(grid);
    }
    _meshes.draw(_placed);

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::string line = text::format("%zu/%zu  %s  ", _current + 1, _names.size(), _names[_current].c_str());
    if (!_error.empty())
        line += "not read: " + _error;
    else
        line += text::format("\"%s\", %zu objects, %zu drawn, %zu with nothing to draw, %zu models not found",
                             _map.description.c_str(), _map.objects.size(), _drawn, _effects, _unmodelled);
    if (_race < std::size(kRaces))
        line += text::format(", %zu starting units (%s)", _starting, kRaces[_race]);
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
