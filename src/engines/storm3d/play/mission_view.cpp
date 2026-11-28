// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"
#include "engines/storm3d/formats/odf.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

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
    // Repair and Recrew, for a ship: lit as it draws, while it needs it.
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

std::vector<Hud::Unit::Button> MissionView::orderPanelOf(bool ship) {
    std::vector<Hud::Unit::Button> panel = {{1, "b_galert", "bb_standard", true},
                                            {2, "b_yalert", "bb_standard", true},
                                            {3, "b_ralert", "bb_standard", true}};
    if (ship) {
        panel.push_back({4, "b_patrol", "bb_standard", true});
        panel.push_back({5, "b_scout", "bb_standard", true});
        panel.push_back({6, "b_sandd", "bb_standard", true});
        panel.push_back({7, "b_guard", "bb_standard", true});
    }
    panel.push_back({10, "b_decom", "bb_standard", true});
    panel.push_back({12, "b_back", "bb_standard", true});
    return panel;
}

void MissionView::sendTo(std::vector<Movement::Ship> &ships, const std::vector<MapScene::Unit> &units,
                         const std::vector<int> &selection, const std::vector<bool> &moves, const gfx::Vec3 &point,
                         const Movement::Terrain &terrain) {
    // Ships keep the rings' half sides apart, as the original's way round
    // the starbase did.
    std::vector<Movement::Obstacle> standing;
    for (size_t i = 0; i < units.size(); ++i)
        if (i >= moves.size() || !moves[i])
            standing.push_back({ringMiddleOf(units[i]), ringHalfOf(units[i])});
    std::vector<size_t> going;
    for (int i : selection)
        if (size_t(i) < ships.size() && size_t(i) < moves.size() && moves[size_t(i)])
            going.push_back(size_t(i));
    if (going.empty())
        return;
    // The first goes to the point; each other keeps its way from it, at
    // least FORMATION_SPACING (100) off: the original's two construction
    // ships, 53 apart, came to the point and 100 beside it, on the same
    // side *(inferred: one flight)*.
    constexpr float kSpacing = 100;
    const gfx::Vec3 lead = ships[going.front()].at;
    for (size_t n = 0; n < going.size(); ++n) {
        const size_t i = going[n];
        float ox = ships[i].at[0] - lead[0];
        float oz = ships[i].at[2] - lead[2];
        const float apart = std::hypot(ox, oz);
        if (n > 0 && apart < kSpacing) {
            // Right on top of the first, set to its side.
            if (apart < 1e-3f)
                ox = 1, oz = 0;
            const float scale = kSpacing / std::max(1e-3f, std::hypot(ox, oz));
            ox *= scale;
            oz *= scale;
        }
        const gfx::Vec3 spot{point[0] + ox, ships[i].at[1], point[2] + oz};
        ships[i].waiting = Movement::kSetOff;
        // The planner's way over the terrain, to the spot or as near as it
        // can get; each leg of it round what stands in it.
        const std::vector<gfx::Vec3> way = Movement::plan(terrain, ships[i].at, spot);
        ships[i].goal = way.empty() ? spot : way.back();
        ships[i].route.clear();
        gfx::Vec3 leg = ships[i].at;
        for (size_t k = 0; k < way.size(); ++k) {
            if (i < units.size())
                for (const gfx::Vec3 &p : Movement::routeAround(leg, way[k], standing, ringHalfOf(units[i])))
                    ships[i].route.push_back(p);
            if (k + 1 < way.size())
                ships[i].route.push_back(way[k]);
            leg = way[k];
        }
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

std::vector<std::string> MissionView::buildListOf(const std::string &odf, const Odf::Loader &loader) {
    std::vector<std::string> items;
    Odf unit;
    if (unit.load(odf + ".odf", loader))
        return items;
    for (int i = 0;; ++i) {
        const std::string item = unit.string("buildItem" + std::to_string(i));
        if (item.empty())
            break;
        Odf what;
        if (!what.load(item + ".odf", loader) && !text::equalsIgnoreCase(what.string("classLabel"), "upgrade"))
            items.push_back(item);
    }
    return items;
}

Hud::Unit MissionView::buildMenuOf(const std::vector<std::string> &items, const Odf::Loader &loader,
                                   const std::vector<std::string> &owned, const TechTree &tree) {
    if (!tree.empty()) {
        // As the map's tech tree has it: the original's dark items, the
        // starbase's freighter and the construction ship's last four, are
        // those whose needs the starting units don't meet.
        std::set<std::string> have;
        for (const std::string &o : owned)
            have.insert(text::toLower(o));
        Hud::Unit menu;
        for (size_t i = 0; i < items.size() && i < 11; ++i)
            menu.controls.push_back(
                {int(i) + 1, "b_" + text::toLower(items[i]), "bb_build", tree.allows(items[i], have)});
        menu.controls.push_back({12, "b_back", "bb_standard", true});
        return menu;
    }
    // What an item needs the player to have, by class. A freighter has
    // nothing to carry without a mining station: the original's was dark
    // with none, and the computer player builds one first (inferred). The
    // construction ship's last four were dark with the starting units;
    // what each needs is a guess.
    static const std::map<std::string, std::string> kNeeds = {
        {"fresear2", "research"}, {"fyard2", "shipyard"}, {"fturret2", "research"}, {"fsuperbl", "research"}};
    std::set<std::string> classes;
    for (const std::string &o : owned) {
        Odf unit;
        if (!unit.load(o + ".odf", loader))
            classes.insert(text::toLower(unit.string("classLabel")));
    }
    const bool mining = classes.count("mining") > 0;
    Hud::Unit menu;
    for (size_t i = 0; i < items.size() && i < 11; ++i) {
        Odf item;
        const bool freighter =
            !item.load(items[i] + ".odf", loader) && text::equalsIgnoreCase(item.string("classLabel"), "freighter");
        auto need = kNeeds.find(text::toLower(items[i]));
        const bool needs = need != kNeeds.end() && !classes.count(need->second);
        menu.controls.push_back(
            {int(i) + 1, "b_" + text::toLower(items[i]), "bb_build", (!freighter || mining) && !needs});
    }
    menu.controls.push_back({12, "b_back", "bb_standard", true});
    return menu;
}

MissionView::Order MissionView::orderOf(const std::string &item, const Odf::Loader &loader) {
    Order o;
    o.item = item;
    Odf odf;
    if (odf.load(item + ".odf", loader))
        return o;
    o.time = odf.number("buildTime");
    o.dilithium = int(odf.number("dilithiumCost"));
    o.crew = int(odf.number("crewCost"));
    o.officers = int(odf.number("officerCost"));
    return o;
}

bool MissionView::pay(Hud::Resources &resources, const Order &order) {
    if (resources.dilithium < order.dilithium || resources.crew < order.crew ||
        resources.officers + order.officers > resources.maxOfficers)
        return false;
    resources.dilithium -= order.dilithium;
    resources.crew -= order.crew;
    resources.officers += order.officers;
    return true;
}

gfx::Vec3 MissionView::launchPointOf(const gfx::Vec3 &builder) {
    // The original's construction ship, built at the starbase, flew out to
    // 131 to the -x and 110 to the -z of it, 15 above, and stopped there
    // (one build).
    return {builder[0] - 131, builder[1] + 15, builder[2] - 110};
}

gfx::Vec3 MissionView::standOffOf(const gfx::Vec3 &site, const gfx::Vec3 &from, float distance) {
    const float dx = from[0] - site[0];
    const float dz = from[2] - site[2];
    const float length = std::hypot(dx, dz);
    if (length < 1e-3f)
        return {site[0], from[1], site[2] + distance};
    return {site[0] + dx / length * distance, from[1], site[2] + dz / length * distance};
}

std::set<std::pair<int, int>> MissionView::takenCellsOf(const std::vector<Footprint> &footprints) {
    constexpr float kCell = 50; // STATION_PLACEMENT_GRID_SIZE
    // The starbase's box, 37.7 and 36.6 each way, and its 80 give the
    // original's yellow block, cells 11 to 15 by 7 to 12, to the cell.
    std::set<std::pair<int, int>> taken;
    for (const Footprint &f : footprints) {
        const int x0 = int(std::floor((f.x - f.halfX - f.buffer) / kCell));
        const int x1 = int(std::floor((f.x + f.halfX + f.buffer) / kCell));
        const int z0 = int(std::floor((f.z - f.halfZ - f.buffer) / kCell));
        const int z1 = int(std::floor((f.z + f.halfZ + f.buffer) / kCell));
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                taken.insert({x, z});
    }
    return taken;
}

std::vector<std::pair<int, int>> MissionView::siteCellsOf(const gfx::Vec3 &site, const Footprint &station) {
    constexpr float kCell = 50;
    const float across = 2 * (std::max(station.halfX, station.halfZ) + station.buffer) / kCell;
    const int reach = std::max(0, (int(std::lround(across)) - 1) / 2);
    const int cx = int(std::floor(site[0] / kCell));
    const int cz = int(std::floor(site[2] / kCell));
    std::vector<std::pair<int, int>> cells;
    for (int x = cx - reach; x <= cx + reach; ++x)
        for (int z = cz - reach; z <= cz + reach; ++z)
            cells.emplace_back(x, z);
    return cells;
}

std::optional<MissionView::Footprint> MissionView::footprintOf(const std::string &odf, const gfx::Vec3 &at,
                                                               float fallback) {
    const auto box = _scene.boxOf(odf);
    if (!box)
        return std::nullopt;
    Odf def;
    const float buffer =
        def.load(odf + ".odf", loaderOf(_files.files())) ? fallback : float(def.number("footprintBuffer", fallback));
    Footprint f;
    f.x = at[0] + (box->first[0] + box->second[0]) / 2;
    f.z = at[2] + (box->first[2] + box->second[2]) / 2;
    f.halfX = (box->second[0] - box->first[0]) / 2;
    f.halfZ = (box->second[2] - box->first[2]) / 2;
    f.buffer = buffer;
    return f;
}

bool MissionView::placeable(const gfx::Vec3 &site, const Footprint &station) const {
    const auto taken = takenCellsOf(_footprints);
    for (const auto &c : siteCellsOf(site, station))
        if (taken.count(c))
            return false;
    return true;
}

float MissionView::standOffDistanceOf(const Footprint &station) {
    return std::hypot(station.halfX, station.halfZ) + 37;
}

void MissionView::work(Freighter &freighter, Movement::Ship &ship, const gfx::Vec3 &moon, const gfx::Vec3 &station,
                       int &dilithium, double seconds) {
    // gminebm's range, minebm's damage and the shot delay; freight.odf's
    // maxScrap; fmining.odf's dilithiumTransferRate.
    constexpr float kReach = 100;
    constexpr float kShot = 15;
    constexpr double kShotDelay = 1.5;
    constexpr float kHold = 150;
    constexpr float kTransfer = 15;
    constexpr int kMaxDilithium = 999999; // RTS_CFG.h's cfgMaxDilithium
    using State = Freighter::State;
    switch (freighter.state) {
    case State::kToMoon:
        if (!ship.goal) {
            // The original's passed the moon's side and mined out of view
            // beyond it: at its side, square to the way from the station,
            // which gives its rounds' 44 seconds (inferred).
            const float dx = moon[0] - station[0];
            const float dz = moon[2] - station[2];
            const float length = std::max(1e-3f, std::hypot(dx, dz));
            const gfx::Vec3 spot{moon[0] - dz / length * kReach, ship.at[1], moon[2] + dx / length * kReach};
            if (std::hypot(spot[0] - ship.at[0], spot[2] - ship.at[2]) > 20) {
                ship.goal = spot;
                // Round the moon, not through it.
                ship.route = Movement::routeAround(ship.at, spot, {{moon, kReach / 2}}, 0);
            } else {
                // Its first shot as it gets there.
                freighter.state = State::kMining;
                freighter.timer = 0;
                freighter.cargo = std::min(kHold, freighter.cargo + kShot);
            }
        }
        break;
    case State::kMining: {
        const int before = int(freighter.timer / kShotDelay);
        freighter.timer += seconds;
        const int shots = int(freighter.timer / kShotDelay) - before;
        freighter.cargo = std::min(kHold, freighter.cargo + kShot * float(shots));
        if (freighter.cargo >= kHold) {
            freighter.state = State::kToStation;
            ship.goal = gfx::Vec3{station[0], ship.at[1], station[2]};
        }
        break;
    }
    case State::kToStation:
        if (!ship.goal)
            freighter.state = State::kUnloading;
        break;
    case State::kUnloading: {
        const float out = std::min(freighter.cargo, float(kTransfer * seconds));
        freighter.cargo -= out;
        freighter.carry += out;
        const double whole = std::floor(freighter.carry);
        freighter.carry -= whole;
        dilithium = int(std::min<double>(kMaxDilithium, dilithium + whole));
        if (freighter.cargo <= 0) {
            // What's left of the sum's fractions: the load comes to its 150.
            dilithium = int(std::min<double>(kMaxDilithium, dilithium + std::lround(freighter.carry)));
            freighter.carry = 0;
            freighter.cargo = 0;
            freighter.state = State::kToMoon;
        }
        break;
    }
    }
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

void MissionView::pressControl(int slot) {
    if (slot == 0 || _selection.size() != 1)
        return;
    if (_ordering) {
        // The alert levels, decommission, the panel left open as the
        // original's was, or back; a ship's standing orders aren't made yet.
        if (slot >= 1 && slot <= 3)
            setAlert(size_t(_selection.front()), slot == 1 ? kGreenAlert : slot == 2 ? kYellowAlert : kRedAlert);
        if (slot == 10)
            decommission(size_t(_selection.front()));
        if (slot == 12)
            _ordering = false;
        return;
    }
    if (!_building) {
        for (const Hud::Unit::Button &b : _orders.controls)
            if (b.slot == slot && b.sprite == "b_order" && b.enabled) {
                _ordering = true;
                return;
            }
        // Repair and Recrew: to the nearest shipyard or starbase.
        const size_t one = size_t(_selection.front());
        for (const Hud::Unit::Button &b : _orders.controls)
            if (b.slot == slot && b.sprite == "b_repair" && one < _vitals.size() &&
                (_vitals[one].shields < _vitals[one].maxShields || _vitals[one].crew < _vitals[one].maxCrew ||
                 _vitals[one].damaged()))
                if (auto station = repairStationFor(one)) {
                    _repairingAt[one] = *station;
                    _ships[one].goal = _scene.units()[*station].at;
                    _targets[one] = -1;
                }
        // Recrew: a station's crew from the pool.
        for (const Hud::Unit::Button &b : _orders.controls)
            if (b.slot == slot && b.sprite == "b_recrew" && b.enabled)
                _recrewing.insert(one);
        // The attack button: the next click on a unit is the target.
        for (const Hud::Unit::Button &b : _orders.controls)
            if (b.slot == slot && b.sprite == "b_attack" && b.enabled)
                _targeting = true;
        // The build button opens the unit's menu.
        for (const Hud::Unit::Button &b : _orders.controls)
            if (b.slot == slot && b.sprite == "b_build" && b.enabled) {
                _buildItems = buildListOf(_scene.units()[size_t(_selection.front())].odf, loaderOf(_files.files()));
                _buildMenu = buildMenuOf(_buildItems, loaderOf(_files.files()), ownedOdfs(), _techTree);
                // A pod researched goes dark, as the original's Antimatter
                // Mines did once built.
                for (Hud::Unit::Button &b : _buildMenu.controls)
                    if (b.slot >= 1 && size_t(b.slot) <= _buildItems.size()) {
                        Odf item;
                        if (!item.load(_buildItems[size_t(b.slot) - 1] + ".odf", loaderOf(_files.files())) &&
                            text::equalsIgnoreCase(item.string("classLabel"), "pod") &&
                            _researched[_slot].count(Combat::researchKeyOf(item.string("tooltip"))))
                            b.enabled = false;
                        // Without a tech tree, one for a ship whose yard
                        // can't be built yet *(inferred)*: the original's
                        // Corbomite Reflector, for the Sovereign of the
                        // Advanced Starship Yard, was dark with no shipyard
                        // (tech1.tt has it need that yard).
                        if (b.enabled && _techTree.empty() &&
                            text::equalsIgnoreCase(item.string("classLabel"), "pod") &&
                            !yardReady(item.string("tooltip")))
                            b.enabled = false;
                    }
                _building = true;
            }
        return;
    }
    const size_t builder = size_t(_selection.front());
    const auto site =
        std::find_if(_sites.begin(), _sites.end(), [builder](const Site &x) { return x.builder == builder; });
    if (slot == 12) {
        _building = false;
    } else if (site != _sites.end()) {
        // Busy building a station: cancel gives it up, paid back if it was
        // paid for *(guessed)*.
        if (slot == 11) {
            if (site->started && _resources) {
                _tallies[_slot].spent -= site->order.dilithium;
                _resources->dilithium += site->order.dilithium;
                _resources->crew += site->order.crew;
                _resources->officers -= site->order.officers;
            }
            _sites.erase(site);
        }
    } else if (slot == 11 && !_queues[builder].empty()) {
        // Cancelled, the last ordered goes, paid back *(guessed)*.
        const Order o = _queues[builder].back();
        _queues[builder].pop_back();
        if (_resources) {
            _tallies[_slot].spent -= o.dilithium;
            _resources->dilithium += o.dilithium;
            _resources->crew += o.crew;
            _resources->officers -= o.officers;
        }
    } else if (slot >= 1 && size_t(slot) <= _buildItems.size()) {
        for (const Hud::Unit::Button &b : _buildMenu.controls)
            if (b.slot == slot && b.enabled && builder < _moves.size() && _moves[builder]) {
                // A ship builds a station where it's placed, and pays as it
                // starts.
                _placing = _buildItems[size_t(slot) - 1];
                _placingFootprint = footprintOf(*_placing, {}, 0).value_or(Footprint{});
            } else if (b.slot == slot && b.enabled && _resources) {
                const Order o = orderOf(_buildItems[size_t(slot) - 1], loaderOf(_files.files()));
                if (pay(*_resources, o)) {
                    _tallies[_slot].spent += o.dilithium;
                    _queues[builder].push_back(o);
                }
            }
    }
}

void MissionView::place(int x, int y) {
    if (!_placing || _selection.size() != 1)
        return;
    const size_t builder = size_t(_selection.front());
    if (builder >= _ships.size())
        return;
    const auto at = MapScene::pointOn(camera(), _ships[builder].at[1], x, y);
    if (!at)
        return;
    if (!placeable(*at, _placingFootprint))
        return;
    startSite(builder, *at, *_placing, _placingFootprint);
    _placing.reset();
}

Hud::Resources *MissionView::bankOf(int slot) {
    if (slot == _slot)
        return _resources ? &*_resources : nullptr;
    for (Computer &c : _computers)
        if (c.slot == slot)
            return &c.bank;
    return nullptr;
}

void MissionView::startSite(size_t builder, const gfx::Vec3 &at, const std::string &item, const Footprint &station) {
    Site site;
    site.builder = builder;
    site.at = at;
    site.order = orderOf(item, loaderOf(_files.files()));
    _sites.push_back(site);
    _ships[builder].goal = standOffOf(at, _ships[builder].at, standOffDistanceOf(station));
    _ships[builder].route.clear();
    _ships[builder].waiting = Movement::kSetOff;
}

std::optional<gfx::Vec3> MissionView::siteFor(Computer &c, const ComputerPlayer::Element &e, const Footprint &station) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    if (c.bases.empty()) {
        // Its first base: its starbase's spot, else its first unit's (0x4180f0).
        std::optional<gfx::Vec3> home;
        for (size_t i = 0; i < units.size() && !home; ++i)
            if (units[i].slot == c.slot && !_dead[i] && _classes[i] == "starbase")
                home = units[i].at;
        for (size_t i = 0; i < units.size() && !home; ++i)
            if (units[i].slot == c.slot && !_dead[i])
                home = units[i].at;
        if (!home)
            return std::nullopt;
        c.bases.push_back(*home);
    }
    Odf def;
    const bool mining = !def.load(e.type + ".odf", loaderOf(_files.files())) &&
                        text::equalsIgnoreCase(def.string("classLabel"), "mining");
    size_t base = c.lastBase;
    if (e.base >= 0 && size_t(e.base) < c.bases.size()) {
        base = size_t(e.base);
    } else if (mining) {
        // A base past those it has, for a mining station: the moon nearest its
        // first base that isn't one of its bases yet (0x418580).
        std::optional<size_t> best;
        float bestDistance = 0;
        for (size_t m = 0; m < _moons.size(); ++m) {
            if (std::find(c.baseMoons.begin(), c.baseMoons.end(), m) != c.baseMoons.end())
                continue;
            const float d = std::hypot(_moons[m][0] - c.bases[0][0], _moons[m][2] - c.bases[0][2]);
            if (!best || d < bestDistance)
                best = m, bestDistance = d;
        }
        if (best) {
            c.bases.push_back({_moons[*best][0], c.bases[0][1], _moons[*best][2]});
            c.baseMoons.push_back(*best);
            base = c.bases.size() - 1;
        }
    }
    // Round its base, then each of the others in turn, till a spot fits;
    // within 100 of the map's edges, and not one taken in its last 20 looks.
    const BznMap &map = _scene.map();
    std::optional<gfx::Vec3> found;
    for (size_t k = 0; k < c.bases.size() && !found; ++k) {
        const size_t b = (base + k) % c.bases.size();
        const gfx::Vec3 middle = c.bases[b];
        int tried = -1;
        int taken = 0;
        const auto spot = ComputerPlayer::siteNear([&](float dx, float dz) {
            ++tried;
            if (auto off = c.offLimits.find({b, tried}); off != c.offLimits.end() && off->second > 0)
                return false;
            const float x = middle[0] + dx;
            const float z = middle[2] + dz;
            if (x <= map.area[0] + 100 || x >= map.area[1] - 100 || z <= map.area[4] + 100 || z >= map.area[5] - 100)
                return false;
            taken = tried;
            return placeable({x, middle[1], z}, station);
        });
        if (spot) {
            c.lastBase = b;
            found = gfx::Vec3{middle[0] + spot->first, middle[1], middle[2] + spot->second};
            c.offLimits[{b, taken}] = 21; // less the one below: 20 more looks
        }
    }
    for (auto &[spot, looks] : c.offLimits)
        looks = std::max(looks - 1, 0);
    return found;
}

void MissionView::plan(Computer &c) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const BznMap &map = _scene.map();
    const int across = std::max(1, int(std::ceil((map.area[1] - map.area[0]) / Strategy::kCell)));
    const int down = std::max(1, int(std::ceil((map.area[5] - map.area[4]) / Strategy::kCell)));
    if (c.explored.size() != size_t(across * down))
        c.explored.assign(size_t(across * down), false);
    auto cellOf = [&](const gfx::Vec3 &at) {
        const int x = std::clamp(int((at[0] - map.area[0]) / Strategy::kCell), 0, across - 1);
        const int z = std::clamp(int((at[2] - map.area[4]) / Strategy::kCell), 0, down - 1);
        return size_t(z * across + x);
    };
    // What it's seen: the cells whose middles its units' scan has reached.
    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].slot != c.slot || _dead[i])
            continue;
        for (int z = 0; z < down; ++z)
            for (int x = 0; x < across; ++x) {
                const float cx = map.area[0] + (float(x) + 0.5f) * Strategy::kCell;
                const float cz = map.area[4] + (float(z) + 0.5f) * Strategy::kCell;
                if (std::hypot(cx - units[i].at[0], cz - units[i].at[2]) <= _scans[i])
                    c.explored[size_t(z * across + x)] = true;
            }
    }
    std::vector<Strategy::Cell> cells(size_t(across * down));
    for (size_t k = 0; k < cells.size(); ++k)
        cells[k].explored = c.explored[k];
    std::vector<Strategy::Ship> ships;
    std::vector<size_t> shipUnits;
    for (size_t i = 0; i < units.size(); ++i) {
        if (_dead[i] || _classes[i] == "wormhole_obj")
            continue;
        Strategy::Cell &cell = cells[cellOf(units[i].at)];
        const bool station = !_moves[i];
        if (units[i].slot == c.slot) {
            cell.ourStations += station;
            // Its ships that fight: armed, and not a builder or freighter;
            // nor one a mission's script has taken over (SetSpecialForcesFlag)
            // *(guessed)*.
            if (!station && !_weapons[i].empty() && _classes[i] != "constructionrig" && _classes[i] != "freighter" &&
                !_scripted.count(i)) {
                ships.push_back({units[i].at[0] - map.area[0], units[i].at[2] - map.area[4], _vitals[i].maxShields});
                shipUnits.push_back(i);
            }
        } else if (hostile(c.slot, units[i].slot)) {
            cell.enemyStations += station;
            cell.enemyShips += !station;
            cell.enemyShields += _vitals[i].shields;
        }
    }
    const std::vector<Strategy::Goal> goals = Strategy::goalsOf(cells, across, down, c.personality);
    const auto to = Strategy::assign(ships, goals, c.personality);
    for (size_t s = 0; s < ships.size(); ++s) {
        if (!to[s])
            continue;
        const Strategy::Goal &g = goals[*to[s]];
        Movement::Ship &ship = _ships[shipUnits[s]];
        const gfx::Vec3 there{map.area[0] + (float(g.x) + 0.5f) * Strategy::kCell, ship.at[1],
                              map.area[4] + (float(g.z) + 0.5f) * Strategy::kCell};
        // On its way there already, or there: left as it is.
        if (std::hypot(there[0] - ship.at[0], there[2] - ship.at[2]) < Strategy::kCell / 2 ||
            (ship.goal && std::hypot(there[0] - (*ship.goal)[0], there[2] - (*ship.goal)[2]) < 1))
            continue;
        ship.goal = there;
        ship.route.clear();
        ship.waiting = Movement::kSetOff;
    }
}

