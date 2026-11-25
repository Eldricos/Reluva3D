// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"
#include "engines/storm3d/formats/odf.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::storm3d {

namespace {

constexpr Color kClear = {0, 0, 0};
// RTS_CFG.h's limits on the overview's height.
constexpr float kMinHeight = 10;  // OVERVIEW_MIN_HEIGHT
constexpr float kMaxHeight = 700; // OVERVIEW_MAX_HEIGHT
constexpr float kFocus = 200;     // OVERVIEW_PARABOLA_FOCUS
// Fitted to the camera in the game's memory before and after its keys
// (camera.md): + held 0.6 s took the height from 450 to 333.4, e^-0.3, so
// it zooms by e^0.5 a second; * held 0.6 s turned it 17.3 degrees; an
// arrow held 0.6 s at 333.4 high moved the target 908, about
// KEYBOARD_SCROLL_RATE (6) times the camera's distance behind it a second.
constexpr double kZoomPerSecond = 0.5;
constexpr double kTurnPerSecond = 0.5;
constexpr double kScrollRate = 6; // KEYBOARD_SCROLL_RATE
// How far the camera looks, for its near and far planes.
constexpr float kRange = 1000;
// Pixels the pointer may move between press and release, and still click.
constexpr int kClickSlop = 4;
// The ring lies near the map's plane: the starbase's and a ship's, placed
// back in the world, at 3.2 and 7.0.
constexpr float kRingHeight = 5;
// Pixels between the health bar's top and its unit's box's top.
constexpr int kBarAbove = 6;

} // namespace

int MissionView::actionOf(uint32_t key) {
    switch (key) {
    case key::kKpPlus:
        return kZoomIn;
    case key::kKpMinus:
        return kZoomOut;
    case key::kKpDivide:
        return kTurnLeft;
    case key::kKpMultiply:
        return kTurnRight;
    case key::kLeft:
        return kScrollLeft;
    case key::kRight:
        return kScrollRight;
    case key::kUp:
        return kScrollUp;
    case key::kDown:
        return kScrollDown;
    default:
        return -1;
    }
}

MissionView::Overview MissionView::moved(Overview o, const std::set<int> &actions, double seconds) {
    const double zoom = (actions.count(kZoomOut) ? 1 : 0) - (actions.count(kZoomIn) ? 1 : 0);
    o.height = std::clamp(float(o.height * std::exp(zoom * kZoomPerSecond * seconds)), kMinHeight, kMaxHeight);
    // * turns it the way the game's memory showed: to a smaller angle.
    o.turn +=
        float(((actions.count(kTurnLeft) ? 1 : 0) - (actions.count(kTurnRight) ? 1 : 0)) * kTurnPerSecond * seconds);
    const double across = (actions.count(kScrollRight) ? 1 : 0) - (actions.count(kScrollLeft) ? 1 : 0);
    const double ahead = (actions.count(kScrollUp) ? 1 : 0) - (actions.count(kScrollDown) ? 1 : 0);
    const double step = kScrollRate * std::sqrt(kFocus * o.height) * seconds;
    // Right is (cos a, -sin a) in the game's left-handed coordinates; ahead (sin a, cos a).
    const double s = std::sin(o.turn);
    const double c = std::cos(o.turn);
    o.target[0] += float((across * c + ahead * s) * step);
    o.target[2] += float((-across * s + ahead * c) * step);
    return o;
}

Hud::Resources MissionView::resourcesOf(const Hud::Resources &start, const std::vector<std::string> &units,
                                        const SearchSet &files) {
    Hud::Resources r = start;
    r.officers = 0;
    const Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
    for (const std::string &unit : units) {
        Odf odf;
        if (odf.load(unit + ".odf", loader))
            continue;
        r.officers += int(odf.number("officerCost"));
        r.maxOfficers += int(odf.number("officerGain"));
    }
    return r;
}

std::string MissionView::hudRaceOf(const std::string &race) {
    static const std::map<std::string, std::string> kNames = {
        {"fed", "fed"}, {"federation", "fed"}, {"kling", "kli"}, {"klingon", "kli"},
        {"rom", "rom"}, {"romulan", "rom"},    {"borg", "bor"},  {"bor", "bor"}};
    auto it = kNames.find(race);
    return it == kNames.end() ? "fed" : it->second;
}

MissionView::Overview MissionView::startOf(const BznMap &map, int slot) {
    Overview o;
    o.target = {(map.area[0] + map.area[1]) / 2, 0, (map.area[4] + map.area[5]) / 2};
    const std::string name = "Camera_" + std::to_string(slot);
    for (const BznMap::Path &p : map.paths)
        if (p.name == name && !p.points.empty()) {
            o.target = p.points[0];
            break;
        }
    return o;
}

