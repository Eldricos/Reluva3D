// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/scene/map_scene.h"

#include "common/text.h"
#include "engines/storm3d/formats/odf.h"
#include "engines/storm3d/scene/model_tree.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::storm3d {

namespace {

// The game's camera sees 45 degrees from top to bottom: tan 22.5 is in its
// memory, and the planets of a frame land where the original has them.
constexpr float kFieldOfView = 0.7853982f; // radians
// The game's overview camera, from RTS_CFG.h: its height above the plane,
// at first and at most and least, and the parabola it rides.
constexpr float kOverviewMinHeight = 10;  // OVERVIEW_MIN_HEIGHT
constexpr float kOverviewMaxHeight = 700; // OVERVIEW_MAX_HEIGHT
constexpr float kOverviewFocus = 200;     // OVERVIEW_PARABOLA_FOCUS

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
// The shroud's cells: 50 units square, as the .mdf's, 128 across a map
// (every map's area is 6400 square). A unit sees a little beyond its
// range: the game's frame clears cells between 716 and 721 units from a
// starbase's, whose range is 700.
constexpr float kShroudCell = 50;
constexpr float kShroudReach = 18;

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

MapScene::Look MapScene::lookOf(const std::string &odf, const SearchSet &files) {
    Look look;
    look.model = odf;
    Odf definition;
    const Odf::Loader loader = loaderFor(files);
    if (definition.load(odf + ".odf", loader))
        return look; // no definition: the model by the same name, if there is one
    if (definition.number("dont_load_SOD") != 0)
        look.model.clear();
    look.scale = float(definition.number("scaleSOD", 1));
    look.scanRange = float(definition.number("rangeScan"));
    if (look.scale <= 0)
        look.scale = 1;
    if (text::equalsIgnoreCase(definition.string("classLabel"), "asteroidfield")) {
        look.minAsteroids = int(definition.number("minAsteroids"));
        look.maxAsteroids = std::max(look.minAsteroids, int(definition.number("maxAsteroids")));
        look.fieldRadius = float(definition.number("fieldRadius"));
    }
    return look;
}

std::vector<std::pair<std::string, gfx::Mat4>> MapScene::asteroidsOf(const BznMap::Object &o, const Look &look) {
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

std::optional<gfx::Vec3> MapScene::teamColourOf(const std::string &name, const SearchSet &files) {
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

std::vector<std::string> MapScene::standardUnitsOf(const std::string &race, const SearchSet &files) {
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

std::vector<MapScene::StartUnit> MapScene::startUnitsOf(const BznMap &map, const std::vector<std::string> &units) {
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
            StartUnit unit{odf, middle, slot, std::nullopt};
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

std::vector<bool> MapScene::seenOf(const BznMap &map, int cells, const std::vector<Sight> &sights) {
    std::vector<bool> seen(size_t(cells * cells), false);
    const float size = map.area[1] - map.area[0];
    if (cells <= 0 || size <= 0)
        return seen;
    const float cell = size / float(cells);
    for (const Sight &sight : sights) {
        // Counted from the unit's cell; rows from the area's near edge.
        const int uc = int(std::floor((sight.at[0] - map.area[0]) / cell));
        const int ur = int(std::floor((sight.at[2] - map.area[4]) / cell));
        const float reach = (sight.range + kShroudReach) / cell;
        for (int r = 0; r < cells; ++r)
            for (int c = 0; c < cells; ++c) {
                const float dc = float(c - uc);
                const float dr = float(r - ur);
                if (dc * dc + dr * dr <= reach * reach)
                    seen[size_t((cells - 1 - r) * cells + c)] = true;
            }
    }
    return seen;
}

gfx::Mat4 MapScene::placeOf(const BznMap::Object &o, float scale) {
    gfx::Mat4 s = gfx::identity();
    s[0] = s[5] = s[10] = scale;
    return gfx::multiply(ModelTree::toMatrix(o.transform), s);
}

// The grid's and the shroud's, which are made together.
void MapScene::freeGrid() {
    if (_renderer) {
        for (const gfx::MeshId m : {_gridMesh, _shroudMesh})
            if (m)
                _renderer->destroyMesh(m);
        for (const gfx::TextureId t : {_gridTexture, _shroudTexture})
            if (t)
                _renderer->destroyTexture(t);
    }
    _gridMesh = _shroudMesh = 0;
    _gridTexture = _shroudTexture = 0;
}

bool MapScene::shrouded(const gfx::Vec3 &at) const {
    const float size = _map.area[1] - _map.area[0];
    const int cells = int(std::lround(size / kShroudCell));
    if (_seen.empty() || at[1] < 0 || size <= 0)
        return false;
    const int c = int(std::floor((at[0] - _map.area[0]) / kShroudCell));
    const int r = int(std::floor((at[2] - _map.area[4]) / kShroudCell));
    // Outside the area there's no shroud: the game's frame shows space
    // past its edge clear.
    if (c < 0 || r < 0 || c >= cells || r >= cells)
        return false;
    return !_seen[size_t((cells - 1 - r) * cells + c)];
}

void MapScene::makeShroud(const std::vector<bool> &seen) {
    const float size = _map.area[1] - _map.area[0];
    const int cells = int(std::lround(size / kShroudCell));
    if (!_renderer || seen.size() != size_t(cells * cells) || cells <= 0)
        return;
    // gshroud's picture, a texel a cell (it's 128 square), with the cells
    // seen left out.
    image::Image picture;
    std::vector<uint8_t> data;
    if (_files.read("Textures/RGB/gshroud.tga", data) || image::decode(data, picture) || picture.w <= 0)
        picture = {};
    std::vector<uint8_t> rgba(size_t(cells * cells) * 4, 128);
    for (int r = 0; r < cells; ++r)
        for (int c = 0; c < cells; ++c) {
            uint8_t *t = &rgba[size_t(r * cells + c) * 4];
            if (picture.w > 0) {
                const size_t from = size_t((r * picture.h / cells) * picture.w + c * picture.w / cells) * 4;
                std::copy_n(&picture.rgba[from], 3, t);
            }
            t[3] = seen[size_t(r * cells + c)] ? 0 : 255;
        }
    gfx::TextureParams params;
    params.clamp = true;
    params.mipmaps = false;
    if (_renderer->createTexture(cells, cells, rgba.data(), params, _shroudTexture))
        _shroudTexture = 0;
    // Over the area on the plane y = 0, its texture's top row the far edge.
    const float x0 = _map.area[0];
    const float x1 = _map.area[1];
    const float z0 = _map.area[4];
    const float z1 = _map.area[5];
    std::vector<gfx::Vertex> corners(4);
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {z0, z0, z1, z1};
    for (size_t i = 0; i < 4; ++i) {
        gfx::Vertex &v = corners[i];
        v.pos[0] = xs[i], v.pos[1] = 0, v.pos[2] = zs[i];
        v.normal[1] = 1;
        v.uv[0] = (xs[i] - x0) / (x1 - x0);
        v.uv[1] = 1 - (zs[i] - z0) / (z1 - z0);
        std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
    }
    if (_renderer->createMesh(corners, {0, 1, 2, 0, 2, 3}, _shroudMesh))
        _shroudMesh = 0;
}

void MapScene::makeGrid() {
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

const MapScene::Look &MapScene::lookFor(const std::string &odf) {
    const std::string key = text::toLower(odf);
    auto look = _looks.find(key);
    if (look == _looks.end())
        look = _looks.emplace(key, lookOf(odf, _files.files())).first;
    return look->second;
}

SodMeshes::Lighting MapScene::lightingOf(const BznMap &map) {
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
        lighting.ambient[k] = map.light[k];
    return lighting;
}

MapScene::Camera MapScene::overviewOf(const gfx::Vec3 &target, float height, float turn) {
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

void MapScene::attach(gfx::Renderer3D *renderer) {
    if (renderer == _renderer)
        return;
    clear();
    _meshes.attach(renderer);
    _renderer = renderer;
}

void MapScene::clear() {
    freeGrid();
    _meshes.clear();
    _placed.clear();
    _teams.clear();
    _units.clear();
    _unitModels.clear();
    _background = nullptr;
    _seen.clear();
    _drawn = _unmodelled = _effects = _starting = 0;
}

std::vector<MapScene::Sight> MapScene::sightsOf(const BznMap &map, const std::vector<StartUnit> &units, int team) {
    std::vector<Sight> sights;
    for (const StartUnit &unit : units)
        if (unit.slot == units.front().slot)
            sights.push_back({unit.at, lookFor(unit.odf).scanRange});
    for (const BznMap::Object &o : map.objects)
        if (team >= 0 && o.team == team)
            sights.push_back({o.position, lookFor(o.odf).scanRange});
    return sights;
}

void MapScene::build(const BznMap &map, const std::vector<StartUnit> &units, const std::string &teamColour,
                     const std::vector<Sight> &sights) {
    clear();
    _map = map;
    if (!_renderer)
        return;
    if (!sights.empty())
        _seen = seenOf(_map, int(std::lround((_map.area[1] - _map.area[0]) / kShroudCell)), sights);
    for (const BznMap::Object &o : _map.objects) {
        // The game draws nothing of the map in the shroud but what's below
        // its plane, which the shroud covers.
        if (shrouded(o.position))
            continue;
        const Look &look = lookFor(o.odf);
        const size_t before = _placed.size();
        for (const auto &[model, place] : asteroidsOf(o, look))
            if (const SodMeshes::Built *built = _meshes.model(model))
                _placed.push_back({built, place});
        if (_placed.size() > before)
            ++_drawn;
        // A sprite node named for the definition is the object's too, at
        // its centre: the planets' glows ("Nimbus for planets", planets.spr).
        if (const SodMeshes::Built *glow = _meshes.spriteNode(o.odf))
            _placed.push_back({glow, placeOf(o, look.scale)});
        if (look.model.empty())
            continue;
        const SodMeshes::Built *built = _meshes.model(look.model);
        if (!built) {
            ++_unmodelled;
            continue;
        }
        if (built->parts.empty() && built->sprites.empty() && built->emitters.empty()) {
            ++_effects;
            continue;
        }
        _placed.push_back({built, placeOf(o, look.scale)});
        ++_drawn;
    }
    _starting = 0;
    for (const StartUnit &unit : units) {
        const Look &look = lookFor(unit.odf);
        const SodMeshes::Built *built = look.model.empty() ? nullptr : _meshes.model(look.model);
        if (!built || shrouded(unit.at))
            continue;
        _teams.resize(_placed.size());
        _placed.push_back({built, gfx::identity()});
        _teams.push_back(unit.colour);
        Unit u;
        u.odf = unit.odf;
        u.slot = unit.slot;
        u.colour = unit.colour;
        _units.push_back(u);
        UnitModel m;
        m.placed = _placed.size() - 1;
        m.scale = look.scale;
        for (size_t k = 0; k < 3; ++k) {
            m.lo[k] = built->lo[k] * look.scale;
            m.hi[k] = built->hi[k] * look.scale;
        }
        _unitModels.push_back(m);
        // Facing +z, as the game makes them.
        moveUnit(_units.size() - 1, unit.at, 0);
        ++_starting;
    }
    // The team parts in the chosen race's colour, or the map's race's: the
    // game's frames show the Federation's grey on the player's ships.
    _meshes.setTeamColour(teamColourOf(teamColour.empty() ? _map.race : teamColour, _files.files()));
    _meshes.setLighting(lightingOf(_map));
    makeGrid();
    if (!_seen.empty())
        makeShroud(_seen);
    std::string background = _map.background;
    if (text::hasSuffixIgnoreCase(background, ".sod"))
        background.resize(background.size() - 4);
    _background = background.empty() ? nullptr : _meshes.model(background);
    if (_background && _background->parts.empty() && _background->sprites.empty() && _background->emitters.empty())
        _background = nullptr;
}

float MapScene::Unit::radius() const {
    const float dx = (hi[0] - lo[0]) / 2;
    const float dy = (hi[1] - lo[1]) / 2;
    const float dz = (hi[2] - lo[2]) / 2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void MapScene::moveUnit(size_t unit, const gfx::Vec3 &at, float heading) {
    if (unit >= _units.size())
        return;
    const UnitModel &m = _unitModels[unit];
    // Its model's +z along its heading, +x to its right: (cos, 0, -sin).
    const float s = std::sin(heading);
    const float c = std::cos(heading);
    gfx::Mat4 &place = _placed[m.placed].second;
    place = gfx::identity();
    place[0] = c * m.scale, place[2] = -s * m.scale;
    place[5] = m.scale;
    place[8] = s * m.scale, place[10] = c * m.scale;
    place[12] = at[0], place[13] = at[1], place[14] = at[2];
    Unit &u = _units[unit];
    u.at = at;
    u.heading = heading;
    // The box about its turned corners.
    u.lo = {at[0] + std::min(c * m.lo[0], c * m.hi[0]) + std::min(s * m.lo[2], s * m.hi[2]), at[1] + m.lo[1],
            at[2] + std::min(-s * m.lo[0], -s * m.hi[0]) + std::min(c * m.lo[2], c * m.hi[2])};
    u.hi = {at[0] + std::max(c * m.lo[0], c * m.hi[0]) + std::max(s * m.lo[2], s * m.hi[2]), at[1] + m.hi[1],
            at[2] + std::max(-s * m.lo[0], -s * m.hi[0]) + std::max(c * m.lo[2], c * m.hi[2])};
}

std::optional<gfx::Vec3> MapScene::pointOn(const Camera &camera, float height, int x, int y) {
    // Right, in the game's left-handed coordinates: up x forward.
    const gfx::Vec3 &u = camera.up;
    const gfx::Vec3 &f = camera.forward;
    const gfx::Vec3 right{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    const float focal = 240 / std::tan(camera.fovY / 2);
    gfx::Vec3 ray{};
    for (size_t k = 0; k < 3; ++k)
        ray[k] = f[k] + right[k] * (float(x) - 320) / focal + u[k] * (240 - float(y)) / focal;
    if (std::abs(ray[1]) < 1e-6f)
        return std::nullopt;
    const float t = (height - camera.eye[1]) / ray[1];
    if (t <= 0)
        return std::nullopt;
    return gfx::Vec3{camera.eye[0] + ray[0] * t, height, camera.eye[2] + ray[2] * t};
}

gfx::Mat4 MapScene::viewProjOf(const Camera &camera, float range, float aspect) {
    // The renderer is right-handed, the game left-handed: the camera's
    // place and axes have z reversed for it (SodMeshes::mirrorZ), and what
    // it draws is in the game's coordinates.
    auto flip = [](const gfx::Vec3 &v) { return gfx::Vec3{v[0], v[1], -v[2]}; };
    return gfx::multiply(gfx::multiply(gfx::perspective(camera.fovY, aspect, range * 0.02f, range * 4 + 20000),
                                       gfx::lookAlong(flip(camera.eye), flip(camera.forward), flip(camera.up))),
                         SodMeshes::mirrorZ());
}

std::optional<gfx::Vec3> MapScene::project(const gfx::Mat4 &viewProj, const gfx::Vec3 &point) {
    // Column-major, as gfx's matrices are.
    const float x = viewProj[0] * point[0] + viewProj[4] * point[1] + viewProj[8] * point[2] + viewProj[12];
    const float y = viewProj[1] * point[0] + viewProj[5] * point[1] + viewProj[9] * point[2] + viewProj[13];
    const float z = viewProj[2] * point[0] + viewProj[6] * point[1] + viewProj[10] * point[2] + viewProj[14];
    const float w = viewProj[3] * point[0] + viewProj[7] * point[1] + viewProj[11] * point[2] + viewProj[15];
    if (w <= 0)
        return std::nullopt;
    return gfx::Vec3{(x / w * 0.5f + 0.5f) * 640, (0.5f - y / w * 0.5f) * 480, z / w};
}

void MapScene::draw(const Camera &camera, double seconds, float range, Color clear, bool grid) {
    if (!_renderer)
        return;
    _renderer->beginFrame(viewProjOf(camera, range), camera.eye, clear);
    drawContents(camera, seconds, grid, true);
}

void MapScene::drawInset(const Camera &camera, double seconds, float range, int x, int y, int w, int h) {
    if (!_renderer || w <= 0 || h <= 0)
        return;
    // The screen's 640 × 480 to the renderer's frame.
    const float sx = float(_renderer->width()) / 640;
    const float sy = float(_renderer->height()) / 480;
    _renderer->setViewport(int(std::lround(x * sx)), int(std::lround(y * sy)), int(std::lround(w * sx)),
                           int(std::lround(h * sy)));
    _renderer->clearDepth();
    _renderer->setView(viewProjOf(camera, range, float(w) / float(h)), camera.eye);
    drawContents(camera, seconds, false, false);
    _renderer->setViewport(0, 0, 0, 0);
}

void MapScene::drawContents(const Camera &camera, double seconds, bool grid, bool withShroud) {
    const gfx::Vec3 &eye = camera.eye;
    // Right, in the game's left-handed coordinates, is up × forward.
    const gfx::Vec3 &u = camera.up;
    const gfx::Vec3 &f = camera.forward;
    _meshes.setView({u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]}, u, seconds);
    // The background round the eye, behind everything: how the game
    // places it isn't known.
    if (_background) {
        gfx::Mat4 at = gfx::identity();
        at[12] = eye[0], at[13] = eye[1], at[14] = eye[2];
        _meshes.draw(*_background, at, true);
    }
    if (grid && _gridMesh && _gridTexture) {
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
    // The shroud after the grid, over it and what's below the plane.
    if (withShroud && _shroudMesh && _shroudTexture) {
        gfx::DrawCall shroud;
        shroud.mesh = _shroudMesh;
        shroud.indexCount = 6;
        shroud.model = gfx::identity();
        shroud.texture = _shroudTexture;
        shroud.state.cull = gfx::Cull::kNone;
        shroud.state.alphaTest = gfx::AlphaTest::kGe128;
        shroud.state.depthWrite = false;
        _renderer->draw(shroud);
    }
    _teams.resize(_placed.size());
    _meshes.draw(_placed, false, &_teams);
}

} // namespace reluva::storm3d