void MissionView::engage() {
    // An armed unit with nothing to attack takes the nearest enemy in its
    // weapons' reach *(guessed: the original's units fight back)*. A
    // computer player's passes over one with three ships or more within
    // 125 of it, itself among them if it's one, as the original's craft
    // choosing their next target (0x433360).
    constexpr float kCrowd = 125;
    constexpr int kCrowded = 3;
    const std::vector<MapScene::Unit> &units = _scene.units();
    auto crowded = [&](size_t target, size_t chooser) {
        int near = 0;
        for (size_t k = 0; k < units.size(); ++k)
            if (k != chooser && !_dead[k] && k < _moves.size() && _moves[k] &&
                std::hypot(units[k].at[0] - units[target].at[0], units[k].at[2] - units[target].at[2]) < kCrowd)
                ++near;
        return near >= kCrowded;
    };
    for (size_t i = 0; i < units.size() && i < _targets.size(); ++i) {
        // Green picks none; yellow only one firing at it (0x42e540).
        const int alert = alertOf(i);
        if (_dead[i] || _targets[i] >= 0 || _weapons[i].empty() || alert == kGreenAlert)
            continue;
        const bool computer = std::any_of(_computers.begin(), _computers.end(),
                                          [&](const Computer &c) { return c.slot == units[i].slot; });
        float reach = 0;
        for (const Combat::Weapon &w : _weapons[i])
            reach = std::max(reach, w.range);
        const gfx::Vec3 from = units[i].middle();
        int best = -1;
        float bestDistance = reach;
        for (size_t j = 0; j < units.size(); ++j) {
            // A cloaked unit not seen by a detector isn't taken.
            // Nor a wormhole: it isn't fought *(inferred: the program's
            // wormhole_obj isn't a craft or a building)*.
            if (_dead[j] || !hostile(units[i].slot, units[j].slot) || (_cloaked[j] && !_revealedUntil.count(j)) ||
                _classes[j] == "wormhole_obj" || (alert == kYellowAlert && _targets[j] != int(i)))
                continue;
            const gfx::Vec3 to = units[j].middle();
            const float d = std::hypot(to[0] - from[0], to[1] - from[1], to[2] - from[2]);
            if (d <= bestDistance && !(computer && crowded(j, i))) {
                best = int(j);
                bestDistance = d;
            }
        }
        _targets[i] = best;
        if (best >= 0)
            _picked[i] = best;
    }
}

ConfigFile::Loader MissionView::aiLoader() const {
    return [this](std::string_view name, std::string &text) {
        std::vector<uint8_t> bytes;
        if (Error err = _files.read("AI/AIPs/" + std::string(name), bytes))
            return err;
        text.assign(bytes.begin(), bytes.end());
        return Error::success();
    };
}

void MissionView::takeStance(Computer &c, ComputerPlayer::Stance stance) {
    c.stance = stance;
    c.personality = Strategy::personalityOf(ComputerPlayer::personalityFileOf(stance), aiLoader());
    c.list = ComputerPlayer::buildListOf(ComputerPlayer::buildListFileOf(c.race, stance), aiLoader());
}

void MissionView::weighStances() {
    // A player's strength is its living units' maxHealth added up, which
    // damage doesn't lessen; the average is of the players in the game
    // that have any (0x430600, 0x4307f0).
    const std::vector<MapScene::Unit> &units = _scene.units();
    std::map<int, float> strength;
    for (size_t i = 0; i < units.size() && i < _vitals.size(); ++i)
        if (!_dead[i])
            strength[units[i].slot] += _vitals[i].maxShields;
    float sum = 0;
    int counted = 0;
    for (const Player &p : _players)
        if (strength[p.slot] > 0)
            sum += strength[p.slot], ++counted;
    if (counted == 0)
        return;
    const float average = sum / float(counted);
    for (Computer &c : _computers) {
        if (!c.stances)
            continue;
        const ComputerPlayer::Stance stance = ComputerPlayer::stanceFor(strength[c.slot], average);
        if (stance != c.stance)
            takeStance(c, stance);
    }
}

Combat::Pace MissionView::paceOf(size_t unit) const {
    // At warp is past its impulse speed *(guessed)*; a station never moves.
    if (unit >= _ships.size() || unit >= _physics.size() || !_physics[unit] || std::abs(_ships[unit].speed) < 0.5f)
        return Combat::Pace::kStopped;
    return std::abs(_ships[unit].speed) > _physics[unit]->cornerSpeed ? Combat::Pace::kWarp : Combat::Pace::kSlow;
}

void MissionView::think(double seconds) {
    _stanceClock -= seconds;
    if (_stanceClock <= 0) {
        _stanceClock += ComputerPlayer::kStanceUpdates / ComputerPlayer::kUpdatesPerSecond;
        weighStances();
    }
    // Each computer player looks once a second at what to build next.
    const std::vector<MapScene::Unit> &units = _scene.units();
    const Odf::Loader loader = loaderOf(_files.files());
    for (Computer &c : _computers) {
        if (_teamsOff.count(c.slot))
            continue;
        // Its ships' goals, each ten seconds (guessed: its personality's
        // recompute_strategy_period, 200, says no unit).
        c.plan -= seconds;
        if (c.plan <= 0) {
            c.plan = 10;
            plan(c);
        }
        c.think -= seconds;
        if (c.think > 0)
            continue;
        c.think = 1;
        // What it has, built or on order.
        std::map<std::string, int> owned;
        for (size_t i = 0; i < units.size(); ++i)
            if (units[i].slot == c.slot && !_dead[i])
                ++owned[text::toLower(units[i].odf)];
        for (const auto &[builder, queue] : _queues)
            if (builder < units.size() && units[builder].slot == c.slot)
                for (const Order &o : queue)
                    ++owned[text::toLower(o.item)];
        for (const Site &s : _sites)
            if (s.builder < units.size() && units[s.builder].slot == c.slot)
                ++owned[text::toLower(s.order.item)];
        const auto next = ComputerPlayer::nextOf(c.list, owned);
        if (!next)
            continue;
        const ComputerPlayer::Element &e = c.list[*next];
        Odf def;
        if (def.load(e.type + ".odf", loader))
            continue;
        if (def.number("enginesHitPoints") > 0) {
            // A ship: from a builder of its with nothing on order.
            for (size_t i = 0; i < units.size(); ++i) {
                if (units[i].slot != c.slot || _dead[i] || !_queues[i].empty())
                    continue;
                const std::vector<std::string> items = buildListOf(units[i].odf, loader);
                if (std::find(items.begin(), items.end(), e.type) == items.end())
                    continue;
                const Order order = orderOf(e.type, loader);
                if (pay(c.bank, order)) {
                    _tallies[c.slot].spent += order.dilithium;
                    _queues[i].push_back(order);
                }
                break;
            }
            continue;
        }
        // A station: by an idle construction ship of its, where there's room.
        for (size_t i = 0; i < units.size(); ++i) {
            if (units[i].slot != c.slot || _dead[i] || _classes[i] != "constructionrig" || _ships[i].goal)
                continue;
            if (std::any_of(_sites.begin(), _sites.end(), [i](const Site &s) { return s.builder == i; }))
                continue;
            const Footprint station = footprintOf(e.type, {}, 0).value_or(Footprint{});
            if (auto at = siteFor(c, e, station))
                startSite(i, *at, e.type, station);
            break;
        }
    }
}

