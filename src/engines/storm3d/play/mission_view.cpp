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

Odf::Loader loaderOf(const SearchSet &files) {
    return [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
}

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
    const Odf::Loader loader = loaderOf(files);
    for (const std::string &unit : units) {
        Odf odf;
        if (odf.load(unit + ".odf", loader))
            continue;
        r.officers += int(odf.number("officerCost"));
        r.maxOfficers += int(odf.number("officerGain"));
    }
    return r;
}

Hud::Unit MissionView::displayOf(const std::string &odfName, const Odf::Loader &loader, const Hud &hud, size_t pick) {
    Hud::Unit u;
    Odf odf;
    if (odf.load(odfName + ".odf", loader))
        return u;
    const std::string race = text::toLower(odf.string("race", "federation"));
    u.raceIcon = race + "_icon";
    u.className = odf.string("unitName");
    const std::vector<std::string> names = odf.strings("possibleCraftNames");
    if (!names.empty())
        u.name = names[pick % names.size()];
    u.crew = int(odf.number("crewCost"));
    // A ship without maximumCrew has room for no more than it comes with:
    // the construction ship's 500/500.
    u.maxCrew = int(odf.number("maximumCrew", u.crew));
    u.officers = int(odf.number("officerCost"));
    // Its wireframe: the race's letter, W, the rest of its name (FWbase).
    if (!odfName.empty())
        u.wireframe =
            std::string(1, char(std::toupper(static_cast<unsigned char>(odfName[0])))) + "W" + odfName.substr(1);
    // A system it has costs crew when hit; the starbase, without engines,
    // loses none to them.
    static const char *const kSystems[] = {"shieldGenerator", "engines", "weapons", "lifeSupport", "sensors"};
    for (size_t i = 0; i < u.systems.size(); ++i)
        u.systems[i] = odf.number(std::string(kSystems[i]) + "CrewLoss") > 0;
    bool attacks = false;
    for (int i = 1;; ++i) {
        const std::string weapon = odf.string("weapon" + std::to_string(i));
        if (weapon.empty())
            break;
        const std::string button = "b_" + text::toLower(weapon);
        if (!hud.hasSprite(button))
            attacks = true;
        else if (std::find(u.specials.begin(), u.specials.end(), button) == u.specials.end())
            u.specials.push_back(button);
    }
    const bool starbase = text::equalsIgnoreCase(odf.string("classLabel"), "starbase");
    const bool moves = odf.number("enginesHitPoints") > 0;
    u.controls.push_back({1, "b_move", "bb_standard", moves});
    u.controls.push_back({2, "b_attack", "bb_standard", attacks});
    u.controls.push_back({3, "b_stop", "bb_standard", true});
    // Seen dark on the construction ship, the one ship seen yet.
    if (!starbase)
        u.controls.push_back({4, "b_repair", "bb_standard", false});
    u.controls.push_back({5, "b_order", "bb_standard", true});
    u.controls.push_back({6, "b_standdown", "bb_standard", true});
    if (starbase) {
        static const std::map<std::string, std::string> kOfficers = {
            {"federation", "b_fedoff"}, {"klingon", "b_klingoff"}, {"romulan", "b_romoff"}, {"borg", "b_borgoff"}};
        auto officers = kOfficers.find(race);
        u.controls.push_back({7, "b_recrew", "bb_standard", true});
        u.controls.push_back({9, officers == kOfficers.end() ? "b_fedoff" : officers->second, "bb_build", true});
    }
    if (!odf.string("buildItem0").empty())
        u.controls.push_back({10, "b_build", "bb_standard", true});
    if (starbase)
        u.controls.push_back({11, "b_rally", "bb_standard", true});
    return u;
}

void MissionView::sendTo(std::vector<Movement::Ship> &ships, const std::vector<MapScene::Unit> &units,
                         const std::vector<int> &selection, const std::vector<bool> &moves, const gfx::Vec3 &point) {
    // Ships keep the rings' half sides apart, as the original's way round
    // the starbase did.
    std::vector<Movement::Obstacle> standing;
    for (size_t i = 0; i < units.size(); ++i)
        if (i >= moves.size() || !moves[i])
            standing.push_back({ringMiddleOf(units[i]), ringHalfOf(units[i])});
    std::vector<size_t> going;
    gfx::Vec3 middle{};
    for (int i : selection)
        if (size_t(i) < ships.size() && size_t(i) < moves.size() && moves[size_t(i)]) {
            going.push_back(size_t(i));
            for (size_t k = 0; k < 3; ++k)
                middle[k] += ships[size_t(i)].at[k];
        }
    if (going.empty())
        return;
    for (float &m : middle)
        m /= float(going.size());
    for (size_t i : going) {
        ships[i].goal =
            gfx::Vec3{point[0] + ships[i].at[0] - middle[0], ships[i].at[1], point[2] + ships[i].at[2] - middle[2]};
        ships[i].waiting = Movement::kSetOff;
        ships[i].route = i < units.size()
                             ? Movement::routeAround(ships[i].at, *ships[i].goal, standing, ringHalfOf(units[i]))
                             : std::vector<gfx::Vec3>{};
    }
}