Error MissionView::start(gfx::Renderer3D *renderer, const std::string &map, int slot, int team, uint64_t millis) {
    stop();
    BznMap parsed;
    std::vector<uint8_t> data;
    if (Error err = _files.read("bzn/" + map, data))
        return err;
    if (Error err = parsed.parse(data))
        return Error::fail(map + ": " + err.message);
    _renderer = renderer;
    _scene.attach(renderer);
    _scene.build(parsed, {}, "", team >= 0 ? _scene.sightsOf(parsed, {}, team) : std::vector<MapScene::Sight>{});
    _overview = startOf(parsed, slot);
    if (_hud.load(_files, hudRaceOf(text::toLower(parsed.race))))
        _hud = Hud{};
    _resources.reset();
    _held.clear();
    _last = millis;
    return Error::success();
}

Error MissionView::startSkirmish(gfx::Renderer3D *renderer, const std::string &map, const std::vector<Player> &players,
                                 bool shroud, const Hud::Resources &start, uint64_t millis) {
    stop();
    BznMap parsed;
    std::vector<uint8_t> data;
    if (Error err = _files.read("bzn/" + map, data))
        return err;
    if (Error err = parsed.parse(data))
        return Error::fail(map + ": " + err.message);
    _renderer = renderer;
    _scene.attach(renderer);
    std::vector<MapScene::StartUnit> units;
    std::vector<MapScene::StartUnit> mine;
    int slot = 1;
    for (const Player &p : players) {
        for (MapScene::StartUnit &u : MapScene::startUnitsOf(parsed, MapScene::standardUnitsOf(p.race, _files.files())))
            if (u.slot == p.slot) {
                u.colour = p.colour;
                units.push_back(u);
                if (p.human)
                    mine.push_back(u);
            }
        if (p.human)
            slot = p.slot;
    }
    _scene.build(parsed, units, "",
                 shroud && !mine.empty() ? _scene.sightsOf(parsed, mine, -1) : std::vector<MapScene::Sight>{});
    _overview = startOf(parsed, slot);
    std::vector<std::string> myUnits;
    std::string myRace = "fed";
    for (const Player &p : players)
        if (p.human)
            myRace = p.race;
    for (const MapScene::StartUnit &u : mine)
        myUnits.push_back(u.odf);
    if (_hud.load(_files, hudRaceOf(myRace)))
        _hud = Hud{};
    _resources = resourcesOf(start, myUnits, _files.files());
    _held.clear();
    _last = millis;
    return Error::success();
}

int MissionView::unitAt(const std::vector<MapScene::Unit> &units, const gfx::Mat4 &viewProj,
                        const MapScene::Camera &camera, int x, int y) {
    int best = -1;
    float bestDistance = 0;
    // Right, in the game's left-handed coordinates: up × forward.
    const gfx::Vec3 &u = camera.up;
    const gfx::Vec3 &f = camera.forward;
    const gfx::Vec3 right{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    for (size_t i = 0; i < units.size(); ++i) {
        const gfx::Vec3 middle = units[i].middle();
        const float r = units[i].radius();
        const auto at = MapScene::project(viewProj, middle);
        const auto edge =
            MapScene::project(viewProj, {middle[0] + right[0] * r, middle[1] + right[1] * r, middle[2] + right[2] * r});
        if (!at || !edge)
            continue;
        const float reach = std::abs((*edge)[0] - (*at)[0]);
        const float dx = float(x) + 0.5f - (*at)[0];
        const float dy = float(y) + 0.5f - (*at)[1];
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= reach && (best < 0 || distance < bestDistance)) {
            best = int(i);
            bestDistance = distance;
        }
    }
    return best;
}

gfx::Vec3 MissionView::ringMiddleOf(const MapScene::Unit &unit) {
    return {unit.at[0], kRingHeight, unit.at[2]};
}

float MissionView::ringHalfOf(const MapScene::Unit &unit) {
    // Its box's half diagonal across: 52.6 for the starbase, 27.3 for a
    // construction ship (their draws placed back in the world).
    return std::hypot((unit.hi[0] - unit.lo[0]) / 2, (unit.hi[2] - unit.lo[2]) / 2);
}

std::array<int, 3> MissionView::healthBarOf(const MapScene::Unit &unit, const gfx::Mat4 &viewProj) {
    // Across the box as it falls on the screen, all eight corners, and a
    // little above its top: the starbase's bar spans 381 to 490 where its
    // box does 381.4 to 489.4, its top 6 above the box's 110.4.
    float left = 1e9f;
    float right = -1e9f;
    float top = 1e9f;
    for (int c = 0; c < 8; ++c) {
        const gfx::Vec3 corner{(c & 1) ? unit.hi[0] : unit.lo[0], (c & 2) ? unit.hi[1] : unit.lo[1],
                               (c & 4) ? unit.hi[2] : unit.lo[2]};
        const auto at = MapScene::project(viewProj, corner);
        if (!at)
            return {0, 0, 0};
        left = std::min(left, (*at)[0]);
        right = std::max(right, (*at)[0]);
        top = std::min(top, (*at)[1]);
    }
    return {int(std::floor(left)), int(std::floor(top)) - kBarAbove, int(std::ceil(right) - std::floor(left))};
}