void MissionView::raise(const Site &site) {
    if (site.builder >= _scene.units().size())
        return;
    const MapScene::Unit &from = _scene.units()[site.builder];
    MapScene::StartUnit unit;
    unit.odf = site.order.item;
    unit.at = site.at;
    unit.slot = from.slot;
    unit.colour = from.colour;
    if (!_scene.addUnit(unit, 0))
        return;
    ++_tallies[from.slot].stationsCreated;
    // Its officerGain on its player's limit, as the starting units' are
    // counted (a skirmish's starbase's 20 over its 100).
    Odf built;
    if (!built.load(site.order.item + ".odf", loaderOf(_files.files())))
        if (Hud::Resources *bank = bankOf(from.slot))
            bank->maxOfficers += int(built.number("officerGain"));
    _moves.push_back(false);
    _physics.emplace_back();
    addFighter(site.order.item);
    if (auto f = footprintOf(site.order.item, site.at, 0))
        _footprints.push_back(*f);
    Movement::Ship standing;
    standing.at = site.at;
    _ships.push_back(standing);
    // A mining station comes with a freighter of its race's (its letter,
    // then "freight": ffreight), as the original's did, and sets it to
    // work on the nearest moon.
    Odf def;
    if (_moons.empty() || def.load(site.order.item + ".odf", loaderOf(_files.files())) ||
        !text::equalsIgnoreCase(def.string("classLabel"), "mining"))
        return;
    const size_t station = _scene.units().size() - 1;
    const std::string freighter = site.order.item.substr(0, 1) + "freight";
    if (auto unit = addShip(freighter, site.at, 0, _scene.units()[station]))
        _freighters.push_back({*unit, station});
}

void MissionView::addFighter(const std::string &odf) {
    Odf def;
    const Odf::Loader loader = loaderOf(_files.files());
    const bool read = !def.load(odf + ".odf", loader);
    Combat::Vitals vitals = read ? Combat::vitalsOf(def) : Combat::Vitals{};
    // Its crew's strengths, its race's (fed.odf...) where it gives none.
    if (read) {
        Odf race;
        const std::string raceFile = Combat::raceFileOf(def.string("race"));
        if (raceFile.empty() || race.load(raceFile + ".odf", loader))
            race = Odf{};
        Combat::raceInto(vitals, def, race);
    }
    _vitals.push_back(vitals);
    _weapons.push_back(read ? Combat::weaponsOf(def, loader) : std::vector<Combat::Weapon>{});
    _targets.push_back(-1);
    _chasing.push_back(false);
    _specials.push_back(read ? Combat::specialsOf(def, loader) : std::vector<Combat::Special>{});
    _cloaked.push_back(false);
    _cloakedWith.emplace_back(0, 0);
    _effects.emplace_back();
    _dead.push_back(false);
    _scans.push_back(read ? float(def.number("rangeScan", 500)) : 500.0f);
    _classes.push_back(read ? text::toLower(def.string("classLabel")) : std::string());
    if (_classes.back() == "wormhole_obj")
        addWormhole(def, _classes.size() - 1, std::nullopt);
}

int MissionView::liveUnitAt(const gfx::Mat4 &viewProj, int x, int y) const {
    const int unit = unitAt(_scene.units(), viewProj, camera(), x, y);
    return unit >= 0 && ((size_t(unit) < _dead.size() && _dead[size_t(unit)]) || !_scene.shown(size_t(unit))) ? -1
                                                                                                              : unit;
}

void MissionView::fight(double seconds) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const size_t count = std::min({units.size(), _vitals.size(), _weapons.size(), _targets.size(), _dead.size()});
    // Its fires burn while one of its systems is out: the original's ship
    // caught fire after the first volley on its hull.
    for (size_t i = 0; i < count; ++i)
        if (!_dead[i]) {
            // No special energy back under a cloak (0x4f5d30).
            const float energy = _vitals[i].energy;
            Combat::regenerate(_vitals[i], seconds);
            if (i < _cloaked.size() && _cloaked[i])
                _vitals[i].energy = energy;
            _scene.setDamaged(i, _vitals[i].damaged());
        }
    uncloakHit();
    for (std::vector<Combat::Special> &specials : _specials)
        for (Combat::Special &sp : specials)
            sp.ready = std::max(0.0, sp.ready - seconds);
    moveMines(seconds);
    const double now = double(_last) / 1000;
    // Self-destructs counted down: the unit gone, and everything within
    // its radius hit by its hull's worth, less with distance *(guessed)*.
    std::vector<size_t> gone;
    for (const auto &[unit, at] : _selfDestructAt)
        if (now >= at)
            gone.push_back(unit);
    for (size_t unit : gone) {
        _selfDestructAt.erase(unit);
        if (_dead[unit])
            continue;
        float radius = 0;
        for (const Combat::Special &sp : _specials[unit])
            if (sp.kind == "selfdestruct")
                radius = sp.radius;
        const gfx::Vec3 a = units[unit].middle();
        const int by = units[unit].slot;
        destroy(unit, by);
        std::vector<size_t> caught;
        for (size_t j = 0; j < count; ++j) {
            const gfx::Vec3 b = units[j].middle();
            const float d = std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
            if (_dead[j] || radius <= 0 || d > radius)
                continue;
            // Its shockwave's damageBase, 380 give or take 10 (xselfd),
            // less with the distance *(guessed: the falloff)*.
            _roll = _roll * 1664525u + 1013904223u;
            const float rolled = 380 + 10 * (2 * float(_roll >> 8) / float(1u << 24) - 1);
            Combat::hit(_vitals[j], rolled * (1 - d / radius));
            if (!_vitals[j].alive())
                caught.push_back(j);
        }
        for (size_t j : caught)
            if (!_dead[j])
                destroy(j, by);
    }
    // Beams: lost when the gap between the two, less their sizes, passes
    // their reach, when the holder can't pay their cost a second, or when
    // their own rule fails (the beams' classes' updates).
    std::erase_if(_holds, [&](const Hold &h) {
        if (h.unit >= count || h.target >= count || _dead[h.unit] || _dead[h.target] || now >= h.until)
            return true;
        const gfx::Vec3 a = units[h.unit].middle();
        const gfx::Vec3 b = units[h.target].middle();
        const float gap =
            std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) - units[h.unit].radius() - units[h.target].radius();
        if (gap > h.range)
            return true;
        const float cost = h.cost * float(seconds);
        Combat::Vitals &holder = _vitals[h.unit];
        const Combat::Vitals &held = _vitals[h.target];
        if (holder.energy < cost)
            return true;
        if (h.kind == "giveShields" && (holder.energy < 20 * float(seconds) || held.shields >= held.maxShields))
            return true;
        if (h.kind == "drainShields" && held.shields < h.rate * float(seconds))
            return true;
        if (h.kind == "assimilate" && held.derelict())
            return true;
        if (h.kind == "tow" && held.systems[Combat::kEngines].working() && !held.derelict() &&
            !active(_effects[h.target].slowed))
            return true;
        holder.energy -= cost;
        return false;
    });
    runEffects(seconds);
    runCrew(seconds);
    runDecommissions(seconds);
    // Settled? Checked each frame, as the original's (0x43d6d9).
    if (!_settledAt && !_players.empty()) {
        std::set<int> standing;
        // A derelict, without crew, doesn't keep its player in (+0x4dc).
        for (size_t i = 0; i < _scene.units().size() && i < _dead.size(); ++i)
            if (!_dead[i] && !(i < _vitals.size() && _vitals[i].derelict()))
                standing.insert(_scene.units()[i].slot);
        if (settled(_players, standing, _alliedVictory))
            _settledAt = double(_last) / 1000;
    }
    // A detector's sight wears off; a cloaked unit seen shows meanwhile.
    std::erase_if(_revealedUntil, [now](const auto &r) { return now >= r.second; });
    for (size_t i = 0; i < count && i < _cloaked.size(); ++i)
        if (_cloaked[i])
            _scene.setCloaked(i, !_revealedUntil.count(i));
    // Each weapon fires at its unit's target when it's ready and the target
    // is in its range: the starbase's eight all at once, every 4.5 seconds.
    for (size_t i = 0; i < count; ++i) {
        const int t = _targets[i];
        if (t < 0)
            continue;
        if (_dead[i] || size_t(t) >= count || _dead[size_t(t)]) {
            _targets[i] = -1;
            continue;
        }
        const gfx::Vec3 from = units[i].middle();
        const gfx::Vec3 to = units[size_t(t)].middle();
        const float distance = std::hypot(to[0] - from[0], to[1] - from[1], to[2] - from[2]);
        for (Combat::Weapon &w : _weapons[i]) {
            // Reloading at its FireModifier times the pace while Death
            // Chant lasts (TimedHyper, 0x503153).
            const Effects &e = _effects[i];
            w.ready = std::max(0.0, w.ready - seconds * (active(e.fireFaster) ? double(e.fireBy) : 1.0));
            // Its weapons out, it doesn't fire.
            if (w.ready > 0 || distance > w.range || !_vitals[i].systems[Combat::kWeapons].working() || _cloaked[i] ||
                active(e.weaponsOff) || active(e.frozen) || disabled(i))
                continue;
            // A roll from a little generator of our own, the same each game.
            _roll = _roll * 1664525u + 1013904223u;
            const float roll = float(_roll >> 8) / float(1u << 24);
            // Another roll for whether it hits, by its hit chance for how
            // it and its target move.
            _roll = _roll * 1664525u + 1013904223u;
            const bool miss =
                float(_roll >> 8) / float(1u << 24) >= Combat::hitChanceOf(w, paceOf(i), paceOf(size_t(t)));
            _shots.push_back({size_t(t),
                              from,
                              w.speed,
                              Combat::damageOf(w, roll),
                              w.sprite,
                              from,
                              units[i].slot,
                              {w.shieldCrew, w.hullCrew, w.threshold},
                              miss,
                              int(i)});
            w.ready = w.delay;
        }
    }
    // The shots fly to their targets' middles, and hit there.
    std::vector<std::pair<size_t, int>> killed; // and by whom
    for (size_t s = 0; s < _shots.size();) {
        Combat::Shot &shot = _shots[s];
        if (shot.target >= count || _dead[shot.target]) {
            _shots.erase(_shots.begin() + std::ptrdiff_t(s));
            continue;
        }
        const gfx::Vec3 to = units[shot.target].middle();
        const float dx = to[0] - shot.at[0];
        const float dy = to[1] - shot.at[1];
        const float dz = to[2] - shot.at[2];
        const float left = std::hypot(dx, dy, dz);
        const float step = float(shot.speed * seconds);
        if (left <= step || shot.speed <= 0) {
            Combat::Vitals &v = _vitals[shot.target];
            const bool shielded = v.shields > 0;
            if (shot.miss || active(_effects[shot.target].invulnerable)) {
                _shots.erase(_shots.begin() + std::ptrdiff_t(s));
                continue;
            }
            // A reflector sends it back at whoever fired it.
            if (active(_effects[shot.target].reflecting) && shot.shooter >= 0 && size_t(shot.shooter) < count &&
                !_dead[size_t(shot.shooter)] && size_t(shot.shooter) != shot.target) {
                const size_t back = size_t(shot.shooter);
                shot.slot = units[shot.target].slot;
                shot.shooter = int(shot.target);
                shot.target = back;
                continue;
            }
            _roll = _roll * 1664525u + 1013904223u;
            Combat::hit(v, shot.damage, shot.crew, float(_roll >> 8) / float(1u << 24));
            if (shot.slot != units[shot.target].slot)
                _hitAt[shot.target] = double(_last) / 1000;
            // The shield glows where it's hit, a new hit starting it over;
            // a hit on the hull alone doesn't (guessed).
            if (shielded) {
                std::erase_if(_shieldHits, [&](const ShieldHit &h) { return h.unit == shot.target; });
                _shieldHits.push_back(
                    {shot.target, shot.from, double(_last) / 1000, v.maxShields > 0 ? v.shields / v.maxShields : 0});
            }

            if (!v.alive())
                killed.emplace_back(shot.target, shot.slot);
            _shots.erase(_shots.begin() + std::ptrdiff_t(s));
            continue;
        }
        shot.at = {shot.at[0] + dx / left * step, shot.at[1] + dy / left * step, shot.at[2] + dz / left * step};
        ++s;
    }
    for (const auto &[k, by] : killed)
        if (!_dead[k])
            destroy(k, by);
}

void MissionView::destroy(size_t unit, int by) {
    // One a mission's script keeps (CraftCannotDie, its byte 0x678) stays.
    if (_cannotDie.count(unit)) {
        _vitals[unit].destroyed = false;
        _vitals[unit].carry = 0;
        return;
    }
    _dead[unit] = true;
    _explodedAt[unit] = double(_last) / 1000;
    // Counted as the original's debriefing showed: lost by its player, and
    // destroyed in its player's row too, not its killer's, when another's
    // shots did it (two ships killed by the computer's starbase: the
    // player's row 2 and 2, the computer's 0).
    const int slot = _scene.units()[unit].slot;
    const bool ship = unit < _moves.size() && _moves[unit];
    ++(ship ? _tallies[slot].shipsLost : _tallies[slot].stationsLost);
    if (by != slot)
        ++(ship ? _tallies[slot].shipsDestroyed : _tallies[slot].stationsDestroyed);
    // It explodes where it was: its fireball's model (craft.odf's xfirebmd,
    // whose dbName is xplode).
    {
        const Odf::Loader loader = loaderOf(_files.files());
        Odf def;
        Odf fireball;
        if (!def.load(_scene.units()[unit].odf + ".odf", loader) && !def.string("fireball").empty() &&
            !fireball.load(def.string("fireball") + ".odf", loader) && !fireball.string("dbName").empty()) {
            Explosion e;
            e.model = fireball.string("dbName");
            e.at = _scene.units()[unit].middle();
            e.began = double(_last) / 1000;
            // Its shockwave follows, xfirebmd's shockwaveDelay on, out to
            // xshock's radiusFactor times the unit's size; only from a unit
            // as big as shockwaveMinimum: the original's construction ship,
            // about 30, threw none.
            Odf shock;
            const float size = _scene.units()[unit].radius();
            if (!fireball.string("shockwave").empty() && size >= float(fireball.number("shockwaveMinimum")) &&
                !shock.load(fireball.string("shockwave") + ".odf", loader)) {
                e.shockDelay = fireball.number("shockwaveDelay");
                e.shockRate = float(shock.number("rate"));
                e.shockRadius = float(shock.number("radiusFactor")) * size;
                e.shockFade = float(shock.number("fadeFactor"));
                e.shockRays = int(shock.number("rays", 16));
                e.shockTexture = shock.string("texture");
                // "1.0 0.3 0.0": its three numbers in turn.
                const std::string colour = shock.string("color");
                const char *at = colour.c_str();
                for (size_t k = 0; k < 3 && *at; ++k) {
                    char *end = nullptr;
                    e.shockColour[k] = std::strtof(at, &end);
                    if (end == at)
                        break;
                    at = end;
                }
            }
            _explosions.push_back(e);
        }
    }
    // And breaks up, its pieces flying out.
    _scene.breakUp(unit, double(_last) / 1000);
    _scene.hideUnit(unit);
    // Its officers go back to the count: the original's 4 of 120 became 2
    // as its construction ship went.
    const MapScene::Unit &u = _scene.units()[unit];
    if (Hud::Resources *bank = bankOf(u.slot)) {
        Odf def;
        if (!def.load(u.odf + ".odf", loaderOf(_files.files())))
            bank->officers = std::max(0, bank->officers - int(def.number("officerCost")));
    }
    forget(unit);
}

