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

void MissionView::stop() {
    _scene.attach(nullptr);
    _renderer = nullptr;
}

void MissionView::handleEvent(const Event &ev) {
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
    const MapScene::Camera camera = MapScene::overviewOf(_overview.target, _overview.height, _overview.turn);
    _scene.draw(camera, double(millis) / 1000, 1000, kClear, true);
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    _hud.draw(_overlay, _resources.value_or(Hud::Resources{}), _scene.seen(), _resources.has_value());
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::storm3d