MapScene::Camera MissionView::camera() const {
    return MapScene::overviewOf(_overview.target, _overview.height, _overview.turn);
}

void MissionView::makeRing() {
    if (!_renderer || _ringMesh)
        return;
    // Gshadow.tga, the game's selection ring, as the original's memory has
    // the texture its draw used.
    image::Image picture;
    std::vector<uint8_t> data;
    if (_files.read("Textures/RGB/Gshadow.tga", data) || image::decode(data, picture) || picture.w <= 0)
        return;
    gfx::TextureParams params;
    params.clamp = true;
    if (_renderer->createTexture(picture.w, picture.h, picture.rgba.data(), params, _ringTexture))
        _ringTexture = 0;
    // A unit square on the plane, its texture turned a quarter, as the
    // draw's corners have it.
    std::vector<gfx::Vertex> corners(4);
    const float xs[4] = {1, 1, -1, -1};
    const float zs[4] = {1, -1, -1, 1};
    const float us[4] = {0, 1, 1, 0};
    const float vs[4] = {0, 0, 1, 1};
    for (size_t i = 0; i < 4; ++i) {
        gfx::Vertex &v = corners[i];
        v = gfx::Vertex{};
        v.pos[0] = xs[i], v.pos[2] = zs[i];
        v.normal[1] = 1;
        v.uv[0] = us[i], v.uv[1] = vs[i];
        std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
    }
    if (_renderer->createMesh(corners, {0, 1, 2, 0, 2, 3}, _ringMesh))
        _ringMesh = 0;
}

void MissionView::stop() {
    if (_renderer) {
        if (_ringMesh)
            _renderer->destroyMesh(_ringMesh);
        if (_ringTexture)
            _renderer->destroyTexture(_ringTexture);
    }
    _ringMesh = 0;
    _ringTexture = 0;
    _selected = _hover = -1;
    _scene.attach(nullptr);
    _renderer = nullptr;
}

void MissionView::handleEvent(const Event &ev) {
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && !_hud.covers(ev.mouseX, ev.mouseY)) {
        _pressX = ev.mouseX;
        _pressY = ev.mouseY;
        return;
    }
    if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && _pressX >= 0) {
        // A click, not a drag: the unit under it, or none.
        if (std::abs(ev.mouseX - _pressX) + std::abs(ev.mouseY - _pressY) <= kClickSlop)
            _selected = unitAt(_scene.units(), MapScene::viewProjOf(camera(), kRange), camera(), ev.mouseX, ev.mouseY);
        _pressX = _pressY = -1;
        return;
    }
    if (ev.type != EventType::kKeyDown && ev.type != EventType::kKeyUp)
        return;
    const int action = actionOf(ev.key);
    if (action < 0)
        return;
    if (ev.type == EventType::kKeyDown)
        _held.insert(action);
    else
        _held.erase(action);
}

void MissionView::draw(uint64_t millis) {
    if (!_renderer)
        return;
    _overview = moved(_overview, _held, double(millis - _last) / 1000);
    _last = millis;
    const MapScene::Camera view = camera();
    _scene.draw(view, double(millis) / 1000, kRange, kClear, true);
    const gfx::Mat4 viewProj = MapScene::viewProjOf(view, kRange);
    const std::vector<MapScene::Unit> &units = _scene.units();
    _hover = _hud.covers(_mouseX, _mouseY) ? -1 : unitAt(units, viewProj, view, _mouseX, _mouseY);
    const MapScene::Unit *chosen =
        _selected >= 0 && size_t(_selected) < units.size() ? &units[size_t(_selected)] : nullptr;
    makeRing();
    if (chosen && _ringMesh && _ringTexture) {
        // Added on in (229, 229, 229), as the draw is.
        const gfx::Vec3 middle = ringMiddleOf(*chosen);
        const float half = ringHalfOf(*chosen);
        gfx::DrawCall ring;
        ring.mesh = _ringMesh;
        ring.indexCount = 6;
        ring.model = gfx::identity();
        ring.model[0] = ring.model[10] = half;
        ring.model[12] = middle[0], ring.model[13] = middle[1], ring.model[14] = middle[2];
        ring.texture = _ringTexture;
        ring.color = {0.898f, 0.898f, 0.898f, 1};
        ring.state.cull = gfx::Cull::kNone;
        ring.state.src = gfx::Blend::kOne;
        ring.state.dst = gfx::Blend::kOne;
        ring.state.depthWrite = false;
        _renderer->draw(ring);
    }
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    if (chosen) {
        const std::array<int, 3> bar = healthBarOf(*chosen, viewProj);
        if (bar[2] > 0)
            _hud.drawHealthBar(_overlay, bar[0], bar[1], bar[2], 1);
    }
    _hud.draw(_overlay, _resources.value_or(Hud::Resources{}), _scene.seen(), _resources.has_value());
    _hud.drawCursor(_overlay, _mouseX, _mouseY, double(millis) / 1000, _hover >= 0);
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::storm3d