void MissionView::decommission(size_t unit) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    if (unit >= units.size() || _dead[unit] || _decommissioning.count(unit))
        return;
    Decommission d;
    // A ship must go to a repair bay, as its tooltip says; a station begins
    // where it stands.
    if (unit < _moves.size() && _moves[unit]) {
        d.bay = repairStationFor(unit);
        if (!d.bay)
            return;
        _ships[unit].goal = units[*d.bay].at;
        _ships[unit].route.clear();
        _targets[unit] = -1;
        _repairingAt.erase(unit);
    }
    _decommissioning[unit] = d;
}

void MissionView::runDecommissions(double seconds) {
    // RTS_CFG.h's DECONSTRUCT_PERCENT_TIME: the original's starbase, its
    // buildTime 100, came apart in about 30 seconds.
    constexpr double kDeconstructShare = 0.3;
    const std::vector<MapScene::Unit> &units = _scene.units();
    const Odf::Loader loader = loaderOf(_files.files());
    std::vector<size_t> done;
    for (auto &[unit, d] : _decommissioning) {
        if (_dead[unit] || (d.bay && _dead[*d.bay]))
            continue;
        Odf def;
        if (def.load(units[unit].odf + ".odf", loader))
            continue;
        if (d.left < 0) {
            if (d.bay) {
                const float reach = units[*d.bay].radius() + units[unit].radius() + 60;
                const gfx::Vec3 a = units[unit].middle();
                const gfx::Vec3 b = units[*d.bay].middle();
                if (std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) > reach) {
                    if (!_ships[unit].goal)
                        _ships[unit].goal = units[*d.bay].at;
                    continue;
                }
                _ships[unit].goal.reset();
                _ships[unit].speed = 0;
            }
            d.left = kDeconstructShare * def.number("buildTime");
        }
        d.left -= seconds;
        if (d.left <= 0)
            done.push_back(unit);
    }
    for (size_t unit : done) {
        // Its decommissionValue, its dilithiumCost unless it says (0x466ab0),
        // and its crew back; its officers and the officers it gave gone. Not
        // lost, as the debriefing counts: the original's starbase wasn't.
        Odf def;
        (void)def.load(units[unit].odf + ".odf", loader);
        if (Hud::Resources *bank = bankOf(units[unit].slot)) {
            bank->dilithium += int(def.number("decommissionValue", def.number("dilithiumCost")));
            bank->crew += int(_vitals[unit].crew);
            bank->officers = std::max(0, bank->officers - int(def.number("officerCost")));
            bank->maxOfficers = std::max(0, bank->maxOfficers - int(def.number("officerGain")));
        }
        _dead[unit] = true;
        _scene.hideUnit(unit);
        forget(unit);
    }
}

void MissionView::forget(size_t unit) {
    std::erase(_selection, int(unit));
    for (int &t : _targets)
        if (t == int(unit))
            t = -1;
    if (unit < _ships.size()) {
        _ships[unit].goal.reset();
        _ships[unit].speed = 0;
    }
    std::erase_if(_freighters, [unit](const Freighter &f) { return f.unit == unit || f.station == unit; });
    std::erase_if(_sites, [unit](const Site &s) { return s.builder == unit; });
    _decommissioning.erase(unit);
    std::erase_if(_decommissioning, [unit](const auto &d) { return d.second.bay == unit; });
    _boundFor.erase(unit);
    std::erase_if(_passages, [unit](const Passage &p) { return p.unit == unit; });
    for (size_t i = 0; i < _wormholes.size(); ++i)
        if (_wormholes[i].unit == unit && !_wormholes[i].gone)
            loseWormhole(i);
}

std::optional<size_t> MissionView::addShip(const std::string &odf, const gfx::Vec3 &at, float heading,
                                           const MapScene::Unit &owner) {
    MapScene::StartUnit unit;
    unit.odf = odf;
    unit.at = at;
    unit.slot = owner.slot;
    unit.colour = owner.colour;
    if (!_scene.addUnit(unit, heading))
        return std::nullopt;
    Odf def;
    const Odf::Loader loader = loaderOf(_files.files());
    std::optional<Movement::Physics> physics;
    if (!def.load(odf + ".odf", loader) && def.number("enginesHitPoints") > 0)
        physics = Movement::physicsOf(def, loader);
    _moves.push_back(physics.has_value());
    _physics.push_back(physics);
    addFighter(odf);
    Movement::Ship ship;
    ship.at = at;
    ship.heading = heading;
    _ships.push_back(ship);
    return _ships.size() - 1;
}

void MissionView::build(double seconds) {
    // The freighters, each at its nearest moon.
    for (Freighter &f : _freighters) {
        Hud::Resources *bank = f.station < _scene.units().size() ? bankOf(_scene.units()[f.station].slot) : nullptr;
        if (f.unit >= _ships.size() || f.station >= _ships.size() || _moons.empty() || !bank)
            continue;
        const gfx::Vec3 &station = _ships[f.station].at;
        const gfx::Vec3 *moon = &_moons.front();
        for (const gfx::Vec3 &m : _moons)
            if (std::hypot(m[0] - station[0], m[2] - station[2]) <
                std::hypot((*moon)[0] - station[0], (*moon)[2] - station[2]))
                moon = &m;
        const int before = bank->dilithium;
        work(f, _ships[f.unit], *moon, station, bank->dilithium, seconds);
        _tallies[_scene.units()[f.station].slot].collected += std::max(0, bank->dilithium - before);
    }
    // The stations: begun, and paid for, when the builder has got there.
    for (size_t i = 0; i < _sites.size();) {
        Site &site = _sites[i];
        const Movement::Ship &ship = _ships[site.builder];
        Hud::Resources *bank = bankOf(_scene.units()[site.builder].slot);
        if (!site.started && !ship.goal && bank && pay(*bank, site.order)) {
            site.started = true;
            _tallies[_scene.units()[site.builder].slot].spent += site.order.dilithium;
        }
        if (site.started)
            site.order.elapsed += seconds;
        if (site.started && site.order.elapsed >= site.order.time) {
            const Site done = site;
            _sites.erase(_sites.begin() + std::ptrdiff_t(i));
            raise(done);
            continue;
        }
        ++i;
    }
    std::vector<std::pair<size_t, std::string>> done;
    for (auto &[builder, queue] : _queues) {
        if (queue.empty())
            continue;
        queue.front().elapsed += seconds;
        if (queue.front().elapsed >= queue.front().time) {
            done.emplace_back(builder, queue.front().item);
            queue.erase(queue.begin());
        }
    }
    for (const auto &[builder, item] : done)
        launch(builder, item);
}

void MissionView::launch(size_t builder, const std::string &item) {
    if (builder >= _scene.units().size())
        return;
    const MapScene::Unit from = _scene.units()[builder];
    // A research station's pod isn't a ship: built, its special weapon is
    // the player's.
    Odf def;
    if (!def.load(item + ".odf", loaderOf(_files.files())) && text::equalsIgnoreCase(def.string("classLabel"), "pod")) {
        _researched[from.slot].insert(Combat::researchKeyOf(def.string("tooltip")));
        // Fixed to the station's next free pod node (podHardpoints), as the
        // original's was to one of its arms.
        Odf station;
        if (!station.load(from.odf + ".odf", loaderOf(_files.files()))) {
            const std::vector<std::string> nodes = station.strings("podHardpoints");
            int &used = _podsAt[builder];
            if (size_t(used) < nodes.size() && _scene.attachTo(builder, nodes[size_t(used)], item))
                ++used;
        }
        return;
    }
    const gfx::Vec3 to = launchPointOf(from.at);
    gfx::Vec3 at = from.middle();
    at[1] = to[1];
    const float heading = std::atan2(to[0] - at[0], to[2] - at[2]);
    // It flies out of the builder, through it.
    const std::optional<size_t> unit = addShip(item, at, heading, from);
    if (unit)
        ++_tallies[from.slot].shipsCreated;
    if (unit && _physics[*unit])
        _ships[*unit].goal = to;
}