std::vector<int> MissionView::boxed(const std::vector<MapScene::Unit> &units, const std::vector<bool> &moves,
                                    const gfx::Mat4 &viewProj, int slot, int x0, int y0, int x1, int y1) {
    std::vector<int> inside;
    bool anyMoves = false;
    for (size_t i = 0; i < units.size(); ++i) {
        if (slot >= 0 && units[i].slot != slot)
            continue;
        const auto at = MapScene::project(viewProj, units[i].middle());
        if (!at || (*at)[0] < std::min(x0, x1) || (*at)[0] > std::max(x0, x1) || (*at)[1] < std::min(y0, y1) ||
            (*at)[1] > std::max(y0, y1))
            continue;
        inside.push_back(int(i));
        anyMoves = anyMoves || (i < moves.size() && moves[i]);
    }
    // The starbase in a box with the construction ships was left out of
    // it; alone, a box selected it.
    if (anyMoves)
        std::erase_if(inside, [&moves](int i) { return size_t(i) >= moves.size() || !moves[size_t(i)]; });
    return inside;
}

Hud::Unit MissionView::ordersOf(const std::vector<Hud::Unit> &units) {
    if (units.size() == 1)
        return units.front();
    Hud::Unit orders;
    for (const Hud::Unit &u : units) {
        for (const Hud::Unit::Button &b : u.controls) {
            auto same = std::find_if(orders.controls.begin(), orders.controls.end(),
                                     [&b](const Hud::Unit::Button &o) { return o.slot == b.slot; });
            if (same == orders.controls.end())
                orders.controls.push_back(b);
            else
                same->enabled = same->enabled || b.enabled;
        }
        for (const std::string &s : u.specials)
            if (std::find(orders.specials.begin(), orders.specials.end(), s) == orders.specials.end())
                orders.specials.push_back(s);
    }
    // Two construction ships had their build button dark.
    for (Hud::Unit::Button &b : orders.controls)
        if (b.sprite == "b_build")
            b.enabled = false;
    std::sort(orders.controls.begin(), orders.controls.end(),
              [](const Hud::Unit::Button &a, const Hud::Unit::Button &b) { return a.slot < b.slot; });
    return orders;
}

MapScene::Camera MissionView::closeUpOf(const MapScene::Unit &unit) {
    // Fitted to the original's draws of a construction ship standing, its
    // hull in the panel within 0.7 units (docs: play.md): 41.8 from its
    // box's middle, 1.53 times its ring's half side, along (-0.38, 0.857,
    // -0.348) with it facing +z; a focal length of 80 pixels for the 80
    // high view.
    constexpr float kOut = 1.53f;
    constexpr gfx::Vec3 kFrom{-0.38f, 0.857f, -0.348f};
    const float s = std::sin(unit.heading);
    const float c = std::cos(unit.heading);
    const gfx::Vec3 from{kFrom[0] * c + kFrom[2] * s, kFrom[1], -kFrom[0] * s + kFrom[2] * c};
    const float length = std::sqrt(from[0] * from[0] + from[1] * from[1] + from[2] * from[2]);
    const gfx::Vec3 target = unit.middle();
    const float distance = kOut * ringHalfOf(unit);
    MapScene::Camera cam;
    for (size_t k = 0; k < 3; ++k) {
        cam.eye[k] = target[k] + from[k] / length * distance;
        cam.forward[k] = -from[k] / length;
    }
    // Up: square to forward, in the plane of forward and the world's up.
    const float fy = cam.forward[1];
    cam.up = {-fy * cam.forward[0], 1 - fy * fy, -fy * cam.forward[2]};
    const float upLength = std::sqrt(cam.up[0] * cam.up[0] + cam.up[1] * cam.up[1] + cam.up[2] * cam.up[2]);
    for (float &v : cam.up)
        v /= upLength;
    cam.fovY = 2 * std::atan(40.0f / 80.0f);
    return cam;
}