std::vector<std::string> MissionView::ownedOdfs() const {
    std::vector<std::string> owned;
    for (const MapScene::Unit &u : _scene.units())
        if (u.slot == _slot)
            owned.push_back(u.odf);
    return owned;
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

std::vector<Hud::Minimap::Icon> MissionView::mapIconsOf(const BznMap &map, const Odf::Loader &loader) {
    // Each definition's mark (mapIcon), or its patch (mapSprite, mapSize).
    std::map<std::string, Hud::Minimap::Icon> icons;
    std::vector<Hud::Minimap::Icon> out;
    for (const BznMap::Object &o : map.objects) {
        const std::string odf = text::toLower(o.odf);
        auto it = icons.find(odf);
        if (it == icons.end()) {
            Odf def;
            Hud::Minimap::Icon icon;
            if (!def.load(odf + ".odf", loader)) {
                icon.sprite = def.string("mapIcon");
                if (icon.sprite.empty() && !def.string("mapSprite").empty()) {
                    icon.sprite = def.string("mapSprite");
                    icon.size = std::max(1, int(def.number("mapSize", 5)));
                    icon.pattern = text::equalsIgnoreCase(def.string("classLabel"), "nebula_obj");
                }
            }
            it = icons.emplace(odf, icon).first;
        }
        if (!it->second.sprite.empty()) {
            Hud::Minimap::Icon icon = it->second;
            icon.x = o.position[0];
            icon.z = o.position[2];
            out.push_back(icon);
        }
    }
    return out;
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
    // The map's objects on a team are its units, in play, where the map
    // has them; the player's are team `slot`'s.
    std::vector<MapScene::StartUnit> units;
    for (size_t i = 0; i < parsed.objects.size(); ++i) {
        const BznMap::Object &o = parsed.objects[i];
        if (o.team < 1)
            continue;
        MapScene::StartUnit u;
        u.odf = o.odf;
        u.at = o.position;
        u.slot = o.team;
        u.heading = std::atan2(o.transform[6], o.transform[8]);
        u.object = i;
        units.push_back(u);
    }
    Player me;
    me.slot = slot;
    me.race = Combat::raceFileOf(parsed.race);
    if (me.race.empty())
        me.race = "fed";
    me.human = true;
    _players = {me};
    // No shroud yet: what the original's casts in a campaign isn't known.
    MapScene::Cover cover;
    cover.shroud = false;
    // The player's counters as the map saves them, its officers' limit
    // less what its units take (0x446640); the RTS_CFG.h start
    // (cfgStartingDilithium, cfgStartingCrew) is what the maps hold.
    Hud::Resources start;
    if (size_t(slot) < parsed.players.size()) {
        const BznMap::PlayerStart &saved = parsed.players[size_t(slot)];
        start = {int(saved.dilithium), int(saved.crew), 0, saved.officerLimit};
    }
    if (Error err = play(renderer, parsed, units, cover, start, millis, parsed.techTree))
        return err;
    // A campaign's every other player, 2 to 8, is the computer's, not
    // thinking till a script turns it on, with no personality but every
    // priority 0 and no build list; its counters the map's (0x430360,
    // 0x418c20, 0x446640). Its stance is never weighed (0x43081c).
    for (int other = 2; other <= 8; ++other) {
        if (other == slot)
            continue;
        Computer c;
        c.slot = other;
        for (const MapScene::StartUnit &u : units)
            if (u.slot == other) {
                Odf def;
                if (!def.load(u.odf + ".odf", loaderOf(_files.files())))
                    c.race = Combat::raceFileOf(def.string("race"));
                break;
            }
        c.personality = Strategy::bare();
        if (size_t(other) < parsed.players.size()) {
            const BznMap::PlayerStart &saved = parsed.players[size_t(other)];
            c.bank = {int(saved.dilithium), int(saved.crew), 0, saved.officerLimit};
        }
        _computers.push_back(c);
        _teamsOff.insert(other);
    }
    if (team >= 0)
        _scene.build(parsed, units, "", _scene.sightsOf(parsed, {}, team), cover);
    for (const BznMap::Path &p : parsed.paths)
        _paths.emplace(p.name, p.points);
    for (const BznMap::Object &o : parsed.objects) {
        _mapObjects.emplace_back(o.label, o.position);
        _mapObjectOdfs.push_back(text::toLower(o.odf));
        Odf def;
        if (!def.load(o.odf + ".odf", loaderOf(_files.files())) &&
            text::equalsIgnoreCase(def.string("classLabel"), "wormhole_obj"))
            addWormhole(def, std::nullopt, _mapObjects.size() - 1);
    }
    // Its scripts, from the map's [AiMission]: the script's DLL, then the
    // rules' (formats/bzn.md).
    if (parsed.scripts.size() == 2 && text::hasSuffixIgnoreCase(parsed.scripts[0], "S.dsl")) {
        std::vector<uint8_t> script;
        std::vector<uint8_t> rules;
        if (_files.read("missions/" + parsed.scripts[0], script) || _files.read("missions/" + parsed.scripts[1], rules))
            return Error::fail(map + ": its scripts aren't in the install");
        _missionScripts = std::make_unique<MissionScripts>();
        _missionScripts->game = [this](const std::string &name, const MissionScripts::Arguments &args) {
            return scriptCall(name, args);
        };
        _missionScripts->readFile = [this](const std::string &path, std::vector<uint8_t> &data) {
            return _files.read(path, data);
        };
        const std::string name = parsed.scripts[0].substr(0, parsed.scripts[0].size() - 5);
        if (Error err = _missionScripts->load(name, script, rules))
            return Error::fail(map + ": " + err.message);
    }
    return Error::success();
}

Error MissionView::startSkirmish(gfx::Renderer3D *renderer, const std::string &map, const std::vector<Player> &players,
                                 MapScene::Cover cover, const Hud::Resources &start, uint64_t millis, int techLevel) {
    stop();
    _players = players;
    BznMap parsed;
    std::vector<uint8_t> data;
    if (Error err = _files.read("bzn/" + map, data))
        return err;
    if (Error err = parsed.parse(data))
        return Error::fail(map + ": " + err.message);
    // The tech level's tree, not the map's techTreeFile: 0x43ea06 reads
    // techlvl.odf's techfile<level>, tech1.tt failing that, for every player.
    Odf levels;
    const std::string treeFile = levels.load("techlvl.odf", loaderOf(_files.files()))
                                     ? std::string("tech1.tt")
                                     : levels.string("techfile" + std::to_string(techLevel), "tech1.tt");
    std::vector<MapScene::StartUnit> units;
    for (const Player &p : players)
        for (MapScene::StartUnit &u : MapScene::startUnitsOf(parsed, MapScene::standardUnitsOf(p.race, _files.files())))
            if (u.slot == p.slot) {
                u.colour = p.colour;
                units.push_back(u);
            }
    return play(renderer, parsed, units, cover, start, millis, treeFile);
}

Error MissionView::play(gfx::Renderer3D *renderer, const BznMap &parsed, const std::vector<MapScene::StartUnit> &units,
                        MapScene::Cover cover, const Hud::Resources &start, uint64_t millis,
                        const std::string &treeFile) {
    const std::vector<Player> &players = _players;
    _renderer = renderer;
    _scene.attach(renderer);
    _techTree = TechTree{};
    std::vector<uint8_t> tree;
    if (!_files.read(treeFile, tree))
        _techTree.parse(std::string_view(reinterpret_cast<const char *>(tree.data()), tree.size()));
    std::vector<MapScene::StartUnit> mine;
    int slot = 1;
    for (const Player &p : players)
        if (p.human) {
            slot = p.slot;
            for (const MapScene::StartUnit &u : units)
                if (u.slot == p.slot)
                    mine.push_back(u);
        }
    cover.slot = slot;
    _scene.build(parsed, units, "",
                 (cover.shroud || cover.fog) && !mine.empty() ? _scene.sightsOf(parsed, mine, -1)
                                                              : std::vector<MapScene::Sight>{},
                 cover);
    _overview = startOf(parsed, slot);
    _slot = slot;
    // The path planner's terrain, each object marking its square as the
    // program's do (0x4097c0), by RTS_CFG.h's settings: a nebula its
    // effectRadius times NEBULAE_PATHING_SAFETY_COEFFICIENT (1.8) each way
    // at its own pathing_cost, 199 if it says none (0x449934); an asteroid
    // belt squares ASTEROID_BELT_PATHING_DISTANCE (150) across at 201, one
    // each 75 over it (0x433026); a black hole impassable, its size times
    // BLACKHOLE_PATHING_SAFETY_COEFFICIENT (1.5) (0x44dd25); a wormhole at 500,
    // its size and 80 (0x451415). The holes' sizes are their models'
    // *(guessed: the program's fields for them aren't named)*.
    _terrain = Movement::Terrain::over(parsed.area[0], parsed.area[4], parsed.area[1], parsed.area[5]);
    for (const BznMap::Object &o : parsed.objects) {
        Odf def;
        if (def.load(o.odf + ".odf", loaderOf(_files.files())))
            continue;
        const std::string label = text::toLower(def.string("classLabel"));
        auto sizeOf = [this, &o]() {
            const auto box = _scene.boxOf(o.odf);
            return box ? std::hypot(box->second[0] - box->first[0], box->second[2] - box->first[2]) / 2 : 0.0f;
        };
        if (label == "nebula_obj") {
            _terrain.mark(o.position, float(def.number("effectRadius")) * 1.8f,
                          int(def.number("pathing_cost", Movement::Terrain::kCannotEnter - 1)));
        } else if (label == "asteroidfield") {
            constexpr float kBelt = 150;
            const float radius = float(def.number("fieldRadius"));
            for (float dz = -radius; dz <= radius; dz += kBelt / 2)
                for (float dx = -radius; dx <= radius; dx += kBelt / 2)
                    if (std::hypot(dx, dz) <= radius)
                        _terrain.mark({o.position[0] + dx, 0, o.position[2] + dz}, kBelt / 2, 201);
        } else if (label == "blackhole_obj") {
            _terrain.mark(o.position, sizeOf() * 1.5f, Movement::Terrain::kImpassable);
        } else if (label == "wormhole_obj") {
            _terrain.mark(o.position, sizeOf() + 80, 500);
        }
    }
    // The pods there are: what the starting units can build, and what that
    // can, on down. A special weapon a pod gives wants it researched.
    {
        const Odf::Loader podLoader = loaderOf(_files.files());
        std::set<std::string> seen;
        std::vector<std::string> open;
        for (const MapScene::Unit &u : _scene.units())
            open.push_back(text::toLower(u.odf));
        while (!open.empty()) {
            const std::string odf = open.back();
            open.pop_back();
            if (!seen.insert(odf).second)
                continue;
            Odf def;
            if (def.load(odf + ".odf", podLoader))
                continue;
            if (text::equalsIgnoreCase(def.string("classLabel"), "pod"))
                _pods.insert(Combat::researchKeyOf(def.string("tooltip")));
            for (int i = 0; !def.string("buildItem" + std::to_string(i)).empty(); ++i)
                open.push_back(text::toLower(def.string("buildItem" + std::to_string(i))));
        }
    }
    _mapIcons = mapIconsOf(parsed, loaderOf(_files.files()));
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
        addFighter(u.odf);
        Movement::Ship ship;
        ship.at = u.at;
        ship.heading = u.heading;
        _ships.push_back(ship);
    }
    // What a station can't be placed on: the standing units (stations have
    // footprintBuffer 80, from station.odf), and the map's objects to keep
    // clear of (avoidMe: the dilithium moons), with 10, which gives the
    // original's moon its 3 x 3 cells (inferred).
    _footprints.clear();
    for (size_t i = 0; i < _scene.units().size(); ++i)
        if (!_moves[i])
            if (auto f = footprintOf(_scene.units()[i].odf, _scene.units()[i].at, 0))
                _footprints.push_back(*f);
    _moons.clear();
    for (const BznMap::Object &o : parsed.objects) {
        Odf def;
        if (def.load(o.odf + ".odf", loader))
            continue;
        if (def.number("avoidMe") > 0)
            if (auto f = footprintOf(o.odf, o.position, 10))
                _footprints.push_back(*f);
        // The dilithium moons are the map's scrap (mdmoon's classLabel).
        if (text::equalsIgnoreCase(def.string("classLabel"), "scrap"))
            _moons.push_back(o.position);
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
    // The race's music: its track0, track1... until one's missing (0x441ff0).
    Odf raceFile;
    if (!raceFile.load(myRace + ".odf", loaderOf(_files.files())))
        for (int i = 0; !raceFile.string("track" + std::to_string(i)).empty(); ++i)
            _tracks.push_back(raceFile.string("track" + std::to_string(i)));
    _resources = resourcesOf(start, myUnits, _files.files());
    _crewCarry = 0;
    // The computer players, each with its race's build list and its own
    // counters, starting as the player does.
    _computers.clear();
    _stanceClock = ComputerPlayer::kStanceUpdates / ComputerPlayer::kUpdatesPerSecond;
    for (const Player &p : players) {
        if (p.human)
            continue;
        Computer c;
        c.slot = p.slot;
        c.race = p.race;
        c.stances = p.stances;
        std::vector<std::string> theirs;
        for (const MapScene::StartUnit &u : units)
            if (u.slot == p.slot)
                theirs.push_back(u.odf);
        c.bank = resourcesOf(start, theirs, _files.files());
        // Every player starts as the program starts them (0x4313ca), not
        // with its race's personality (federation.aip...).
        takeStance(c, ComputerPlayer::Stance::kNormal);
        _computers.push_back(c);
    }
    _held.clear();
    _last = millis;
    return Error::success();
}

Odf::Loader MissionView::odfLoader() const {
    return loaderOf(_files.files());
}

int MissionView::alertOf(size_t unit) const {
    auto it = _alerts.find(unit);
    return it == _alerts.end() ? kRedAlert : it->second;
}

void MissionView::setAlert(size_t unit, int level) {
    // Yellow or green drops what it was firing at (0x425ad4).
    if (level == kRedAlert)
        _alerts.erase(unit);
    else
        _alerts[unit] = level;
    if (level != kRedAlert && unit < _targets.size()) {
        _targets[unit] = -1;
        _picked.erase(unit);
    }
}

void MissionView::runAlerts() {
    // A green ship standing still, hit in the last second, goes to the
    // nearest shipyard of its player's, if there is one (0x428ba0); next
    // to it *(guessed: its ring's half and 50 short of its middle)*.
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    for (const auto &[unit, level] : _alerts) {
        if (level != kGreenAlert || unit >= _ships.size() || _dead[unit] || !_moves[unit] || _ships[unit].goal ||
            _scripted.count(unit))
            continue;
        auto hit = _hitAt.find(unit);
        if (hit == _hitAt.end() || now - hit->second >= 1)
            continue;
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && _classes[i] == "shipyard" && units[i].slot == units[unit].slot) {
                const float d = std::hypot(units[i].at[0] - _ships[unit].at[0], units[i].at[2] - _ships[unit].at[2]);
                if (d < bestDistance)
                    best = int(i), bestDistance = d;
            }
        if (best < 0)
            continue;
        const gfx::Vec3 yard = units[size_t(best)].at;
        const float off = ringHalfOf(units[size_t(best)]) + 50;
        const float d = std::max(bestDistance, 1.0f);
        _ships[unit].goal = gfx::Vec3{yard[0] + (_ships[unit].at[0] - yard[0]) / d * off, _ships[unit].at[1],
                                      yard[2] + (_ships[unit].at[2] - yard[2]) / d * off};
        _ships[unit].route.clear();
        _ships[unit].waiting = Movement::kSetOff;
    }
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
    if (auto cut = _cine.camera())
        return *cut;
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

void MissionView::drawEffects(double now) {
    // The glow's flip-book, xshldx01 (a name of the program's own), at
    // eighth of its 16 frames 0.4 seconds after a hit: 20 a second.
    constexpr double kGlowFrame = 0.05;
    std::erase_if(_shieldHits, [&](const ShieldHit &h) { return now - h.at >= 16 * kGlowFrame || _dead[h.unit]; });
    for (const ShieldHit &h : _shieldHits)
        // Green full to red empty, as the info panel's shield bar: (215,
        // 40, 0) the original's at a sixth.
        _scene.drawShieldHit(h.unit, h.from, int((now - h.at) / kGlowFrame), {1 - h.left, h.left, 0}, "xshldx01");
    // The explosion's sprites are drawn for 1.1 to 1.5 seconds (their
    // "draw" animations); it ends with the last.
    constexpr double kExplosion = 1.5;
    std::erase_if(_explosions, [&](const Explosion &e) {
        const double shock = e.shockRate > 0 ? e.shockDelay + e.shockRadius / e.shockRate : 0;
        return now - e.began >= std::max(kExplosion, shock);
    });
    for (const Explosion &e : _explosions) {
        if (now - e.began < kExplosion)
            _scene.drawEffect(e.model, e.at, now - e.began);
        const double t = now - e.began - e.shockDelay;
        if (e.shockRate <= 0 || t < 0)
            continue;
        // Out at its rate, fading over the last of its way.
        const float radius = float(t) * e.shockRate;
        const float left = 1 - radius / e.shockRadius;
        const float bright = e.shockFade > 0 ? std::clamp(left / e.shockFade, 0.0f, 1.0f) : 1.0f;
        _scene.drawShockwave(e.at, radius, e.shockRays, e.shockTexture,
                             {e.shockColour[0] * bright, e.shockColour[1] * bright, e.shockColour[2] * bright});
    }
    _scene.drawDebris(now);
    for (const Mine &m : _mines)
        _scene.drawEffect(m.special.model, m.at, now);
}

void MissionView::makeShotMesh() {
    if (!_renderer || _shotMesh)
        return;
    std::vector<gfx::Vertex> corners(4);
    const float xs[4] = {-1, 0, 0, -1};
    const float ys[4] = {-1, -1, 1, 1};
    const float us[4] = {0, 1, 1, 0};
    const float vs[4] = {0, 0, 1, 1};
    for (size_t i = 0; i < 4; ++i) {
        corners[i] = gfx::Vertex{};
        corners[i].pos[0] = xs[i], corners[i].pos[1] = ys[i];
        corners[i].uv[0] = us[i], corners[i].uv[1] = vs[i];
        std::fill(std::begin(corners[i].color), std::end(corners[i].color), uint8_t(255));
    }
    if (_renderer->createMesh(corners, {0, 1, 2, 0, 2, 3}, _shotMesh))
        _shotMesh = 0;
}

void MissionView::drawShots(const MapScene::Camera &view) {
    if (!_renderer || _shots.empty())
        return;
    makeShotMesh();
    if (!_shotMesh)
        return;
    // The ordnance's Length and Radius: the starbase's pulses' 50 and 8.
    constexpr float kLength = 50;
    constexpr float kRadius = 8;
    for (const Combat::Shot &shot : _shots) {
        auto look = _shotLooks.find(shot.sprite);
        if (look == _shotLooks.end()) {
            gfx::TextureId texture = 0;
            std::array<float, 4> rect{0, 0, 1, 1};
            if (const SpriteTable::Sprite *s = _hud.findSprite(shot.sprite)) {
                image::Image picture;
                std::vector<uint8_t> data;
                if (!_files.read("Textures/RGB/" + s->texture + ".tga", data) && !image::decode(data, picture) &&
                    picture.w > 0) {
                    gfx::TextureParams params;
                    params.clamp = true;
                    if (_renderer->createTexture(picture.w, picture.h, picture.rgba.data(), params, texture))
                        texture = 0;
                    const double rw = s->referenceWidth > 0 ? s->referenceWidth : picture.w;
                    const double rh = s->referenceHeight > 0 ? s->referenceHeight : picture.h;
                    rect = {float(s->u / rw), float(s->v / rh), float((s->u + s->w) / rw), float((s->v + s->h) / rh)};
                }
            }
            look = _shotLooks.emplace(shot.sprite, std::pair{texture, rect}).first;
        }
        if (!look->second.first || shot.target >= _scene.units().size())
            continue;
        // Along its way, turned to face the camera.
        const gfx::Vec3 to = _scene.units()[shot.target].middle();
        gfx::Vec3 d{to[0] - shot.at[0], to[1] - shot.at[1], to[2] - shot.at[2]};
        const float dl = std::max(1e-3f, std::hypot(d[0], d[1], d[2]));
        for (float &c : d)
            c /= dl;
        const gfx::Vec3 v{view.eye[0] - shot.at[0], view.eye[1] - shot.at[1], view.eye[2] - shot.at[2]};
        gfx::Vec3 side{d[1] * v[2] - d[2] * v[1], d[2] * v[0] - d[0] * v[2], d[0] * v[1] - d[1] * v[0]};
        const float sl = std::max(1e-3f, std::hypot(side[0], side[1], side[2]));
        for (float &c : side)
            c /= sl;
        gfx::DrawCall call;
        call.mesh = _shotMesh;
        call.indexCount = 6;
        call.model = gfx::identity();
        for (size_t k = 0; k < 3; ++k) {
            call.model[k] = d[k] * kLength;
            call.model[4 + k] = side[k] * kRadius;
            call.model[12 + k] = shot.at[k];
        }
        call.texture = look->second.first;
        // The sprite's rectangle of its texture: u0 + u (u1 - u0), and v so.
        const std::array<float, 4> &r = look->second.second;
        call.preRow0 = {r[2] - r[0], 0, r[0]};
        call.preRow1 = {0, r[3] - r[1], r[1]};
        call.state.cull = gfx::Cull::kNone;
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
        call.state.depthWrite = false;
        _renderer->draw(call);
    }
}

void MissionView::drawBillboards(const MapScene::Camera &view, double now) {
    if (!_renderer)
        return;
    std::set<size_t> on = _billboards;
    for (const auto &[from, to] : _transportTo)
        on.insert({from, to});
    if (on.empty())
        return;
    // Its texture, as a shot's; a 4 x 4 sheet of 32 frames a second, round
    // and round (weapon.spr's @anim=tex4x4: 16 over 0.5 seconds).
    auto look = _shotLooks.find("xtransport");
    if (look == _shotLooks.end()) {
        gfx::TextureId texture = 0;
        std::array<float, 4> rect{0, 0, 1, 1};
        if (const SpriteTable::Sprite *s = _hud.findSprite("xtransport")) {
            image::Image picture;
            std::vector<uint8_t> data;
            if (!_files.read("Textures/RGB/" + s->texture + ".tga", data) && !image::decode(data, picture) &&
                picture.w > 0) {
                gfx::TextureParams params;
                params.clamp = true;
                if (_renderer->createTexture(picture.w, picture.h, picture.rgba.data(), params, texture))
                    texture = 0;
                rect = {float(s->u / picture.w), float(s->v / picture.h), float((s->u + s->w) / picture.w),
                        float((s->v + s->h) / picture.h)};
            }
        }
        look = _shotLooks.emplace("xtransport", std::pair{texture, rect}).first;
    }
    makeShotMesh();
    if (!look->second.first || !_shotMesh)
        return;
    const int frame = int(std::fmod(now, 0.5) / 0.5 * 16) % 16;
    const std::array<float, 4> &cell = look->second.second;
    const float cw = cell[2] - cell[0];
    const float ch = cell[3] - cell[1];
    const float u0 = cell[0] + float(frame % 4) * cw;
    const float v0 = cell[1] + float(frame / 4) * ch;
    // The camera's right and up, for a square facing it.
    const gfx::Vec3 &f = view.forward;
    const gfx::Vec3 &up = view.up;
    gfx::Vec3 right{up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]};
    const float rl = std::max(1e-3f, std::hypot(right[0], right[1], right[2]));
    for (float &c : right)
        c /= rl;
    const std::vector<MapScene::Unit> &units = _scene.units();
    for (size_t unit : on) {
        if (unit >= units.size() || _dead[unit] || !_scene.shown(unit))
            continue;
        // Its radius each side, moved that far towards the camera (0x5829c0).
        const float r = units[unit].radius();
        gfx::Vec3 centre = units[unit].middle();
        gfx::Vec3 toEye{view.eye[0] - centre[0], view.eye[1] - centre[1], view.eye[2] - centre[2]};
        const float el = std::max(1e-3f, std::hypot(toEye[0], toEye[1], toEye[2]));
        for (size_t k = 0; k < 3; ++k)
            centre[k] += toEye[k] / el * r;
        gfx::DrawCall call;
        call.mesh = _shotMesh;
        call.indexCount = 6;
        call.model = gfx::identity();
        for (size_t k = 0; k < 3; ++k) {
            call.model[k] = right[k] * 2 * r;
            call.model[4 + k] = up[k] * r;
            call.model[12 + k] = centre[k] + right[k] * r;
        }
        call.texture = look->second.first;
        call.preRow0 = {cw, 0, u0};
        call.preRow1 = {0, ch, v0};
        call.state.cull = gfx::Cull::kNone;
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
        call.state.depthWrite = false;
        _renderer->draw(call);
    }
}

void MissionView::makeCells() {
    if (!_renderer || _cellMesh)
        return;
    // The program makes its cells' texture itself; these are like its
    // frames: a taken cell's outline in yellow; a station's square as a
    // thick frame broken in the middle of each side, green where it can go
    // and red where it can't; added on.
    constexpr int kTexels = 32;
    std::vector<uint8_t> taken(size_t(kTexels * kTexels) * 4, 0);
    std::vector<uint8_t> open(size_t(kTexels * kTexels) * 4, 0);
    std::vector<uint8_t> shut(size_t(kTexels * kTexels) * 4, 0);
    for (int y = 0; y < kTexels; ++y)
        for (int x = 0; x < kTexels; ++x) {
            const bool edge = x == 0 || y == 0 || x == kTexels - 1 || y == kTexels - 1;
            const int in = std::min(std::min(x, y), std::min(kTexels - 1 - x, kTexels - 1 - y));
            const bool middle = std::abs(x - kTexels / 2) < 2 || std::abs(y - kTexels / 2) < 2;
            const bool frame = in < 3 && !middle;
            uint8_t *t = &taken[size_t(y * kTexels + x) * 4];
            uint8_t *f = &open[size_t(y * kTexels + x) * 4];
            uint8_t *b = &shut[size_t(y * kTexels + x) * 4];
            if (edge)
                t[0] = 230, t[1] = 180, t[2] = 40;
            if (frame)
                f[1] = 230, b[0] = 230;
            t[3] = f[3] = b[3] = 255;
        }
    gfx::TextureParams params;
    params.clamp = true;
    if (_renderer->createTexture(kTexels, kTexels, taken.data(), params, _cellTaken))
        _cellTaken = 0;
    if (_renderer->createTexture(kTexels, kTexels, open.data(), params, _cellFree))
        _cellFree = 0;
    if (_renderer->createTexture(kTexels, kTexels, shut.data(), params, _cellBlocked))
        _cellBlocked = 0;
    // A cell: the unit square from its corner, on the plane.
    std::vector<gfx::Vertex> corners(4);
    const float xs[4] = {0, 1, 1, 0};
    const float zs[4] = {0, 0, 1, 1};
    for (size_t i = 0; i < 4; ++i) {
        gfx::Vertex &v = corners[i];
        v = gfx::Vertex{};
        v.pos[0] = xs[i], v.pos[2] = zs[i];
        v.normal[1] = 1;
        v.uv[0] = xs[i], v.uv[1] = zs[i];
        std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
    }
    if (_renderer->createMesh(corners, {0, 1, 2, 0, 2, 3}, _cellMesh))
        _cellMesh = 0;
    _shotMesh = 0;
    _shotLooks.clear();
}

void MissionView::drawPlacement(const MapScene::Camera &view) {
    if (!_placing || _selection.size() != 1 || size_t(_selection.front()) >= _ships.size() ||
        _hud.covers(_mouseX, _mouseY))
        return;
    const auto at = MapScene::pointOn(view, _ships[size_t(_selection.front())].at[1], _mouseX, _mouseY);
    if (!at)
        return;
    _scene.drawModelAt(*_placing, *at);
    makeCells();
    if (!_cellMesh || !_cellTaken || !_cellFree)
        return;
    const auto taken = takenCellsOf(_footprints);
    const auto site = siteCellsOf(*at, _placingFootprint);
    auto cell = [this](const std::pair<int, int> &c, int across, gfx::TextureId texture) {
        gfx::DrawCall d;
        d.mesh = _cellMesh;
        d.indexCount = 6;
        d.model = gfx::identity();
        d.model[0] = d.model[10] = 50.0f * float(across);
        d.model[12] = float(c.first) * 50, d.model[14] = float(c.second) * 50;
        d.texture = texture;
        d.state.cull = gfx::Cull::kNone;
        d.state.src = gfx::Blend::kOne;
        d.state.dst = gfx::Blend::kOne;
        d.state.depthWrite = false;
        _renderer->draw(d);
    };
    // On the plane y = 0, as the original's.
    for (const auto &c : taken)
        cell(c, 1, _cellTaken);
    // The station's own cells, one frame over them all: green where it can
    // go, red where it can't.
    const int across = int(std::lround(std::sqrt(double(site.size()))));
    cell(site.front(), across, placeable(*at, _placingFootprint) ? _cellFree : _cellBlocked);
}

void MissionView::stop() {
    if (_renderer) {
        if (_cellMesh)
            _renderer->destroyMesh(_cellMesh);
        if (_shotMesh)
            _renderer->destroyMesh(_shotMesh);
        for (const auto &[name, look] : _shotLooks)
            if (look.first)
                _renderer->destroyTexture(look.first);
        for (const gfx::TextureId t : {_cellTaken, _cellFree, _cellBlocked})
            if (t)
                _renderer->destroyTexture(t);
        if (_ringMesh)
            _renderer->destroyMesh(_ringMesh);
        if (_ringTexture)
            _renderer->destroyTexture(_ringTexture);
    }
    _ringMesh = 0;
    _ringTexture = 0;
    _cellMesh = 0;
    _cellTaken = _cellFree = _cellBlocked = 0;
    _selection.clear();
    _displayed.clear();
    _queues.clear();
    _building = false;
    _ordering = false;
    _placing.reset();
    _sites.clear();
    _footprints.clear();
    _moons.clear();
    _freighters.clear();
    _moves.clear();
    _physics.clear();
    _ships.clear();
    _vitals.clear();
    _classes.clear();
    _scans.clear();
    _computers.clear();
    _weapons.clear();
    _targets.clear();
    _chasing.clear();
    _specials.clear();
    _cloaked.clear();
    _cloakedWith.clear();
    _effects.clear();
    _fields.clear();
    _expiresAt.clear();
    _takenOver.clear();
    _spies.clear();
    _transporting = false;
    _transportTo.clear();
    _nextParty.clear();
    _parties.clear();
    _repairingAt.clear();
    _decommissioning.clear();
    _terrain = {};
    _missionScripts.reset();
    _scriptFault.clear();
    _paths.clear();
    _mapObjects.clear();
    _mapObjectOdfs.clear();
    _wormholes.clear();
    _waitingWormhole = -1;
    _boundFor.clear();
    _passages.clear();
    _scripted.clear();
    _picked.clear();
    _avoiding = true;
    _markers.clear();
    _flags.clear();
    _nextFlag = 1;
    _relations.clear();
    _perceived.clear();
    _hitAt.clear();
    _patrols.clear();
    _teamsOff.clear();
    _paused = false;
    _allyAttacks.clear();
    _explodedAt.clear();
    _conduits.clear();
    _alerts.clear();
    _billboards.clear();
    _credits.reset();
    _jumps.clear();
    _scriptPasses = 0;
    _cannotDie.clear();
    _missionEnd.reset();
    for (const auto &[id, voice] : _voices)
        if (_mixer && voice.handle != audio::kNoSound)
            _mixer->stop(voice.handle);
    _voices.clear();
    _nextVoice = 0;
    _overlayTexts.clear();
    _nextOverlayText = 0;
    _objectives.clear();
    _objectivesShown = false;
    stopMusic();
    stopBridgeMovie();
    _bridgeMovie.reset();
    _bridgeType = 0;
    _bridgeShown = _bridgeOnScreen = false;
    _tracks.clear();
    _lastTrack = -1;
    _jukeboxUpdates = 0;
    _cine = Cineractive{};
    _gridShown = true;
    _warpSpeeds.clear();
    _recrewing.clear();
    _poolCarry.clear();
    _revealedUntil.clear();
    _selfDestructAt.clear();
    _cineUnit = -1;
    _cineUntil = 0;
    _holds.clear();
    _pods.clear();
    _researched.clear();
    _podsAt.clear();
    _arming.clear();
    _mines.clear();
    _dead.clear();
    _shots.clear();
    _shieldHits.clear();
    _explosions.clear();
    _players.clear();
    _tallies.clear();
    _surrendered = false;
    _settledAt.reset();
    _menuWanted = false;
    _targeting = false;
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
    // The objectives box's OK puts it away (0x49a620); the rest of it
    // takes clicks.
    if (_objectivesShown && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft &&
        _hud.objectivesCover(ev.mouseX, ev.mouseY)) {
        if (_hud.objectivesOkAt(ev.mouseX, ev.mouseY))
            _objectivesShown = false;
        return;
    }
    // A cut scene takes the input: Space votes to skip it (Input.map's
    // switch_camera_shot, 0x4a49ca); Esc still opens the menu.
    if (_cine.on() && !(ev.type == EventType::kKeyDown && ev.key == key::kEscape)) {
        if (ev.type == EventType::kKeyDown && ev.key == key::kSpace)
            _cine.skip();
        return;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && _hud.covers(ev.mouseX, ev.mouseY)) {
        // On the minimap, the camera goes there; on the control panel, its
        // button.
        const BznMap &map = _scene.map();
        if (auto at = _hud.minimapPointAt(ev.mouseX, ev.mouseY, map.area[0], map.area[1], map.area[4], map.area[5])) {
            _overview.target[0] = (*at)[0];
            _overview.target[2] = (*at)[1];
            return;
        }
        if (_hud.cinematicButtonAt(ev.mouseX, ev.mouseY) == 2) {
            _menuWanted = true;
            return;
        }
        // The transport button: the next unit clicked is where crew go.
        if (_hud.transportAt(ev.mouseX, ev.mouseY) && !_selection.empty()) {
            _transporting = true;
            _targeting = true;
            return;
        }
        if (const int sp = _hud.specialAt(ev.mouseX, ev.mouseY); sp >= 0 && size_t(sp) < _orders.specials.size()) {
            useSpecial(_orders.specials[size_t(sp)], -1);
            return;
        }
        pressControl(_hud.controlAt(ev.mouseX, ev.mouseY));
        return;
    }
    if (_targeting && ev.type == EventType::kMouseDown && !_hud.covers(ev.mouseX, ev.mouseY)) {
        // The unit clicked on is what the selected units attack; the right
        // button gives up.
        _targeting = false;
        if (ev.button == MouseButton::kLeft) {
            const int t = liveUnitAt(MapScene::viewProjOf(camera(), kRange), ev.mouseX, ev.mouseY);
            const bool point = std::any_of(_selection.begin(), _selection.end(), [this](int i) {
                return size_t(i) < _specials.size() &&
                       std::any_of(
                           _specials[size_t(i)].begin(), _specials[size_t(i)].end(),
                           [this](const Combat::Special &sp) { return sp.button == _arming && wantsPoint(sp); });
            });
            if (_transporting) {
                if (t >= 0)
                    for (int i : obeying(_selection))
                        transport(size_t(i), size_t(t));
            } else if (!_arming.empty() && point && !_selection.empty() && size_t(_selection.front()) < _ships.size()) {
                if (auto at =
                        MapScene::pointOn(camera(), _ships[size_t(_selection.front())].at[1], ev.mouseX, ev.mouseY))
                    firePoint(_arming, *at);
            } else if (!_arming.empty())
                useSpecial(_arming, t);
            else
                attack(t);
        }
        _arming.clear();
        _transporting = false;
        return;
    }
    if (_placing && ev.type == EventType::kMouseDown && !_hud.covers(ev.mouseX, ev.mouseY)) {
        // Placed with the left button, given up with the right.
        if (ev.button == MouseButton::kLeft)
            place(ev.mouseX, ev.mouseY);
        else
            _placing.reset();
        return;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && !_hud.covers(ev.mouseX, ev.mouseY)) {
        _pressX = ev.mouseX;
        _pressY = ev.mouseY;
        return;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kRight && !_hud.covers(ev.mouseX, ev.mouseY)) {
        // On another player's unit, the selected units that can attack it
        // do *(guessed: the original's right-click interface, as its
        // settings name it; there was no enemy near to try)*.
        const int unit = liveUnitAt(MapScene::viewProjOf(camera(), kRange), ev.mouseX, ev.mouseY);
        if (unit >= 0 && _scene.units()[size_t(unit)].slot != _slot && attack(unit))
            return;
        // Sent to a wormhole, the ships go through it (0x425b7e).
        if (auto hole = unit >= 0 ? wormholeOfHandle(unit + 1) : std::nullopt) {
            for (int i : obeying(_selection))
                if (size_t(i) < _moves.size() && _moves[size_t(i)]) {
                    _targets[size_t(i)] = -1;
                    sendThrough(size_t(i), *hole);
                }
            return;
        }
        // Sent to the point under the pointer, on the plane the selected
        // units fly in; an order to go gives up their attack.
        for (int i : _selection)
            if (size_t(i) < _targets.size() && size_t(i) < _moves.size() && _moves[size_t(i)])
                _targets[size_t(i)] = -1;
        if (!_selection.empty() && size_t(_selection.front()) < _ships.size())
            if (auto point =
                    MapScene::pointOn(camera(), _ships[size_t(_selection.front())].at[1], ev.mouseX, ev.mouseY))
                sendTo(_ships, _scene.units(), obeying(_selection), _moves, *point, _terrain);
        return;
    }
    if (_pressX >= 0 && (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseUp)) {
        // Past a click's slop it's a box, and what it holds is selected as
        // it's dragged, as the original's frames show.
        if (std::abs(ev.mouseX - _pressX) + std::abs(ev.mouseY - _pressY) > kClickSlop)
            _dragging = true;
        const gfx::Mat4 viewProj = MapScene::viewProjOf(camera(), kRange);
        if (_dragging) {
            _selection = boxed(_scene.units(), _moves, viewProj, _slot, _pressX, _pressY, ev.mouseX, ev.mouseY);
            std::erase_if(_selection, [this](int i) { return size_t(i) < _dead.size() && _dead[size_t(i)]; });
        }
        if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
            if (!_dragging) {
                // A click: the unit under it, or none.
                const int unit = liveUnitAt(viewProj, ev.mouseX, ev.mouseY);
                _selection = unit < 0 ? std::vector<int>{} : std::vector<int>{unit};
            }
            _pressX = _pressY = -1;
            _dragging = false;
        }
        return;
    }
    if (ev.type != EventType::kKeyDown && ev.type != EventType::kKeyUp)
        return;
    // A, the attack shortcut (Input.map's attack): the next click on a unit
    // is the target of the selected units that can attack.
    if (ev.type == EventType::kKeyDown && ev.key == 'a' && ev.mods == 0) {
        for (int i : _selection)
            if (size_t(i) < _weapons.size() && !_weapons[size_t(i)].empty())
                _targeting = true;
        return;
    }
    // Shift+G, Shift+Y and Shift+R (Input.map's alerts): the selected
    // units' alert level.
    if (ev.type == EventType::kKeyDown && (ev.mods & keymod::kShift) && !(ev.mods & (keymod::kCtrl | keymod::kAlt)) &&
        (ev.key == 'g' || ev.key == 'y' || ev.key == 'r')) {
        const int level = ev.key == 'g' ? kGreenAlert : ev.key == 'y' ? kYellowAlert : kRedAlert;
        for (int i : obeying(_selection))
            if (size_t(i) < _scene.units().size() && _scene.units()[size_t(i)].slot == _slot)
                setAlert(size_t(i), level);
        return;
    }
    // Shift+D, with Control and Alt up (Input.map's decomm): the selected
    // units of the player's decommissioned.
    if (ev.type == EventType::kKeyDown && ev.key == 'd' && (ev.mods & keymod::kShift) &&
        !(ev.mods & (keymod::kCtrl | keymod::kAlt))) {
        for (int i : obeying(_selection))
            if (size_t(i) < _scene.units().size() && _scene.units()[size_t(i)].slot == _slot)
                decommission(size_t(i));
        return;
    }
    const int action = actionOf(ev.key);
    if (action < 0)
        return;
    if (ev.type == EventType::kKeyDown)
        _held.insert(action);
    else
        _held.erase(action);
}

bool MissionView::attack(int target) {
    if (target < 0 || size_t(target) >= _dead.size() || _dead[size_t(target)] ||
        _classes[size_t(target)] == "wormhole_obj")
        return false;
    bool any = false;
    for (int i : obeying(_selection))
        if (i != target && size_t(i) < _targets.size() && !_weapons[size_t(i)].empty()) {
            _targets[size_t(i)] = target;
            noteAttack(size_t(i), size_t(target));
            any = true;
        }
    return any;
}