int MissionView::grownCrew(int crew, double &carry, int starbases, double seconds) {
    // 3.5 a second with one starbase: the original's counter went from
    // 3502 to 3607 in 30 seconds, steadily; per starbase is a guess.
    constexpr double kPerSecond = 3.5;
    constexpr int kMaxCrew = 99999; // RTS_CFG.h's cfgMaxCrew
    carry += kPerSecond * starbases * seconds;
    const double whole = std::floor(carry);
    carry -= whole;
    return int(std::min<double>(kMaxCrew, crew + whole));
}

Hud::Minimap MissionView::minimapOf(const BznMap &map, const std::vector<MapScene::Unit> &units,
                                    const std::vector<bool> &moves, const MapScene::Camera &camera, float plane) {
    Hud::Minimap m;
    m.left = map.area[0];
    m.right = map.area[1];
    m.near = map.area[4];
    m.far = map.area[5];
    for (size_t i = 0; i < units.size(); ++i) {
        // In its player's colour: the original's, white (teamcolor.odf's 0.9).
        const gfx::Vec3 c = units[i].colour.value_or(gfx::Vec3{0.9f, 0.9f, 0.9f});
        Hud::Minimap::Mark mark;
        mark.x = units[i].at[0];
        mark.z = units[i].at[2];
        for (size_t k = 0; k < 3; ++k)
            mark.colour[k] = uint8_t(std::lround(std::clamp(c[k], 0.0f, 1.0f) * 255));
        mark.station = i >= moves.size() || !moves[i];
        m.marks.push_back(mark);
    }
    // The screen's corners on the plane the camera looks at: the top edge
    // as the original's, the bottom a pixel or two lower.
    for (const auto &[x, y] : {std::pair{0, 0}, {640, 0}, {640, 480}, {0, 480}})
        if (auto p = MapScene::pointOn(camera, plane, x, y))
            m.view.push_back({(*p)[0], (*p)[2]});
    if (m.view.size() != 4)
        m.view.clear();
    return m;
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
    _starbases = 0;
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
    _slot = slot;
    std::map<std::string, std::optional<Movement::Physics>> physics;
    const Odf::Loader loader = loaderOf(_files.files());
    for (const MapScene::Unit &u : _scene.units()) {
        auto it = physics.find(u.odf);
        if (it == physics.end()) {
            Odf odf;
            std::optional<Movement::Physics> p;
            if (!odf.load(u.odf + ".odf", loader) && odf.number("enginesHitPoints") > 0)
                p = Movement::physicsOf(odf, loader);
            it = physics.emplace(u.odf, p).first;
        }
        _moves.push_back(it->second.has_value());
        _physics.push_back(it->second);
        Movement::Ship ship;
        ship.at = u.at;
        ship.heading = u.heading;
        _ships.push_back(ship);
    }
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
    _starbases = 0;
    _crewCarry = 0;
    for (const std::string &u : myUnits) {
        Odf odf;
        if (!odf.load(u + ".odf", loader) && text::equalsIgnoreCase(odf.string("classLabel"), "starbase"))
            ++_starbases;
    }
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
    _selection.clear();
    _displayed.clear();
    _moves.clear();
    _physics.clear();
    _ships.clear();
    _slot = -1;
    _dragging = false;
    _hover = -1;
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
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kRight && !_hud.covers(ev.mouseX, ev.mouseY)) {
        // Sent to the point under the pointer, on the plane the selected
        // units fly in.
        if (!_selection.empty() && size_t(_selection.front()) < _ships.size())
            if (auto point =
                    MapScene::pointOn(camera(), _ships[size_t(_selection.front())].at[1], ev.mouseX, ev.mouseY))
                sendTo(_ships, _scene.units(), _selection, _moves, *point);
        return;
    }
    if (_pressX >= 0 && (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseUp)) {
        // Past a click's slop it's a box, and what it holds is selected as
        // it's dragged, as the original's frames show.
        if (std::abs(ev.mouseX - _pressX) + std::abs(ev.mouseY - _pressY) > kClickSlop)
            _dragging = true;
        const gfx::Mat4 viewProj = MapScene::viewProjOf(camera(), kRange);
        if (_dragging)
            _selection = boxed(_scene.units(), _moves, viewProj, _slot, _pressX, _pressY, ev.mouseX, ev.mouseY);
        if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
            if (!_dragging) {
                // A click: the unit under it, or none.
                const int unit = unitAt(_scene.units(), viewProj, camera(), ev.mouseX, ev.mouseY);
                _selection = unit < 0 ? std::vector<int>{} : std::vector<int>{unit};
            }
            _clicked = !_dragging;
            _pressX = _pressY = -1;
            _dragging = false;
        }
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
    const double seconds = double(millis - _last) / 1000;
    _overview = moved(_overview, _held, seconds);
    _last = millis;
    if (_resources)
        _resources->crew = grownCrew(_resources->crew, _crewCarry, _starbases, seconds);
    // The ships fly on, in steps of no more than a twentieth of a second.
    const int steps = std::max(1, int(std::ceil(seconds / 0.05)));
    for (size_t i = 0; i < _ships.size(); ++i) {
        if (!_physics[i] || (!_ships[i].goal && _ships[i].speed == 0))
            continue;
        for (int s = 0; s < steps; ++s)
            Movement::step(_ships[i], *_physics[i], seconds / steps);
        if (!_ships[i].goal && std::abs(_ships[i].speed) < 0.01f)
            _ships[i].speed = 0;
        _scene.moveUnit(i, _ships[i].at, _ships[i].heading);
    }
    const MapScene::Camera view = camera();
    _scene.draw(view, double(millis) / 1000, kRange, kClear, true);
    const gfx::Mat4 viewProj = MapScene::viewProjOf(view, kRange);
    const std::vector<MapScene::Unit> &units = _scene.units();
    _hover = _hud.covers(_mouseX, _mouseY) ? -1 : unitAt(units, viewProj, view, _mouseX, _mouseY);
    std::vector<const MapScene::Unit *> chosen;
    for (int i : _selection)
        if (i >= 0 && size_t(i) < units.size())
            chosen.push_back(&units[size_t(i)]);
    makeRing();
    for (const MapScene::Unit *unit : chosen) {
        if (!_ringMesh || !_ringTexture)
            break;
        // Added on in (229, 229, 229), as the draw is.
        const gfx::Vec3 middle = ringMiddleOf(*unit);
        const float half = ringHalfOf(*unit);
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
    // The cinematic view: the unit selected first, or the player's first
    // that moves, close up.
    int shown = _selection.empty() ? -1 : _selection.front();
    for (size_t i = 0; shown < 0 && i < units.size(); ++i)
        if (i < _moves.size() && _moves[i] && units[i].slot == _slot)
            shown = int(i);
    _hud.setCinematicShown(shown >= 0);
    if (shown >= 0) {
        const std::array<int, 4> area = _hud.cinematicArea();
        _scene.drawInset(closeUpOf(units[size_t(shown)]), double(millis) / 1000, kRange, area[0], area[1], area[2],
                         area[3]);
    }
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    // A unit clicked on has its bar; the original drew none over units a
    // box selected.
    if (_clicked && chosen.size() == 1) {
        const std::array<int, 3> bar = healthBarOf(*chosen.front(), viewProj);
        if (bar[2] > 0)
            _hud.drawHealthBar(_overlay, bar[0], bar[1], bar[2], 1);
    }
    if (_dragging)
        Hud::drawBox(_overlay, _pressX, _pressY, _mouseX, _mouseY);
    _hud.draw(_overlay, _resources.value_or(Hud::Resources{}), _scene.seen(), _resources.has_value(),
              minimapOf(_scene.map(), units, _moves, view, _overview.target[1]));
    if (_displayed != _selection) {
        _display.clear();
        // The game picks a name at random; any one of them will do.
        for (int i : _selection)
            _display.push_back(displayOf(units[size_t(i)].odf, loaderOf(_files.files()), _hud, size_t(i)));
        _orders = ordersOf(_display);
        _displayed = _selection;
    }
    _hud.drawSelection(_overlay, _display, _orders);
    // Over a unit, the select cursor; over the map with ships selected,
    // the move cursor (the original's, with a construction ship).
    bool sending = false;
    for (int i : _selection)
        sending = sending || (size_t(i) < _moves.size() && _moves[size_t(i)]);
    const char *cursor = _hover >= 0 ? "c_select" : sending && !_hud.covers(_mouseX, _mouseY) ? "c_move" : "c_arrow";
    _hud.drawCursor(_overlay, _mouseX, _mouseY, double(millis) / 1000, cursor);
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::storm3d