std::optional<gfx::Vec3> MissionView::chasePointOf(const gfx::Vec3 &from, const gfx::Vec3 &to, float reach,
                                                   float height) {
    // The original's destroyer, its target flown from 186 to 300 away,
    // moved 93 towards it and fired from 246: just inside its reach of
    // 250; within reach it held where it was.
    constexpr float kInside = 0.95f;
    const float dx = to[0] - from[0];
    const float dz = to[2] - from[2];
    if (std::hypot(dx, to[1] - from[1], dz) <= reach)
        return std::nullopt;
    const float flat = std::max(1e-3f, std::hypot(dx, dz));
    const float keep = reach * kInside;
    return gfx::Vec3{to[0] - dx / flat * keep, height, to[2] - dz / flat * keep};
}

void MissionView::pursue() {
    // Out of its weapons' reach, a ship flies to the edge of it on the way
    // to its target, set afresh as the target moves; within it, it stops.
    const std::vector<MapScene::Unit> &units = _scene.units();
    for (size_t i = 0; i < units.size() && i < _targets.size() && i < _chasing.size() && i < _ships.size(); ++i) {
        const int t = _targets[i];
        Movement::Ship &ship = _ships[i];
        if (t < 0 || _dead[i] || !_physics[i] || _weapons[i].empty()) {
            if (_chasing[i])
                ship.goal.reset();
            _chasing[i] = false;
            continue;
        }
        float reach = 0;
        for (const Combat::Weapon &w : _weapons[i])
            reach = std::max(reach, w.range);
        auto point = chasePointOf(units[i].middle(), units[size_t(t)].middle(), reach, ship.at[1]);
        // A target it took up itself on the way somewhere it was sent, gone
        // out of reach, is let go, not chased *(guessed)*.
        auto picked = _picked.find(i);
        if (point && !_chasing[i] && (ship.goal || _boundFor.count(i) || inWormholeQueue(i)) &&
            picked != _picked.end() && picked->second == t) {
            _targets[i] = -1;
            _picked.erase(picked);
            continue;
        }
        if (point) {
            ship.goal = point;
            ship.route.clear();
            _chasing[i] = true;
        } else if (_chasing[i]) {
            ship.goal.reset();
            _chasing[i] = false;
        }
    }
}

bool MissionView::yardReady(const std::string &podTooltip) const {
    // The ship a pod refits, by the class its tooltip ends with ("- Sovereign").
    const std::string key = Combat::researchKeyOf(podTooltip);
    const size_t dash = key.rfind(" - ");
    if (dash == std::string::npos)
        return true;
    const std::string ship = key.substr(dash + 3);
    const Odf::Loader loader = loaderOf(_files.files());
    // The yards the player's builders can put up, and what each builds.
    std::set<std::string> yards;
    for (const std::string &o : ownedOdfs())
        for (const std::string &item : buildListOf(o, loader)) {
            Odf yard;
            if (!yard.load(item + ".odf", loader) && text::equalsIgnoreCase(yard.string("classLabel"), "shipyard"))
                yards.insert(text::toLower(item));
        }
    for (const std::string &yard : yards)
        for (const std::string &item : buildListOf(yard, loader)) {
            Odf unit;
            if (unit.load(item + ".odf", loader) || text::toLower(unit.string("unitName")).rfind(ship, 0) != 0)
                continue;
            // Its yard can be built now: buildMenuOf lights it.
            const Hud::Unit menu = buildMenuOf({yard}, loader, ownedOdfs(), _techTree);
            return !menu.controls.empty() && menu.controls.front().enabled;
        }
    return true;
}

bool MissionView::usable(size_t unit, const Combat::Special &special) const {
    const std::string &kind = special.kind;
    const bool cloak = kind == "cloakingdevice" || kind == "phasecloakdevice";
    if (unit >= _vitals.size() || _dead[unit] || special.ready > 0 || _selfDestructAt.count(unit))
        return false;
    // One on comes off for nothing: a cloak, a beam, regeneration.
    const bool on = (cloak && unit < _cloaked.size() && _cloaked[unit]) || _running.count(std::pair{unit, kind}) ||
                    (kind == "toggleweapon" &&
                     std::any_of(_holds.begin(), _holds.end(), [unit](const Hold &h) { return h.unit == unit; }));
    if (!on && _vitals[unit].energy < special.energy)
        return false;
    // One a pod gives wants it researched by the unit's player.
    if (_pods.count(special.research)) {
        auto it = _researched.find(_scene.units()[unit].slot);
        if (it == _researched.end() || !it->second.count(special.research))
            return false;
    }
    // Those made yet: mines, shots that do damage, the cloaks and their
    // detectors, self-destruct, regeneration, the holding beam, and the
    // missiles that drain energy or shields.
    const std::string &shot = special.ordnance;
    return (kind == "minelayer" && (shot == "antimine" || shot == "gravmine")) ||
           ((kind == "cannon" || kind == "launcher") && special.damage > 0) || cloak || kind == "detectcloak" ||
           kind == "selfdestruct" || kind == "regeneration" || (kind == "toggleweapon" && shot == "holding_bm") ||
           (kind == "areamissile" && (shot == "micro" || shot == "shieldh" || shot == "myotron")) || madeKind(special);
}

void MissionView::useSpecial(const std::string &button, int target) {
    for (int i : obeying(_selection)) {
        if (size_t(i) >= _specials.size() || _scene.units()[size_t(i)].slot != _slot)
            continue;
        for (Combat::Special &sp : _specials[size_t(i)]) {
            if (sp.button != button || !usable(size_t(i), sp))
                continue;
            // One that wants a target waits for the next click on a unit;
            // a transwarp, for a click on the map.
            if ((sp.needTarget && target < 0) || wantsPoint(sp)) {
                _arming = button;
                _targeting = true;
                continue;
            }
            fireSpecial(size_t(i), sp, target);
        }
    }
}

void MissionView::fireSpecial(size_t unit, Combat::Special &special, int target) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    const std::string &kind = special.kind;
    special.ready = special.delay;
    if (kind == "cloakingdevice" || kind == "phasecloakdevice") {
        // On for its cost, off for nothing; it can't fire while on. A phase
        // cloak drains its energyDrainRate a second besides (0x4f6260).
        if (!_cloaked[unit]) {
            _vitals[unit].energy -= special.energy;
            _cloakedWith[unit] = {_vitals[unit].shields, _vitals[unit].crew};
            if (kind == "phasecloakdevice")
                _running[std::pair{unit, kind}] = {special.value("energyDrainRate", 15), 0};
        } else {
            _running.erase(std::pair{unit, kind});
        }
        _cloaked[unit] = !_cloaked[unit];
        _scene.setCloaked(unit, _cloaked[unit]);
        return;
    }
    if (madeKind(special) || kind == "toggleweapon") {
        fireMore(unit, special, target);
        return;
    }
    _vitals[unit].energy -= special.energy;
    if (kind == "detectcloak") {
        // Shows the cloaked units of others within its sight, for its
        // detectTimer.
        for (size_t j = 0; j < units.size() && j < _cloaked.size(); ++j) {
            const gfx::Vec3 a = units[unit].middle();
            const gfx::Vec3 b = units[j].middle();
            if (_cloaked[j] && units[j].slot != units[unit].slot &&
                std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) <= _scans[unit])
                _revealedUntil[j] = now + special.timer;
        }
        return;
    }
    if (kind == "selfdestruct") {
        _selfDestructAt[unit] = now + special.timer;
        return;
    }
    if (kind == "regeneration") {
        // On while the ship pays its cost a second, off when pressed again
        // (RegenerationWeapon, 0x501bf0).
        const auto key = std::pair{unit, kind};
        if (!_running.erase(key))
            _running[key] = {special.energy, special.modifier};
        return;
    }
    if (kind == "areamissile") {
        // A missile at each of its area's targets: the Energy Dissipator
        // drains its specialEnergyDrainRate of special energy over its
        // specialEnergyDelay (0x4d9550); the Shield Disruptor takes the
        // shields and their generator for its specialValue, giving the
        // shields back after (0x4df810); the Myotron, the weapons (0x4d9fa0).
        for (size_t j : areaTargetsOf(unit, special)) {
            Combat::Vitals &v = _vitals[j];
            const float lasting = special.modifier > 0 ? special.modifier : 15;
            if (special.ordnance == "micro") {
                const float over = std::max(special.value("specialEnergyDelay", 10), 0.1f);
                _effects[j].energyDrain = special.value("specialEnergyDrainRate", 1000) / over;
                _effects[j].energyDrainUntil = now + over;
            } else if (special.ordnance == "shieldh") {
                Combat::disable(v, Combat::kShieldGenerator, lasting, true);
            } else if (special.ordnance == "myotron") {
                Combat::disable(v, Combat::kWeapons, lasting);
            }
        }
        return;
    }
    if (special.kind == "minelayer") {
        // Left where the ship is, to wait for a ship of another's.
        _mines.push_back({units[unit].middle(), units[unit].slot, special, special.life, 0});
        return;
    }
    if (target < 0 || size_t(target) >= units.size() || _dead[size_t(target)])
        return;
    _roll = _roll * 1664525u + 1013904223u;
    const float roll = float(_roll >> 8) / float(1u << 24);
    Combat::Weapon as;
    as.damage = special.damage;
    as.variance = special.variance;
    const gfx::Vec3 from = units[unit].middle();
    _shots.push_back(
        {size_t(target), from, special.speed, Combat::damageOf(as, roll), special.sprite, from, units[unit].slot});
}

void MissionView::moveMines(double seconds) {
    // Each goes for the nearest ship of another's within its sight, at its
    // speed, and hits it within that ship's reach; spent at its life's end.
    const std::vector<MapScene::Unit> &units = _scene.units();
    std::vector<std::pair<size_t, int>> killed;
    for (size_t m = 0; m < _mines.size();) {
        Mine &mine = _mines[m];
        mine.left -= seconds;
        int prey = -1;
        float best = mine.special.detect;
        for (size_t j = 0; j < units.size() && j < _dead.size(); ++j) {
            if (_dead[j] || units[j].slot == mine.slot)
                continue;
            const gfx::Vec3 c = units[j].middle();
            const float d = std::hypot(c[0] - mine.at[0], c[1] - mine.at[1], c[2] - mine.at[2]);
            if (d <= best)
                prey = int(j), best = d;
        }
        bool spent = mine.left <= 0;
        if (prey >= 0) {
            const gfx::Vec3 c = units[size_t(prey)].middle();
            // Up to its speed at its shotAccel, from still.
            mine.speed = mine.special.accel > 0
                             ? std::min(mine.special.speed, mine.speed + float(mine.special.accel * seconds))
                             : mine.special.speed;
            const float step = float(mine.speed * seconds);
            if (best <= std::max(step, units[size_t(prey)].radius() * 0.5f)) {
                _roll = _roll * 1664525u + 1013904223u;
                Combat::Weapon as;
                as.damage = mine.special.damage;
                as.variance = mine.special.variance;
                // A Gravity Mine does no damage but weighs its ship down to
                // its slowPercent (0x4e9a70) *(guessed: for 20 seconds)*.
                if (mine.special.ordnance == "gravmine") {
                    _effects[size_t(prey)].slowed = double(_last) / 1000 + 20;
                    _effects[size_t(prey)].slowBy = mine.special.value("slowPercent", 10) / 100;
                } else {
                    const float damage = Combat::damageOf(as, float(_roll >> 8) / float(1u << 24));
                    _roll = _roll * 1664525u + 1013904223u;
                    Combat::hit(_vitals[size_t(prey)], damage, mine.special.crew, float(_roll >> 8) / float(1u << 24));
                    if (!_vitals[size_t(prey)].alive())
                        killed.emplace_back(size_t(prey), mine.slot);
                }
                spent = true;
            } else {
                for (size_t k = 0; k < 3; ++k)
                    mine.at[k] += (c[k] - mine.at[k]) / best * step;
            }
        }
        if (spent)
            _mines.erase(_mines.begin() + std::ptrdiff_t(m));
        else
            ++m;
    }
    for (const auto &[k, by] : killed)
        if (!_dead[k])
            destroy(k, by);
}

bool MissionView::settled(const std::vector<Player> &players, const std::set<int> &standing, bool alliedVictory) {
    // Every two players with a unit allied both ways, each with Allied
    // Victory on; a player's team is who they're allied with.
    std::optional<int> team;
    int left = 0;
    for (const Player &p : players) {
        if (!standing.count(p.slot))
            continue;
        ++left;
        if (team && *team != p.team)
            return false;
        team = p.team;
    }
    return left <= 1 || alliedVictory;
}

MissionView::Ending MissionView::ending() const {
    if (_players.empty())
        return Ending::kNone;
    // A campaign mission ends when its script says, at the game time it
    // gives (0x401c00).
    if (_missionScripts) {
        if (_missionEnd && _missionScripts->clock() >= _missionEnd->first)
            return _missionEnd->second ? Ending::kLost : Ending::kWon;
        return Ending::kNone;
    }
    if (_surrendered)
        return Ending::kLost;
    // The session ends 3 seconds after (0x5bdbf4); the original's debriefing
    // came within 4 seconds of the player's last unit going.
    constexpr double kSettledDelay = 3;
    if (!_settledAt || double(_last) / 1000 < *_settledAt + kSettledDelay)
        return Ending::kNone;
    for (size_t i = 0; i < _scene.units().size() && i < _dead.size(); ++i)
        if (_scene.units()[i].slot == _slot && !_dead[i])
            return Ending::kWon;
    return Ending::kLost;
}

void MissionView::drawScreen(const Surface &screen, uint64_t millis) {
    if (!_renderer)
        return;
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, kClear);
    _renderer->endFrame(&screen);
    _last = millis;
}

void MissionView::draw(uint64_t millis) {
    if (!_renderer)
        return;
    double seconds = double(millis - _last) / 1000;
    _overview = moved(_overview, _held, seconds);
    _last = millis;
    // A campaign's objectives box holds the world still while it's up
    // (0x43dad4); the camera and interface run on.
    if (_objectivesShown && _missionScripts)
        seconds = 0;
    // Each player's crew, by the starbases it has standing.
    auto starbasesOf = [this](int slot) {
        int n = 0;
        for (size_t i = 0; i < _scene.units().size() && i < _dead.size(); ++i)
            n += _scene.units()[i].slot == slot && !_dead[i] && _classes[i] == "starbase";
        return n;
    };
    if (_resources)
        _resources->crew = grownCrew(_resources->crew, _crewCarry, starbasesOf(_slot), seconds);
    for (Computer &c : _computers)
        c.bank.crew = grownCrew(c.bank.crew, c.crewCarry, starbasesOf(c.slot), seconds);
    runScripts(seconds);
    // Paused by a script, the world holds still; the scripts and the camera run on.
    if (_paused)
        seconds = 0;
    runMusic();
    think(seconds);
    build(seconds);
    engage();
    pursue();
    fight(seconds);
    runWormholes(seconds);
    runMarkers(seconds);
    runTranswarps(seconds);
    runAlerts();
    runPatrols();
    // The ships fly on, in steps of no more than a twentieth of a second.
    const int steps = std::max(1, int(std::ceil(seconds / 0.05)));
    // What each ship keeps clear of: every other unit standing, as it moves.
    std::vector<Movement::Neighbour> everyone;
    for (size_t j = 0; j < _ships.size() && j < _scene.units().size(); ++j) {
        const Movement::Ship &o = _ships[j];
        const bool moving = j < _physics.size() && _physics[j].has_value();
        everyone.push_back(
            {_scene.units()[j].middle(),
             moving ? gfx::Vec3{std::sin(o.heading) * o.speed, 0, std::cos(o.heading) * o.speed} : gfx::Vec3{},
             _dead[j] ? 0.0f : ringHalfOf(_scene.units()[j])});
    }
    const BznMap &area = _scene.map();
    // RTS_CFG.h's MAP_EDGE_BUFFER: ships kept 100 inside the map.
    auto inside = [&area](gfx::Vec3 &p) {
        constexpr float kEdge = 100;
        if (area.area[1] - area.area[0] > 2 * kEdge && area.area[5] - area.area[4] > 2 * kEdge) {
            p[0] = std::clamp(p[0], area.area[0] + kEdge, area.area[1] - kEdge);
            p[2] = std::clamp(p[2], area.area[4] + kEdge, area.area[5] - kEdge);
        }
    };
    for (size_t i = 0; i < _ships.size(); ++i) {
        if (!_physics[i] || (!_ships[i].goal && _ships[i].speed == 0))
            continue;
        if (_ships[i].goal)
            inside(*_ships[i].goal);
        // Its engines out, or held by a beam, it stops where it is.
        const bool held = std::any_of(_holds.begin(), _holds.end(), [i](const Hold &h) { return h.target == i; });
        const bool stopped =
            disabled(i) || (i < _effects.size() && (active(_effects[i].enginesOff) || active(_effects[i].frozen)));
        if (held || stopped || (i < _vitals.size() && !_vitals[i].systems[Combat::kEngines].working())) {
            _ships[i].speed = 0;
            continue;
        }
        // A Gravity Mine's weight: its slowPercent of its speed.
        const double pace = i < _effects.size() && active(_effects[i].slowed) ? _effects[i].slowBy : 1.0;
        for (int s = 0; s < steps; ++s)
            Movement::step(_ships[i], *_physics[i], seconds * pace / steps);
        // Steered clear of the others it'd come too near, at its speed, or
        // a crawl if it's slowing to a stop *(guessed)*; climbing or diving
        // by Y_REPULSION's part, then back to the plane its goals are on,
        // as fast as a fifth of its pace *(guessed)*.
        std::vector<Movement::Neighbour> others;
        for (size_t j = 0; j < everyone.size(); ++j)
            if (j != i && everyone[j].radius > 0)
                others.push_back(everyone[j]);
        // None while a script has turned steering round others off
        // (ObstacleAvoidence, 0x42d592), as through its cut scenes.
        const gfx::Vec3 push =
            _avoiding ? Movement::avoid(_ships[i], _scene.units()[i].middle(), ringHalfOf(_scene.units()[i]), others)
                      : gfx::Vec3{};
        const float pace2 = float(std::max(std::abs(_ships[i].speed), 10.0f) * seconds);
        _ships[i].at[0] += push[0] * pace2;
        _ships[i].at[2] += push[2] * pace2;
        _ships[i].at[1] = Movement::climb(_ships[i].at[1], push[1] * pace2, pace2 * 0.2f);
        inside(_ships[i].at);
        if (!_ships[i].goal && std::abs(_ships[i].speed) < 0.01f)
            _ships[i].speed = 0;
        _scene.moveUnit(i, _ships[i].at, _ships[i].heading);
    }
    // What the player's units see now, after they've moved.
    std::vector<MapScene::Sight> sights;
    for (size_t i = 0; i < _scene.units().size() && i < _dead.size() && i < _scans.size(); ++i)
        if (_scene.units()[i].slot == _slot && !_dead[i]) {
            // Its sensors out or jammed, it sees as its damagedScan says *(guessed: 100)*.
            const bool blind = (i < _effects.size() && active(_effects[i].sensorsOff)) ||
                               (i < _vitals.size() && !_vitals[i].systems[Combat::kSensors].working());
            sights.push_back({_scene.units()[i].at, blind ? std::min(_scans[i], 100.0f) : _scans[i]});
        }
    for (const Spy &spy : _spies)
        if (spy.slot == _slot && spy.target < _scene.units().size() && !_dead[spy.target])
            sights.push_back({_scene.units()[spy.target].at, 400});
    _scene.see(sights);
    std::erase_if(_selection, [this](int i) { return i < 0 || !_scene.shown(size_t(i)); });
    const MapScene::Camera view = camera();
    if (_cine.on() && _bridgeShown) {
        drawBridge(millis, view);
        return;
    }
    _scene.draw(view, double(millis) / 1000, kRange, kClear, _gridShown);
    const gfx::Mat4 viewProj = MapScene::viewProjOf(view, kRange);
    const std::vector<MapScene::Unit> &units = _scene.units();
    _hover = _hud.covers(_mouseX, _mouseY) ? -1 : liveUnitAt(viewProj, _mouseX, _mouseY);
    // The pointer resting on a player's units, the same player's frame
    // after frame, builds up a second's worth (0x496347).
    const int over = _hover >= 0 ? units[size_t(_hover)].slot : -1;
    _barLevel = over >= 0 && over == _barPlayer ? std::min(1.0, _barLevel + seconds) : 0;
    _barPlayer = over;
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
    drawShots(view);
    drawBillboards(view, double(millis) / 1000);
    drawEffects(double(millis) / 1000);
    drawPlacement(view);
    // A cut scene hides the interface (input mode 2): its fade alone over
    // the picture (0x495850).
    if (_cine.on()) {
        drawCutScene();
        drawCredits(millis);
        _renderer->endFrame(&_overlay);
        return;
    }
    // The cinematic view: the unit selected first; with none, as the
    // original's did, one of the player's units after another, each for 2
    // to 6 seconds, starting on its first ship *(inferred)*, from a run's
    // frames.
    int shown = _selection.empty() ? -1 : _selection.front();
    if (shown < 0) {
        const double now = double(millis) / 1000;
        const bool gone = _cineUnit < 0 || size_t(_cineUnit) >= units.size() || _dead[size_t(_cineUnit)] ||
                          units[size_t(_cineUnit)].slot != _slot;
        if (gone || now >= _cineUntil) {
            std::vector<int> mine;
            for (size_t i = 0; i < units.size() && i < _dead.size(); ++i)
                if (units[i].slot == _slot && !_dead[i])
                    mine.push_back(int(i));
            auto roll = [this] {
                _cineRoll = _cineRoll * 1664525u + 1013904223u;
                return double(_cineRoll >> 8) / double(1u << 24);
            };
            if (_cineUnit < 0) {
                for (size_t i = 0; _cineUnit < 0 && i < units.size(); ++i)
                    if (i < _moves.size() && _moves[i] && units[i].slot == _slot && !_dead[i])
                        _cineUnit = int(i);
            } else if (!mine.empty()) {
                _cineUnit = mine[std::min(mine.size() - 1, size_t(roll() * double(mine.size())))];
            }
            _cineUntil = now + 2 + 4 * roll();
        }
        shown = _cineUnit;
    }
    _hud.setCinematicShown(shown >= 0);
    if (shown >= 0) {
        const std::array<int, 4> area = _hud.cinematicArea();
        _scene.drawInset(closeUpOf(units[size_t(shown)]), double(millis) / 1000, kRange, area[0], area[1], area[2],
                         area[3]);
    }
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    drawBridgeMovie(millis, false);
    // Bars as the original draws them (0x4977a6): the unit under the
    // pointer's, and once the pointer has rested half a second on a
    // player's units, all of theirs, fading in over the next half second
    // (drawn whole here); not the selection's as such. The bar is the
    // shields'.
    std::set<int> barred;
    if (_hover >= 0)
        barred.insert(_hover);
    if (_barLevel > 0.5)
        for (size_t i = 0; i < units.size(); ++i)
            if (units[i].slot == _barPlayer)
                barred.insert(int(i));
    for (int b : barred) {
        if (b < 0 || size_t(b) >= units.size() || _dead[size_t(b)] || !_scene.shown(size_t(b)))
            continue;
        const std::array<int, 3> bar = healthBarOf(units[size_t(b)], viewProj);
        const Combat::Vitals &v = _vitals[size_t(b)];
        if (bar[2] > 0)
            _hud.drawHealthBar(_overlay, bar[0], bar[1], bar[2], v.maxShields > 0 ? v.shields / v.maxShields : 1);
    }
    if (_dragging)
        Hud::drawBox(_overlay, _pressX, _pressY, _mouseX, _mouseY);
    Hud::Minimap minimap = minimapOf(_scene.map(), units, _moves, view, _overview.target[1]);
    for (const Marker &m : _markers)
        minimap.rings.push_back({m.x, m.z, m.now});
    // What the cover hides isn't on the minimap either: the original's
    // showed neither the computer's units nor its moon's mark under the fog.
    for (size_t i = minimap.marks.size(); i-- > 0;)
        if ((i < _dead.size() && _dead[i]) || !_scene.shown(i))
            minimap.marks.erase(minimap.marks.begin() + std::ptrdiff_t(i));
    for (const Hud::Minimap::Icon &icon : _mapIcons)
        if (icon.size > 0 || !_scene.covered({icon.x, 0, icon.z}))
            minimap.icons.push_back(icon);
    _hud.draw(_overlay, _resources.value_or(Hud::Resources{}),
              _scene.cover().shroud ? _scene.seen() : std::vector<bool>{},
              _scene.cover().fog ? _scene.seenNow() : std::vector<bool>{}, _resources.has_value(), minimap);
    if (_displayed != _selection) {
        _display.clear();
        // The game picks a name at random; any one of them will do.
        for (int i : _selection)
            _display.push_back(displayOf(units[size_t(i)].odf, loaderOf(_files.files()), _hud, size_t(i)));
        _orders = ordersOf(_display);
        _displayed = _selection;
        _building = false;
        _ordering = false;
        _placing.reset();
    }
    // Their shields, as they are now.
    for (size_t k = 0; k < _display.size() && k < _selection.size(); ++k) {
        const size_t i = size_t(_selection[k]);
        if (i < _vitals.size() && _vitals[i].maxShields > 0)
            _display[k].shields = _vitals[i].shields / _vitals[i].maxShields;
        if (i < _vitals.size()) {
            const Combat::Vitals &v = _vitals[i];
            _display[k].energy = v.maxEnergy > 0 ? v.energy / v.maxEnergy : 0;
            if (v.maxCrew > 0)
                _display[k].crew = int(std::lround(v.crew));
            for (size_t n = 0; n < v.systems.size(); ++n)
                _display[k].systemHealth[n] =
                    v.systems[n].maxHitPoints > 0 ? v.systems[n].hitPoints / v.systems[n].maxHitPoints : 1;
        }
    }
    Hud::Unit orders = _orders;
    // A special weapon's button lit while any unit selected could fire it.
    orders.specialsReady.assign(orders.specials.size(), false);
    for (size_t b = 0; b < orders.specials.size(); ++b)
        for (int i : _selection)
            if (size_t(i) < _specials.size())
                for (const Combat::Special &sp : _specials[size_t(i)])
                    if (sp.button == orders.specials[b] && usable(size_t(i), sp))
                        orders.specialsReady[b] = true;
    if (!_display.empty())
        orders.energy = _display.front().energy;
    // Repair and Recrew lit for one ship short of hull, systems or crew,
    // with a shipyard or starbase to go to: dark on the original's whole
    // construction ship *(inferred)*.
    if (_selection.size() == 1 && size_t(_selection.front()) < _vitals.size()) {
        const size_t one = size_t(_selection.front());
        const Combat::Vitals &v = _vitals[one];
        const bool needs = v.shields < v.maxShields || v.crew < v.maxCrew || v.damaged();
        for (Hud::Unit::Button &b : orders.controls)
            if (b.sprite == "b_repair")
                b.enabled = needs && repairStationFor(one).has_value();
    }
    if (_ordering && _selection.size() == 1) {
        const size_t one = size_t(_selection.front());
        orders.controls = orderPanelOf(one < _moves.size() && _moves[one]);
        // The level it's at, lit as one being built is *(guessed: the
        // original's is drawn pressed)*.
        const int alert = alertOf(one);
        for (Hud::Unit::Button &b : orders.controls)
            b.active = b.slot == (alert == kGreenAlert ? 1 : alert == kYellowAlert ? 2 : 3);
    }
    if (_building) {
        orders.controls = _buildMenu.controls;
        // The item being placed in orange; all dark while the ship builds,
        // with cancel lit.
        bool busy = false;
        for (const Site &site : _sites)
            busy = busy || (_selection.size() == 1 && site.builder == size_t(_selection.front()));
        for (Hud::Unit::Button &b : orders.controls) {
            b.placing = b.active = _placing && b.sprite == "b_" + text::toLower(*_placing);
            if (busy && b.slot != 12)
                b.enabled = false;
        }
        if (busy)
            orders.controls.push_back({11, "b_cancel", "bb_standard", true});
    }
    // What the one unit selected is building: on the rail, its button in
    // orange, its construction bar.
    if (_selection.size() == 1 && !_display.empty()) {
        const auto queue = _queues.find(size_t(_selection.front()));
        _display.front().construction = 0;
        for (const Site &site : _sites)
            if (site.started && site.builder == size_t(_selection.front()) && site.order.time > 0)
                _display.front().construction = float(site.order.elapsed / site.order.time);
        if (queue != _queues.end() && !queue->second.empty()) {
            const Order &first = queue->second.front();
            const float progress = first.time > 0 ? float(first.elapsed / first.time) : 1;
            _display.front().construction = progress;
            orders.progress = progress;
            for (const Order &o : queue->second)
                orders.queue.push_back("b_" + text::toLower(o.item));
            for (Hud::Unit::Button &b : orders.controls)
                b.active = _building && b.sprite == "b_" + text::toLower(first.item);
            if (_building)
                orders.controls.push_back({11, "b_cancel", "bb_standard", true});
        }
    }
    _hud.drawSelection(_overlay, _display, orders);
    // Over a unit, the select cursor; over the map with ships selected,
    // the move cursor (the original's, with a construction ship).
    bool sending = false;
    for (int i : _selection)
        sending = sending || (size_t(i) < _moves.size() && _moves[size_t(i)]);
    const bool onMap = !_hud.covers(_mouseX, _mouseY);
    const char *cursor = _transporting && onMap ? "c_transport"
                         : _targeting && onMap  ? "c_attack"
                         : _hover >= 0          ? "c_select"
                         : sending && onMap     ? "c_move"
                                                : "c_arrow";
    if (_objectivesShown)
        _hud.drawObjectives(_overlay, _objectives);
    _hud.drawCursor(_overlay, _mouseX, _mouseY, double(millis) / 1000, cursor);
    drawCredits(millis);
    _renderer->endFrame(&_overlay);
}

void MissionView::drawBridge(uint64_t millis, const MapScene::Camera &view) {
    // The world isn't drawn whole while the bridge is up (0x5ac97a): only
    // into its view screen, unless that shows the movie.
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, kClear);
    const std::array<int, 4> screen = bridgeScreen();
    if (!_bridgeOnScreen)
        _scene.drawInset(view, double(millis) / 1000, kRange, screen[0], screen[1], screen[2], screen[3]);
    // The movie, by its clock, counted through its plays; its frame under
    // the picture, showing through the screen.
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    // Its frame under the picture, showing through the screen; the last
    // one held there once it's done.
    drawBridgeMovie(millis, _bridgeOnScreen);
    _hud.drawBridge(_overlay, bridgePrefix(), double(millis) / 1000 - _bridgeLightsFrom);
    // The titles and fade over the bridge (priorities 9 and 1).
    drawCutScene(false);
    drawCredits(millis);
    _renderer->endFrame(&_overlay);
}

void MissionView::drawBridgeMovie(uint64_t millis, bool held) {
    // By its clock, counted through its plays, closed at the end of its
    // last (0x47a410); drawn wherever it is while it's open, in the
    // rectangle it was given, even over play once its cut scene is
    // skipped without stopping it.
    if (!_bridgeMovie)
        return;
    if (_bridgeMoviePlaying) {
        _bridgeMovie->update(millis);
        if (_bridgeMoviePasses > 0 && _bridgeMovie->shown() >= _bridgeMovie->frames() * _bridgeMoviePasses)
            stopBridgeMovie();
    }
    if ((!_bridgeMoviePlaying && !held) || _bridgeMovie->rgba().empty())
        return;
    const std::array<int, 4> &r = _bridgeMovieRect;
    const int w = _bridgeMovie->width();
    const int h = _bridgeMovie->height();
    for (int y = 0; y < r[3]; ++y)
        for (int x = 0; x < r[2]; ++x) {
            const int px = r[0] + x;
            const int py = r[1] + y;
            if (px < 0 || py < 0 || px >= _overlay.w || py >= _overlay.h)
                continue;
            const uint8_t *from = &_bridgeMovie->rgba()[(size_t(y * h / r[3]) * size_t(w) + size_t(x * w / r[2])) * 4];
            uint8_t *to = _overlay.row(py) + size_t(px) * 4;
            std::copy(from, from + 3, to);
            to[3] = 255;
        }
}

} // namespace reluva::storm3d
