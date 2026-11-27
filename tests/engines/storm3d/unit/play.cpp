// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: a mission in play, its overview camera moved by
// the keys as the camera in the game's memory moved (camera.md).

#include "engines/storm3d/formats/gui_config.h"
#include "engines/storm3d/formats/tech_tree.h"
#include "engines/storm3d/play/combat.h"
#include "engines/storm3d/play/computer_player.h"
#include "engines/storm3d/play/hud.h"
#include "engines/storm3d/play/mission_view.h"
#include "engines/storm3d/play/movement.h"
#include "engines/storm3d/play/strategy.h"

#include "tests/support/test.h"

#include <cmath>
#include <array>
#include <map>
#include <set>
#include <string>

using reluva::storm3d::BznMap;
using reluva::storm3d::GuiConfig;
using reluva::storm3d::Hud;
using reluva::storm3d::MissionView;
namespace key = reluva::key;

namespace {

void testKeys() {
    CHECK(MissionView::actionOf(key::kKpPlus) == MissionView::kZoomIn);
    CHECK(MissionView::actionOf(key::kKpMultiply) == MissionView::kTurnRight);
    CHECK(MissionView::actionOf(key::kRight) == MissionView::kScrollRight);
    CHECK(MissionView::actionOf('a') == -1);
}

// The game's memory: + held 0.6 s took the height from 450 to 333.4; −
// stops at 700; * turned it about 17 degrees the smaller way; an arrow
// moves the target along the camera's right, (cos a, −sin a).
void testMoves() {
    MissionView::Overview o;
    o.target = {5751, 39.9f, 1042.8f};
    const MissionView::Overview nearer = MissionView::moved(o, {MissionView::kZoomIn}, 0.6);
    CHECK(std::abs(nearer.height - 333.4f) < 0.1f);
    CHECK(MissionView::moved(nearer, {MissionView::kZoomOut}, 2).height == 700);
    CHECK(MissionView::moved(o, {MissionView::kZoomIn}, 100).height == 10);
    const MissionView::Overview turned = MissionView::moved(nearer, {MissionView::kTurnRight}, 0.6);
    CHECK(std::abs(turned.turn * 180 / 3.14159265f + 17.2f) < 0.2f);
    // Six times the distance behind the target a second, about the 908
    // the game moved in 0.6 s at 333.4 high.
    const MissionView::Overview right = MissionView::moved(turned, {MissionView::kScrollRight}, 0.6);
    const float dx = right.target[0] - turned.target[0];
    const float dz = right.target[2] - turned.target[2];
    CHECK(std::abs(std::sqrt(dx * dx + dz * dz) - 929.5f) < 1);
    CHECK(std::abs(dx / std::sqrt(dx * dx + dz * dz) - std::cos(turned.turn)) < 1e-4f);
    CHECK(dz * std::sin(turned.turn) < 0);
    // Up and down along the way it faces; nothing held, nothing moves.
    const MissionView::Overview up = MissionView::moved(o, {MissionView::kScrollUp}, 1);
    CHECK(up.target[0] == o.target[0] && up.target[2] > o.target[2]);
    const MissionView::Overview still = MissionView::moved(o, {}, 5);
    CHECK(still.height == o.height && still.target == o.target && still.turn == o.turn);
}

// Over the player's Camera_<slot>; the area's middle without one.
void testStart() {
    BznMap map;
    map.area = {0, 6400, 0, 0, 0, 6400};
    BznMap::Path path;
    path.name = "Camera_1";
    path.points = {{5751, 39.9f, 1042.8f}};
    map.paths.push_back(path);
    const MissionView::Overview o = MissionView::startOf(map, 1);
    CHECK(o.target[0] == 5751 && o.target[2] == 1042.8f && o.height == 450 && o.turn == 0);
    CHECK(MissionView::startOf(map, 2).target[0] == 3200);
}

// The interface's layout, as gui_fed.cfg writes it: tabs between, quoted
// names, comments, and the file it includes read in its place.
void testLayout() {
    const std::map<std::string, std::string> files = {
        {"misc/gui_fed.cfg", "// FEDERATION INTERFACE\r\n#include \"gui_glob.cfg\"\r\n"
                             "speedPanelArea\t\t\t=\t\t\t146\t327\t247\t40\r\n"
                             "speedBackgroundPanel\t=\t\"speed_panel\"\t// the rail\r\n"
                             "infoSingleEnergyBarColor\t=\t1       1     0\r\n"},
        {"misc/gui_glob.cfg", "objectivesTextColor\t\t=\t1.0\t0.47\t0.0\r\nspeedPanelArea = 1 2 3 4\r\n"},
    };
    GuiConfig layout;
    CHECK(!layout.load("misc/gui_fed.cfg", [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("missing");
        text = it->second;
        return reluva::Error::success();
    }));
    CHECK((layout.area("speedPanelArea") == std::array<int, 4>{146, 327, 247, 40}));
    CHECK(layout.text("speedBackgroundPanel") == "speed_panel");
    CHECK(layout.number("objectivesTextColor", 1) == 0.47);
    CHECK(layout.number("infoSingleEnergyBarColor", 2, 9) == 0);
    CHECK(layout.number("nothing", 0, 7) == 7 && !layout.find("nothing"));
}

// The counters as the original's first frame of play says them: two
// construction ships (officerCost 2) and a starbase (officerGain 20) on
// 3000, 3500 and 100 officers make "4/120".
void testCounters() {
    Hud::Resources r{3000, 3500, 4, 120};
    CHECK(Hud::counterText(r, 0) == "3000" && Hud::counterText(r, 1) == "3500" && Hud::counterText(r, 2) == "4/120");
    CHECK(Hud::panels().size() == 8);
    CHECK(MissionView::hudRaceOf("kling") == "kli" && MissionView::hudRaceOf("federation") == "fed" &&
          MissionView::hudRaceOf("dominion") == "fed");
}

// The starbase a skirmish on "The Very First Map" starts the first player
// with, seen from the overview's start: its bar spans its box on the
// screen, 381 to 490 with its top at 104, as the original's draw; its ring
// is as wide as its box's diagonal across (52.6 each side, placed back);
// a click on it picks it, one beside it nothing.
void testSelection() {
    reluva::storm3d::MapScene::Unit base;
    base.odf = "fbase";
    base.at = {677.6f, 38.5f, 502.8f};
    const float lo[3] = {-38.106f, -29.02f, -35.862f};
    const float hi[3] = {37.195f, 63.333f, 37.323f};
    for (size_t k = 0; k < 3; ++k) {
        base.lo[k] = base.at[k] + lo[k];
        base.hi[k] = base.at[k] + hi[k];
    }
    const auto camera = reluva::storm3d::MapScene::overviewOf({576.6f, 38.5f, 444.5f});
    const auto viewProj = reluva::storm3d::MapScene::viewProjOf(camera, 1000);
    const std::array<int, 3> bar = MissionView::healthBarOf(base, viewProj);
    CHECK(std::abs(bar[0] - 381) <= 1 && std::abs(bar[1] - 104) <= 1 && std::abs(bar[2] - 109) <= 2);
    CHECK(std::abs(MissionView::ringHalfOf(base) - 52.6f) < 0.3f);
    CHECK(MissionView::ringMiddleOf(base)[0] == base.at[0] && MissionView::ringMiddleOf(base)[2] == base.at[2]);
    const std::vector<reluva::storm3d::MapScene::Unit> units = {base};
    CHECK(MissionView::unitAt(units, viewProj, camera, 430, 185) == 0);
    CHECK(MissionView::unitAt(units, viewProj, camera, 600, 300) == -1);
}

// The display's facts from a definition, as the starbase and the
// construction ship show them.
void testDisplay() {
    const std::map<std::string, std::string> files = {
        {"fbase.odf", "[StarbaseClass]\nunitName = \"Starbase\"\nrace = \"federation\"\nofficerCost = 0\n"
                      "crewCost = 800\nmaximumCrew = 1500\nenginesHitPoints = 0\nenginesCrewLoss = 0.0f\n"
                      "lifeSupportCrewLoss = 10.0f\nweaponsCrewLoss = 5.0f\nshieldGeneratorCrewLoss = 15.0f\n"
                      "sensorsCrewLoss = 5.0f\npossibleCraftNames = \"Starbase 612\" \"Starbase 473\"\n"
                      "weapon1 = \"fbpulse\"\nbuildItem0 = \"fconst\"\nclassLabel = \"starbase\"\n"},
        {"fconst.odf", "[CraftClass]\nunitName = \"Construction Ship\"\nrace = \"federation\"\n"
                       "officerCost = 2\ncrewCost = 500\nenginesHitPoints = 10\nenginesCrewLoss = 5.0f\n"
                       "buildItem0 = \"fbase\"\nclassLabel = \"constructionrig\"\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    const Hud hud;
    const Hud::Unit base = MissionView::displayOf("fbase", loader, hud, 1);
    CHECK(base.className == "Starbase" && base.name == "Starbase 473" && base.raceIcon == "federation_icon");
    CHECK(base.crew == 800 && base.maxCrew == 1500 && base.officers == 0 && base.wireframe == "FWbase");
    CHECK(base.systems[0] && !base.systems[1] && base.systems[2] && base.systems[3] && base.systems[4]);
    std::map<int, std::pair<std::string, bool>> controls;
    for (const Hud::Unit::Button &b : base.controls)
        controls[b.slot] = {b.sprite, b.enabled};
    CHECK(!controls[1].second && controls[2].second && controls.count(4) == 0);
    CHECK(controls[9].first == "b_fedoff" && controls.count(10) && controls.count(11));

    const Hud::Unit ship = MissionView::displayOf("fconst", loader, hud, 0);
    CHECK(ship.crew == 500 && ship.maxCrew == 500 && ship.officers == 2 && ship.wireframe == "FWconst");
    CHECK(ship.name.empty() && ship.systems[1] && !ship.systems[0]);
    controls.clear();
    for (const Hud::Unit::Button &b : ship.controls)
        controls[b.slot] = {b.sprite, b.enabled};
    CHECK(controls[1].second && !controls[2].second && controls.count(4) && !controls[4].second);
    CHECK(controls.count(9) == 0 && controls.count(10) && controls.count(11) == 0);
}

// A box keeps the ships that move, leaving out the starbase with them;
// alone, the starbase; and only the player's own.
void testBox() {
    using reluva::storm3d::MapScene;
    auto unit = [](const char *odf, int slot, float x, float z) {
        MapScene::Unit u;
        u.odf = odf;
        u.slot = slot;
        u.at = {x, 38.5f, z};
        u.lo = {x - 10, 30, z - 10};
        u.hi = {x + 10, 50, z + 10};
        return u;
    };
    const std::vector<MapScene::Unit> units = {unit("fbase", 1, 677.6f, 502.8f), unit("fconst", 1, 560, 520),
                                               unit("fconst", 1, 600, 520), unit("fconst", 2, 620, 500)};
    const std::vector<bool> moves = {false, true, true, true};
    const auto viewProj = MapScene::viewProjOf(MapScene::overviewOf({576.6f, 38.5f, 444.5f}), 1000);
    CHECK((MissionView::boxed(units, moves, viewProj, 1, 0, 0, 640, 330) == std::vector<int>{1, 2}));
    CHECK((MissionView::boxed(units, moves, viewProj, -1, 640, 330, 0, 0) == std::vector<int>{1, 2, 3}));
    const auto base = MapScene::project(viewProj, units[0].middle());
    CHECK(base.has_value());
    if (base) {
        const int x = int((*base)[0]);
        const int y = int((*base)[1]);
        CHECK((MissionView::boxed(units, moves, viewProj, 1, x - 5, y - 5, x + 5, y + 5) == std::vector<int>{0}));
    }
    CHECK(MissionView::boxed(units, moves, viewProj, 1, 0, 0, 5, 5).empty());
}

// Two construction ships' orders: theirs, build dark, the tractor beam once.
void testGroupOrders() {
    Hud::Unit ship;
    ship.controls = {{2, "b_attack", "bb_standard", false},
                     {1, "b_move", "bb_standard", true},
                     {10, "b_build", "bb_standard", true}};
    ship.specials = {"b_gtracbm"};
    Hud::Unit other = ship;
    other.controls[0].enabled = true;
    const Hud::Unit orders = MissionView::ordersOf({ship, other});
    CHECK(orders.controls.size() == 3 && orders.controls[0].slot == 1 && orders.controls[1].enabled);
    CHECK(!orders.controls[2].enabled && orders.specials.size() == 1);
    CHECK(MissionView::ordersOf({ship}).controls[2].enabled);
}

// A construction ship's flight, as the original's: straight ahead 145,
// it's at full speed in about a second and stopped near the point in
// about five; and its physics from its file.
void testMovement() {
    using reluva::storm3d::Movement;
    const std::map<std::string, std::string> files = {
        {"fconst.odf", "[CraftClass]\nphysicsFile = \"cnstphys.odf\"\n"},
        {"cnstphys.odf", "[Physics]\nimpulseSpeed = 20\nwarpSpeed = 50\nyawRotationRate = 0.72\n"
                         "headingCloseEnoughForWarp = 30;\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    reluva::storm3d::Odf unit;
    CHECK(!unit.load("fconst.odf", loader));
    const Movement::Physics physics = Movement::physicsOf(unit, loader);
    CHECK(physics.topSpeed == 50 && physics.cornerSpeed == 20 && std::abs(physics.turnRate - 0.72f) < 1e-6f);
    CHECK(std::abs(physics.wideTurn - 0.5236f) < 1e-3f);

    Movement::Ship ship;
    ship.at = {0, 38.5f, 0};
    ship.goal = reluva::gfx::Vec3{0, 38.5f, 145};
    ship.waiting = Movement::kSetOff;
    float fastest = 0;
    double stopped = -1;
    for (int i = 1; i <= 1000; ++i) {
        Movement::step(ship, physics, 0.01);
        if (i == 150)
            CHECK(ship.speed > 40);
        fastest = std::max(fastest, ship.speed);
        if (stopped < 0 && !ship.goal && ship.speed < 0.5f)
            stopped = i * 0.01;
    }
    CHECK(fastest > 45 && fastest <= 50);
    CHECK(stopped > 3.5 && stopped < 6);
    CHECK(std::abs(ship.at[2] - 145) < 8 && std::abs(ship.at[0]) < 0.01f);

    // Round the starbase in its way, on the side away from it.
    const std::vector<Movement::Obstacle> base = {{{677.6f, 38.5f, 502.8f}, 52.6f}};
    const auto way = Movement::routeAround({624.4f, 38.5f, 548.7f}, {777.7f, 38.5f, 568.9f}, base, 27.3f);
    CHECK(way.size() == 1 && way[0][2] > 600);
    CHECK(Movement::routeAround({624.4f, 38.5f, 548.7f}, {624.4f, 38.5f, 700}, base, 27.3f).empty());
}

// The click's point on the plane is the point the camera shows there.
void testPointOn() {
    using reluva::storm3d::MapScene;
    const auto camera = MapScene::overviewOf({576.6f, 38.5f, 444.5f});
    const auto viewProj = MapScene::viewProjOf(camera, 1000);
    const reluva::gfx::Vec3 point{700, 38.5f, 560};
    const auto at = MapScene::project(viewProj, point);
    CHECK(at.has_value());
    if (at) {
        const auto back = MapScene::pointOn(camera, 38.5f, int(std::lround((*at)[0])), int(std::lround((*at)[1])));
        CHECK(back && std::abs((*back)[0] - 700) < 2 && std::abs((*back)[2] - 560) < 3);
    }
}

// A group sent somewhere, as the original's two construction ships went:
// the first to the point, each other on its side of it, at least 100 off;
// one already further keeps its place from the first.
void testSendGroup() {
    using reluva::storm3d::Movement;
    std::vector<Movement::Ship> ships(4);
    ships[0].at = {0, 38.5f, 0};
    ships[1].at = {-53, 38.5f, 0};
    ships[2].at = {500, 38.5f, 0};
    ships[3].at = {0, 38.5f, 300};
    MissionView::sendTo(ships, {}, {0, 1, 2, 3}, {true, true, false, true}, {110, 38.5f, 100});
    CHECK(ships[0].goal && ships[1].goal && !ships[2].goal && ships[3].goal);
    CHECK((*ships[0].goal)[0] == 110 && (*ships[0].goal)[2] == 100);
    CHECK(std::fabs((*ships[1].goal)[0] - 10) < 1e-3f && std::fabs((*ships[1].goal)[2] - 100) < 1e-3f);
    CHECK(std::fabs((*ships[3].goal)[2] - 400) < 1e-3f);
    CHECK(ships[0].waiting == Movement::kSetOff);
}

// The crew as the original's counter grew with one starbase: 3502 to
// 3607 in 30 seconds; none without one; never past cfgMaxCrew.
void testCrew() {
    double carry = 0;
    int crew = 3502;
    for (int i = 0; i < 300; ++i)
        crew = MissionView::grownCrew(crew, carry, 1, 0.1);
    CHECK(crew == 3607);
    carry = 0;
    CHECK(MissionView::grownCrew(3500, carry, 0, 10) == 3500);
    CHECK(MissionView::grownCrew(99990, carry, 2, 10) == 99999);
}

// The minimap's marks and view: a unit in its player's colour, white
// without one, a station bigger; the view's top edge where the
// original's lines fell, (12.5, 454.6) to (28.6, 454.6) on 4first.bzn.
void testMinimap() {
    using reluva::storm3d::MapScene;
    reluva::storm3d::BznMap map;
    map.area = {0, 6400, -28.5f, 105.5f, 0, 6400};
    MapScene::Unit base;
    base.at = {677.6f, 38.5f, 502.8f};
    MapScene::Unit ship;
    ship.at = {570, 38.5f, 546};
    ship.colour = reluva::gfx::Vec3{1, 0, 0};
    const auto camera = MapScene::overviewOf({576.6f, 38.5f, 444.5f});
    const Hud::Minimap m = MissionView::minimapOf(map, {base, ship}, {false, true}, camera, 38.5f);
    CHECK(m.right == 6400 && m.far == 6400 && m.marks.size() == 2);
    CHECK(m.marks[0].station && (m.marks[0].colour == std::array<uint8_t, 3>{230, 230, 230}));
    CHECK(!m.marks[1].station && (m.marks[1].colour == std::array<uint8_t, 3>{255, 0, 0}));
    CHECK(m.view.size() == 4);
    if (m.view.size() == 4) {
        auto px = [](float x) { return 9 + x / 50; };
        auto py = [](float z) { return 343 + (6400 - z) / 50; };
        CHECK(std::abs(px(m.view[0][0]) - 12.5f) < 0.3f && std::abs(px(m.view[1][0]) - 28.6f) < 0.3f);
        CHECK(std::abs(py(m.view[0][1]) - 454.6f) < 0.3f);
    }
}

// The cinematic view's close-up: the construction ship's, as fitted, and
// turned with a unit's heading.
void testCloseUp() {
    reluva::storm3d::MapScene::Unit ship;
    ship.at = {570, 38.5f, 546};
    ship.lo = {562.5f, 36, 517.5f};
    ship.hi = {577.5f, 46, 568.5f};
    const auto cam = MissionView::closeUpOf(ship);
    const auto middle = ship.middle();
    const float distance = std::hypot(cam.eye[0] - middle[0], cam.eye[1] - middle[1], cam.eye[2] - middle[2]);
    CHECK(std::abs(distance - 1.53f * MissionView::ringHalfOf(ship)) < 0.05f);
    CHECK(cam.eye[0] < middle[0] && cam.eye[2] < middle[2] && cam.eye[1] > middle[1]);
    CHECK(std::abs(cam.forward[1] + 0.857f) < 0.01f && std::abs(cam.fovY - 0.9273f) < 0.001f);
    // Facing +x, it's seen from behind (-x) and to its left (+z).
    ship.heading = 1.5707963f;
    const auto turned = MissionView::closeUpOf(ship);
    CHECK(turned.eye[0] < middle[0] && turned.eye[2] > middle[2]);
}

// The starbase's build menu: its ships, not the officers' upgrades; the
// freighter dark until there's a mining station; the back button last.
// Items lit by a map's tech tree.
void testBuildMenu() {
    const std::map<std::string, std::string> files = {
        {"fbase.odf", "[S]\nbuildItem0 = \"fconst\"\nbuildItem1 = \"ffreight\"\nbuildItem2 = \"fedoff\"\n"},
        {"fconst.odf", "[S]\nclassLabel = \"constructionrig\"\n"},
        {"ffreight.odf", "[S]\nclassLabel = \"freighter\"\n"},
        {"fedoff.odf", "[S]\nclassLabel = \"upgrade\"\n"},
        {"fmining.odf", "[S]\nclassLabel = \"mining\"\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    const std::vector<std::string> items = MissionView::buildListOf("fbase", loader);
    CHECK((items == std::vector<std::string>{"fconst", "ffreight"}));
    const Hud::Unit menu = MissionView::buildMenuOf(items, loader, {"fbase", "fconst"});
    CHECK(menu.controls.size() == 3 && menu.controls[0].sprite == "b_fconst" && menu.controls[0].enabled);
    CHECK(menu.controls[0].border == "bb_build" && !menu.controls[1].enabled);
    CHECK(menu.controls[2].slot == 12 && menu.controls[2].sprite == "b_back");
    CHECK(MissionView::buildMenuOf(items, loader, {"fbase", "fmining"}).controls[1].enabled);

    // With the map's tech tree (tech1.tt's lines): the construction ship's
    // six firsts lit with the starting units, its last four dark, as the
    // original's; the advanced yard lit once there's a yard and research.
    reluva::storm3d::TechTree tree;
    tree.parse("fbase.odf    0\nfmining.odf  0\nfyard.odf    1 fbase.odf\nfsensor.odf  1 fbase.odf\n"
               "fturret.odf  1 fbase.odf\nfresear.odf  1 fbase.odf\nfturret2.odf 1 fyard.odf\n"
               "fyard2.odf   2 fyard.odf fresear.odf\nfresear2.odf 2 fyard.odf fresear.odf\n"
               "fsuperbl.odf 3 fbase.odf fyard2.odf fresear2.odf\n\nffreight.odf 2 fbase.odf fmining.odf\n");
    CHECK(tree.needsOf("FYARD2") && tree.needsOf("fyard2")->size() == 2 && !tree.needsOf("fconst"));
    const std::vector<std::string> builds = {"fbase",   "fyard",    "fsensor", "fturret",  "fmining",
                                             "fresear", "fresear2", "fyard2",  "fturret2", "fsuperbl"};
    const Hud::Unit start = MissionView::buildMenuOf(builds, loader, {"fbase", "fconst"}, tree);
    for (size_t i = 0; i < builds.size(); ++i)
        CHECK(start.controls[i].enabled == (i < 6));
    CHECK(MissionView::buildMenuOf({"fyard2"}, loader, {"fbase", "fyard", "fresear"}, tree).controls[0].enabled);
    CHECK(!MissionView::buildMenuOf({"ffreight"}, loader, {"fbase"}, tree).controls[0].enabled);
}

// A construction ship's order, as the original took it: 500 dilithium
// and 500 crew at once, 2 officers on the count, 50 seconds.
void testOrders() {
    const std::map<std::string, std::string> files = {
        {"fconst.odf", "[S]\nbuildTime = 50.0\ndilithiumCost = 500\ncrewCost = 500\nofficerCost = 2\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    const MissionView::Order o = MissionView::orderOf("fconst", loader);
    CHECK(o.time == 50 && o.dilithium == 500 && o.crew == 500 && o.officers == 2);
    Hud::Resources r{3000, 3586, 4, 120};
    CHECK(MissionView::pay(r, o) && r.dilithium == 2500 && r.crew == 3086 && r.officers == 6);
    Hud::Resources poor{400, 3586, 4, 120};
    CHECK(!MissionView::pay(poor, o) && poor.dilithium == 400 && poor.crew == 3586);
    Hud::Resources full{3000, 3586, 119, 120};
    CHECK(!MissionView::pay(full, o));
    const auto to = MissionView::launchPointOf({677.6f, 38.5f, 502.8f});
    CHECK(std::abs(to[0] - 546.6f) < 0.1f && std::abs(to[1] - 53.5f) < 0.1f && std::abs(to[2] - 392.8f) < 0.1f);
}

// Placing a station: the cells the starbase's footprint takes and those a
// station would, the construction ship waiting 53 from the site; the
// construction ship's later items dark until what they need is built.
void testPlacing() {
    using reluva::storm3d::MapScene;
    // The starbase's box and 80 give the original's yellow block.
    MissionView::Footprint base;
    base.x = 677.6f + (-38.106f + 37.195f) / 2;
    base.z = 502.8f + (-35.862f + 37.323f) / 2;
    base.halfX = (37.195f + 38.106f) / 2;
    base.halfZ = (37.323f + 35.862f) / 2;
    base.buffer = 80;
    const auto taken = MissionView::takenCellsOf({base});
    CHECK(taken.size() == 30 && taken.count({11, 7}) && taken.count({15, 12}));
    CHECK(!taken.count({10, 8}) && !taken.count({16, 10}) && !taken.count({13, 13}));
    // The cannon takes its cell; the mining station five by five about it.
    MissionView::Footprint cannon;
    cannon.halfX = cannon.halfZ = 12.3f;
    cannon.buffer = 10;
    CHECK((MissionView::siteCellsOf({514.6f, 38.5f, 434.4f}, cannon) == std::vector<std::pair<int, int>>{{10, 8}}));
    MissionView::Footprint mining;
    mining.halfX = 45.1f;
    mining.halfZ = 43.2f;
    mining.buffer = 80;
    const auto cells = MissionView::siteCellsOf({1025, 38.5f, 578}, mining);
    CHECK(cells.size() == 25 && cells.front() == std::make_pair(18, 9) && cells.back() == std::make_pair(22, 13));
    // Stood off 53 from the cannon's site, 100 from the mining station's.
    CHECK(std::abs(MissionView::standOffDistanceOf(cannon) - 53) < 2);
    CHECK(std::abs(MissionView::standOffDistanceOf(mining) - 100) < 2);
    const auto stand = MissionView::standOffOf({514.6f, 38.5f, 434.4f}, {624, 38.5f, 548}, 53);
    CHECK(std::abs(std::hypot(stand[0] - 514.6f, stand[2] - 434.4f) - 53) < 0.01f && stand[0] > 514.6f);

    const std::map<std::string, std::string> files = {{"fturret.odf", "[S]\nclassLabel = \"sensor\"\n"},
                                                      {"fyard2.odf", "[S]\nclassLabel = \"shipyard\"\n"},
                                                      {"fyard.odf", "[S]\nclassLabel = \"shipyard\"\n"},
                                                      {"fbase.odf", "[S]\nclassLabel = \"starbase\"\n"}};
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    const auto menu = MissionView::buildMenuOf({"fturret", "fyard2"}, loader, {"fbase"});
    CHECK(menu.controls[0].enabled && !menu.controls[1].enabled);
    CHECK(MissionView::buildMenuOf({"fturret", "fyard2"}, loader, {"fbase", "fyard"}).controls[1].enabled);
}

// The map's objects with a mark on the minimap: a dilithium moon's.
void testMapIcons() {
    const std::map<std::string, std::string> files = {
        {"mdmoon.odf", "[GameObjectClass]\nmapIcon = \"map_dmoon\"\n"},
        {"mnebula3.odf", "[GameObjectClass]\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    reluva::storm3d::BznMap map;
    map.objects.resize(3);
    map.objects[0].odf = "mdmoon";
    map.objects[0].position = {1118.5f, -50, 577};
    map.objects[1].odf = "mnebula3";
    map.objects[2].odf = "MDMOON";
    const auto icons = MissionView::mapIconsOf(map, loader);
    CHECK(icons.size() == 2 && icons[0].sprite == "map_dmoon" && icons[0].x == 1118.5f && icons[0].z == 577);
}

// A freighter's rounds between the original's mining station and its
// moon: 150 a time, unloaded over ten seconds, about every 44 seconds.
void testFreighter() {
    using reluva::storm3d::Movement;
    const reluva::gfx::Vec3 station{923, 38.5f, 800};
    const reluva::gfx::Vec3 moon{1118.5f, -50, 577};
    Movement::Physics physics; // cnstphys.odf's, as the construction ship's
    Movement::Ship ship;
    ship.at = station;
    MissionView::Freighter freighter;
    int dilithium = 2000;
    std::vector<double> deliveries; // when each began
    int last = dilithium;
    for (int i = 0; i < 20000; ++i) {
        const double t = i * 0.01;
        MissionView::work(freighter, ship, moon, station, dilithium, 0.01);
        Movement::step(ship, physics, 0.01);
        if (dilithium > last && (deliveries.empty() || t - deliveries.back() > 20))
            deliveries.push_back(t);
        last = dilithium;
    }
    CHECK(deliveries.size() >= 4 && std::abs(deliveries[0] - 33) < 2.5);
    CHECK(dilithium >= 2000 + 150 * int(deliveries.size() - 1));
    // Each load comes to 150 exactly.
    CHECK(freighter.state != MissionView::Freighter::State::kUnloading ? (dilithium - 2000) % 150 == 0 : true);
    if (deliveries.size() >= 3) {
        const double period = (deliveries.back() - deliveries[1]) / double(deliveries.size() - 2);
        CHECK(std::abs(period - 44.3) < 1.5);
    }
}

// The starbase's eight pulses at a construction ship, as the original's:
// three quarters of its shields after the first volley, none after the
// fifth, and it's gone at the seventh, 27 seconds in.
void testCombat() {
    using reluva::storm3d::Combat;
    const std::map<std::string, std::string> files = {
        {"fbase.odf", "[S]\nweapon1 = \"fbpulse\"\nweapon2 = \"fbpulse\"\nweapon3 = \"gtracbm\"\n"},
        {"fbpulse.odf", "[W]\nordName = \"fbpulseo\"\nshotDelay = 4.5\nrange = 400.0f\nhitChance = 0.7\n"
                        "toHitStopModifier = 0.50\ntoHitSlowModifier = 0.10\nwarpToHitModifier = -0.25\n"},
        {"fbpulseo.odf", "[O]\ndamageBase = 22\ndamageVariance = 2\nshotSpeed = 300\nSprite = \"pphaser\"\n"},
        {"gtracbm.odf", "[W]\nordName = \"tracbm\"\nspecial = 1\n"},
        {"fconst.odf", "[S]\nmaxHealth = 700\nshieldRate = 3.5\n"},
    };
    const reluva::storm3d::Odf::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    reluva::storm3d::Odf base;
    reluva::storm3d::Odf ship;
    CHECK(!base.load("fbase.odf", loader) && !ship.load("fconst.odf", loader));
    const auto weapons = Combat::weaponsOf(base, loader);
    CHECK(weapons.size() == 2 && weapons[0].range == 400 && weapons[0].delay == 4.5 && weapons[0].damage == 22);
    CHECK(weapons[0].variance == 2 && weapons[0].speed == 300 && weapons[0].sprite == "pphaser");
    CHECK(Combat::damageOf(weapons[0], 0) == 20 && Combat::damageOf(weapons[0], 1) == 24);
    // pulse.odf's chances: 0.7 at a target moving fast, 0.8 at one slow,
    // all at one stopped; a quarter less from a shooter at warp.
    using Pace = Combat::Pace;
    CHECK(std::abs(Combat::hitChanceOf(weapons[0], Pace::kStopped, Pace::kWarp) - 0.7f) < 1e-6f);
    CHECK(std::abs(Combat::hitChanceOf(weapons[0], Pace::kStopped, Pace::kSlow) - 0.8f) < 1e-6f);
    CHECK(Combat::hitChanceOf(weapons[0], Pace::kStopped, Pace::kStopped) >= 1);
    CHECK(std::abs(Combat::hitChanceOf(weapons[0], Pace::kWarp, Pace::kWarp) - 0.45f) < 1e-6f);

    Combat::Vitals v = Combat::vitalsOf(ship);
    CHECK(v.shields == 700 && v.shieldRate == 3.5f);
    // Its shields a quarter down a volley, gone at the fifth, as the
    // original's bar went.
    for (int i = 0; i <= 1800; ++i) {
        if (i % 450 == 0) {
            for (int shot = 0; shot < 8; ++shot)
                Combat::hit(v, 22);
            if (i == 0)
                CHECK(std::abs(v.shields / v.maxShields - 0.753f) < 0.01f);
        }
        Combat::regenerate(v, 0.01);
    }
    CHECK(v.shields < 1 && v.alive());
}

// The computer player's build list: read as the game's, and its next
// entry the first not yet met, counting each type's entries together.
void testPlan() {
    using reluva::storm3d::Movement;
    using Terrain = Movement::Terrain;
    const reluva::gfx::Vec3 from{200, 0, 1000};
    const reluva::gfx::Vec3 to{1800, 0, 1000};
    // Open space: straight there.
    Terrain open = Terrain::over(0, 0, 2000, 2000);
    CHECK(Movement::plan(open, from, to).size() == 1);
    // An impassable square between: round it, every leg clear of it.
    Terrain hole = open;
    hole.mark({1000, 0, 1000}, 300, Terrain::kImpassable);
    const auto way = Movement::plan(hole, from, to);
    CHECK(way.size() >= 2 && way.back()[0] == to[0] && way.back()[2] == to[2]);
    reluva::gfx::Vec3 at = from;
    for (const reluva::gfx::Vec3 &p : way) {
        for (int k = 1; k < 100; ++k) {
            const float t = float(k) / 100;
            CHECK(hole.at(at[0] + (p[0] - at[0]) * t, at[2] + (p[2] - at[2]) * t) != Terrain::kImpassable);
        }
        at = p;
    }
    // A nebula costing 2 is flown through; one dearer than 200 isn't.
    Terrain cheap = open;
    cheap.mark({1000, 0, 1000}, 300, 2);
    CHECK(Movement::plan(cheap, from, to).size() == 1);
    Terrain dear = open;
    dear.mark({1000, 0, 1000}, 100, 400);
    CHECK(Movement::plan(dear, from, to).size() >= 2);
    // A goal inside what can't be entered: as near as can be.
    const auto near = Movement::plan(hole, from, {1000, 0, 1000});
    CHECK(!near.empty() && hole.at(near.back()[0], near.back()[2]) != Terrain::kImpassable);
    CHECK(std::hypot(near.back()[0] - 1000, near.back()[2] - 1000) < 400);
    // Off the map costs OFF_MAP_PATHING_COST.
    CHECK(open.at(-200, 1000) == Terrain::kOffMap && open.at(10, 10) == Terrain::kDefault);
}

void testSettled() {
    using reluva::storm3d::MissionView;
    std::vector<MissionView::Player> players(3);
    for (int k = 0; k < 3; ++k)
        players[size_t(k)].slot = k + 1;
    players[0].team = 0;
    players[1].team = 1;
    players[2].team = 1;
    // The player out with two others still fighting: not settled.
    CHECK(!MissionView::settled(players, {2, 3}, false));
    // The two left on one team: settled with Allied Victory, not without.
    CHECK(MissionView::settled(players, {2, 3}, true));
    players[2].team = 2;
    CHECK(!MissionView::settled(players, {2, 3}, true));
    // One player left, or none.
    CHECK(MissionView::settled(players, {3}, false));
    CHECK(MissionView::settled(players, {}, false));
}

void testBuildList() {
    using reluva::storm3d::ComputerPlayer;
    // Round a moon (5927, 960) whose station must stand 300 off: the
    // original's mining station at about (5673, 709), its cannon, which may
    // stand 170 off, at about (5803, 839).
    auto spotOff = [](float clear) {
        return ComputerPlayer::siteNear([clear](float dx, float dz) { return std::hypot(dx, dz) >= clear; });
    };
    const auto station = spotOff(300);
    CHECK(station && station->first == -260 && station->second == -260);
    const auto cannon = spotOff(170);
    CHECK(cannon && cannon->first == -130 && cannon->second == -130);
    // The rings' order: the middle, then the first ring's -z row, its sides, its +z row.
    std::vector<std::pair<float, float>> order;
    (void)ComputerPlayer::siteNear([&order](float dx, float dz) {
        order.emplace_back(dx, dz);
        return order.size() == 9;
    });
    const std::vector<std::pair<float, float>> ring = {{0, 0},  {-65, -65}, {0, -65}, {65, -65}, {-65, 0},
                                                       {65, 0}, {-65, 65},  {0, 65},  {65, 65}};
    CHECK(order == ring);
    CHECK(!ComputerPlayer::siteNear([](float, float) { return false; }));
    CHECK(ComputerPlayer::buildListFileOf("kling") == "klingon_build_list.aip");
    const std::map<std::string, std::string> files = {
        {"aipdef.h", "struct Better_Build_List_Element\n{\n char unit_type_name[80];\n double units_to_build;\n"
                     " int m_base_num;\n};\n#define MAX_ELEMENTS 200\n"},
        {"federation_build_list.aip",
         "#include \"aipdef.h\"\nBetter_Build_List_Element the_build_list[MAX_ELEMENTS] =\n{\n"
         " \"fbase\", 1,0, // first base\n \"fconst\", 1,0,\n \"fmining\", 1,1,\n \"ffreight\", 2,0,\n"
         " \"fmining\", 1,2,\n};\n"},
    };
    const reluva::storm3d::ConfigFile::Loader loader = [&files](std::string_view name, std::string &text) {
        auto it = files.find(std::string(name));
        if (it == files.end())
            return reluva::Error::fail("not found");
        text = it->second;
        return reluva::Error::success();
    };
    const auto list = ComputerPlayer::buildListOf("federation_build_list.aip", loader);
    CHECK(list.size() == 5 && list[2].type == "fmining" && list[2].count == 1 && list[2].base == 1);
    CHECK(list[3].count == 2 && list[4].base == 2);
    // The skirmish's start: a starbase and two construction ships meet the
    // first two; a mining station is next.
    CHECK(ComputerPlayer::nextOf(list, {{"fbase", 1}, {"fconst", 2}}) == 2u);
    CHECK(ComputerPlayer::nextOf(list, {{"fbase", 1}, {"fconst", 2}, {"fmining", 1}, {"ffreight", 1}}) == 3u);
    CHECK(ComputerPlayer::nextOf(list, {{"fbase", 1}, {"fconst", 2}, {"fmining", 1}, {"ffreight", 2}}) == 4u);
    CHECK(!ComputerPlayer::nextOf(list, {{"fbase", 1}, {"fconst", 1}, {"fmining", 2}, {"ffreight", 2}}));
}

// A Medium or Hard computer's stance: by its units' hull against the
// average of the players', each loading its personality and its race's
// list. The skirmish's start (6,000 + 700 + 700 each) is even: normal.
void testStances() {
    using reluva::storm3d::ComputerPlayer;
    using Stance = ComputerPlayer::Stance;
    CHECK(ComputerPlayer::buildListFileOf("borg", Stance::kDefensive) == "borg_defensive_list.aip");
    CHECK(ComputerPlayer::buildListFileOf("rom", Stance::kOffensive) == "romulan_offensive_list.aip");
    CHECK(ComputerPlayer::personalityFileOf(Stance::kNormal) == "newdefault.aip");
    CHECK(ComputerPlayer::personalityFileOf(Stance::kDefensive) == "default_defensive.aip");
    CHECK(ComputerPlayer::personalityFileOf(Stance::kOffensive) == "default_offensive.aip");
    CHECK(ComputerPlayer::stanceFor(7400, 7400) == Stance::kNormal);
    // Two players: 10,000 against 6,000 averages 8,000; 1.1 times it is
    // 8,800 and 0.7 times it 5,600.
    CHECK(ComputerPlayer::stanceFor(10000, 8000) == Stance::kOffensive);
    CHECK(ComputerPlayer::stanceFor(8800, 8000) == Stance::kOffensive);
    CHECK(ComputerPlayer::stanceFor(8700, 8000) == Stance::kNormal);
    CHECK(ComputerPlayer::stanceFor(6000, 8000) == Stance::kNormal);
    CHECK(ComputerPlayer::stanceFor(5600, 8000) == Stance::kDefensive);
    CHECK(ComputerPlayer::stanceFor(0, 8000) == Stance::kDefensive);
}

// The scheduler, as the game's README tells it: goals by what's in each
// cell, ships committed nearest and best first until a goal's enemy
// shields are beaten, one to explore, and those short taken back.
void testStrategy() {
    using reluva::storm3d::Strategy;
    Strategy::Personality p; // federation.aip's
    std::vector<Strategy::Cell> cells(3 * 3);
    for (auto &c : cells)
        c.explored = true;
    cells[0].ourStations = 1; // (0, 0): home
    cells[2].enemyShips = 1;  // (2, 0): an enemy ship
    cells[2].enemyShields = 700;
    cells[8].enemyStations = 1; // (2, 2): its base
    cells[8].enemyShields = 6000;
    cells[4].explored = false; // (1, 1): not seen
    const auto goals = Strategy::goalsOf(cells, 3, 3, p);
    auto find = [&goals](Strategy::Kind k, int x, int z) {
        for (size_t i = 0; i < goals.size(); ++i)
            if (goals[i].kind == k && goals[i].x == x && goals[i].z == z)
                return std::optional<size_t>(i);
        return std::optional<size_t>();
    };
    const auto attack = find(Strategy::Kind::kAttack, 2, 0);
    CHECK(attack && find(Strategy::Kind::kDefend, 0, 0) && find(Strategy::Kind::kAttackBase, 2, 2));
    CHECK(find(Strategy::Kind::kExplore, 1, 1) && !find(Strategy::Kind::kExplore, 0, 0));
    CHECK(!find(Strategy::Kind::kAttack, 2, 2)); // a cell with their stations is the base's goal
    if (attack)
        CHECK(std::abs(goals[*attack].priority - (2 * 0 + 2 * 10 + 1000 + 10)) < 1e-6);

    // Two ships of 500: the ship beats 700 only with both; one alone is
    // taken back and goes exploring.
    const std::vector<Strategy::Ship> two = {{1000, 200, 500}, {1000, 300, 500}};
    const auto to = Strategy::assign(two, goals, p);
    CHECK(to[0] && to[1] && *to[0] == *attack && *to[1] == *attack);
    const auto one = Strategy::assign({{1000, 200, 500}}, goals, p);
    CHECK(one[0] && goals[*one[0]].kind != Strategy::Kind::kAttack);
}

} // namespace

// A pod names the special weapon it gives by its tooltip, the weapon's
// without its key; special energy comes back at its rate, to its most.
void testSpecials() {
    using reluva::storm3d::Combat;
    CHECK(Combat::researchKeyOf("Antimatter Mines (^F^1) - Defiant") == "antimatter mines - defiant");
    CHECK(Combat::researchKeyOf("^Antimatter Mines - Defiant") == "antimatter mines - defiant");
    CHECK(Combat::researchKeyOf("Chain Reaction Pulsar (^F^2) - Akira") ==
          Combat::researchKeyOf("^Chain Reaction Pulsar - Akira"));
    Combat::Vitals v;
    v.maxShields = v.shields = 150;
    v.maxEnergy = 1000;
    v.energyRate = 10;
    Combat::regenerate(v, 30);
    CHECK(std::fabs(v.energy - 300) < 1e-3f);
    Combat::regenerate(v, 100);
    CHECK(v.energy == 1000);
}

// Crew lost as a ship's hit, a share of those aboard: on the shields only
// past the shot's damageThreshold, by its shieldCrewModifier; a hit past
// the shields lands on a system, taking its hit points and its crewLoss
// once it's out, or on the hull alone, by the shot's hullCrewModifier, or
// critically, destroying it; life support out drains the crew; systems
// are repaired over their repair time.
void testCrewAndSystems() {
    using reluva::storm3d::Combat;
    Combat::Vitals v;
    v.maxShields = v.shields = 100;
    v.maxCrew = v.crew = 500;
    v.lifeSupportLoss = 2;
    const Combat::CrewCost pulse{0.2f, 0.3f, 23}; // the starbase's fbpulseo
    // 22 isn't past its 23: none; 24 is 1 past: 0.2% of 500, and 1.
    Combat::hit(v, 22, pulse, 0.5f);
    CHECK(v.crew == 500 && v.shields == 78);
    Combat::hit(v, 24, pulse, 0.5f);
    CHECK(std::fabs(v.crew - 498) < 1e-3f);

    Combat::SystemState &life = v.systems[Combat::kLifeSupport];
    life.maxHitPoints = life.hitPoints = 10;
    life.hitPercent = 100;
    life.repairTime = 10;
    life.crewLoss = 3;
    // 22 with 10 shields left: 10 on them, 12 past, life support's 10
    // points gone: 3% of the crew, and 1.
    v.shields = 10;
    v.crew = 500;
    Combat::hit(v, 22, {0.2f, 0.3f, 0}, 0.5f);
    CHECK(v.shields == 0 && life.out() && v.damaged());
    CHECK(std::fabs(v.crew - (500 - 15 - 1)) < 1e-3f);
    // On the hull alone: 0.3% a point of the 12, and 1.
    Combat::Vitals hull = v;
    hull.systems = {};
    hull.hullHitPercent = 100;
    hull.crew = 500;
    Combat::hit(hull, 12, {0.2f, 0.3f, 0}, 0.5f);
    CHECK(std::fabs(hull.crew - (500 - 18 - 1)) < 1e-3f && hull.alive());
    // Past every share, a critical hit of a whole point or more: gone; under
    // one, kept for the next.
    hull.hullHitPercent = 50;
    Combat::Vitals critical = hull;
    Combat::hit(critical, 0.6f, {}, 0.9f);
    CHECK(critical.alive() && std::fabs(critical.carry - 0.6f) < 1e-6f);
    Combat::hit(critical, 0.6f, {}, 0.9f);
    CHECK(!critical.alive());
    // The roll's order: life support's share first, then the shield
    // generator's.
    Combat::Vitals order;
    order.maxCrew = order.crew = 100;
    for (Combat::System s : {Combat::kLifeSupport, Combat::kShieldGenerator}) {
        order.systems[s].maxHitPoints = order.systems[s].hitPoints = 10;
        order.systems[s].hitPercent = 50;
    }
    Combat::hit(order, 5, {}, 0.49f);
    CHECK(order.systems[Combat::kLifeSupport].hitPoints == 5 &&
          order.systems[Combat::kShieldGenerator].hitPoints == 10);
    Combat::hit(order, 5, {}, 0.5f);
    CHECK(order.systems[Combat::kShieldGenerator].hitPoints == 5);
    // Under one crew left: none, derelict.
    order.crew = 1.5f;
    order.hullHitPercent = 100;
    order.systems = {};
    Combat::hit(order, 1, {0, 0, 0}, 0.5f);
    CHECK(order.crew == 0 && order.derelict() && order.alive());
    // Out, its crew drains 2% a second; repaired over its 10 seconds.
    const float before = v.crew;
    Combat::regenerate(v, 1);
    CHECK(v.crew < before && v.crew > before * 0.97f);
    Combat::regenerate(v, 10);
    CHECK(!life.out() && !v.damaged());
}

// A boarding party against a crew, each times its race's strength (the
// Klingons' 1.5, the Federation's 1): the bigger side keeps the
// difference, in its own crew. Repairs go by the crew: faster by the
// race's repairStrength, slower by repairRed with the crew below its
// share, none with no crew.
void testBoardingAndRepair() {
    using reluva::storm3d::Combat;
    const Combat::Boarding took = Combat::board(100, 1.5f, 120, 1);
    CHECK(took.taken && std::fabs(took.left - 20) < 1e-3f);
    const Combat::Boarding held = Combat::board(50, 1, 80, 1);
    CHECK(!held.taken && std::fabs(held.left - 30) < 1e-3f);
    CHECK(Combat::raceFileOf("Federation") == "fed" && Combat::raceFileOf("klingon") == "kling");

    Combat::Vitals v;
    v.maxShields = v.shields = 100;
    v.maxCrew = 100;
    v.crew = 100;
    v.repairStrength = 1.5f;
    v.crewRed = 0.2f;
    v.repairRed = 2;
    Combat::SystemState &engines = v.systems[Combat::kEngines];
    engines.maxHitPoints = 10;
    engines.repairTime = 10;
    Combat::regenerate(v, 2); // a full crew, 1.5 times as fast: 3 of 10
    CHECK(std::fabs(engines.hitPoints - 3) < 1e-3f);
    v.crew = 10; // below its red share: half again slower than that
    engines.hitPoints = 0;
    Combat::regenerate(v, 2);
    CHECK(std::fabs(engines.hitPoints - 1.5f) < 1e-3f);
    v.crew = 0; // none: no repairs
    engines.hitPoints = 0;
    Combat::regenerate(v, 2);
    CHECK(engines.hitPoints == 0);
}

// A ship pushed up or down climbs or dives, but not on past the ceiling
// or floor (67 and 10), and settles back to the plane at their middle.
void testClimb() {
    using reluva::storm3d::Movement;
    CHECK(Movement::kPlane == 38.5f);
    CHECK(Movement::climb(38.5f, 0, 1) == 38.5f);
    CHECK(Movement::climb(38.5f, 5, 0) == 43.5f);
    CHECK(Movement::climb(38.5f, 5, 1) == 42.5f);
    CHECK(Movement::climb(70, 5, 0) == 70);  // above the ceiling: no higher
    CHECK(Movement::climb(70, -5, 0) == 65); // but down
    CHECK(Movement::climb(8, -5, 0) == 8);   // below the floor: no lower
    CHECK(Movement::climb(40, 0, 5) == 38.5f);
}

// Two ships closing head on are pushed apart before they meet, by what
// they'd come within 1.4 seconds; one passing well clear isn't; the push
// builds up from the last frame's. A way past two obstacles goes round
// both in turn.
void testAvoidance() {
    using reluva::storm3d::Movement;
    Movement::Ship ship;
    ship.heading = 0; // along +z
    ship.speed = 50;
    const std::vector<Movement::Neighbour> oncoming = {{{5, 0, 100}, {0, 0, -50}, 20}};
    const reluva::gfx::Vec3 first = Movement::avoid(ship, {0, 0, 0}, 20, oncoming);
    CHECK(first[0] < 0);                       // away from the other, which is a little to the right
    CHECK(std::fabs(first[0]) < 0.3f + 1e-4f); // only this frame's share yet
    const reluva::gfx::Vec3 second = Movement::avoid(ship, {0, 0, 0}, 20, oncoming);
    CHECK(second[0] < first[0]);
    Movement::Ship clear;
    clear.speed = 50;
    const std::vector<Movement::Neighbour> far = {{{300, 0, 100}, {0, 0, -50}, 20}};
    CHECK(Movement::avoid(clear, {0, 0, 0}, 20, far)[0] == 0);

    const auto way = Movement::routeAround({0, 0, 0}, {0, 0, 1000}, {{{10, 0, 300}, 50}, {{-10, 0, 700}, 50}}, 10);
    CHECK(way.size() == 2 && way[0][2] < way[1][2]);
    // Each passed on the side away from it: the second, at x -10, is right
    // of the way from the first detour, so passed on its left too.
    CHECK(way[0][0] < -10 && way[1][0] < -60);
}

// The original's destroyer: within its reach of 250 it held; its target
// flown to 300 away, it flew to the edge of its reach on the way.
void testChase() {
    CHECK(!MissionView::chasePointOf({0, 0, 0}, {186, 0, 0}, 250, 0));
    const auto point = MissionView::chasePointOf({0, 40, 0}, {0, 40, 300}, 250, 40);
    CHECK(point && std::fabs((*point)[0]) < 1e-3f && std::fabs((*point)[1] - 40) < 1e-3f);
    CHECK(point && std::fabs(300 - (*point)[2] - 250 * 0.95f) < 1e-2f);
}

static const reluva::test::Suite
    kSuite("storm3d.unit.play", {
                                    {"keys", [](const std::filesystem::path &) { testKeys(); }},
                                    {"moves", [](const std::filesystem::path &) { testMoves(); }},
                                    {"start", [](const std::filesystem::path &) { testStart(); }},
                                    {"layout", [](const std::filesystem::path &) { testLayout(); }},
                                    {"counters", [](const std::filesystem::path &) { testCounters(); }},
                                    {"selection", [](const std::filesystem::path &) { testSelection(); }},
                                    {"display", [](const std::filesystem::path &) { testDisplay(); }},
                                    {"box", [](const std::filesystem::path &) { testBox(); }},
                                    {"group orders", [](const std::filesystem::path &) { testGroupOrders(); }},
                                    {"movement", [](const std::filesystem::path &) { testMovement(); }},
                                    {"point on", [](const std::filesystem::path &) { testPointOn(); }},
                                    {"send group", [](const std::filesystem::path &) { testSendGroup(); }},
                                    {"crew", [](const std::filesystem::path &) { testCrew(); }},
                                    {"minimap", [](const std::filesystem::path &) { testMinimap(); }},
                                    {"close-up", [](const std::filesystem::path &) { testCloseUp(); }},
                                    {"build menu", [](const std::filesystem::path &) { testBuildMenu(); }},
                                    {"orders", [](const std::filesystem::path &) { testOrders(); }},
                                    {"placing", [](const std::filesystem::path &) { testPlacing(); }},
                                    {"chase", [](const std::filesystem::path &) { testChase(); }},
                                    {"specials", [](const std::filesystem::path &) { testSpecials(); }},
                                    {"crew", [](const std::filesystem::path &) { testCrewAndSystems(); }},
                                    {"boarding", [](const std::filesystem::path &) { testBoardingAndRepair(); }},
                                    {"avoidance", [](const std::filesystem::path &) { testAvoidance(); }},
                                    {"climb", [](const std::filesystem::path &) { testClimb(); }},
                                    {"map icons", [](const std::filesystem::path &) { testMapIcons(); }},
                                    {"freighter", [](const std::filesystem::path &) { testFreighter(); }},
                                    {"combat", [](const std::filesystem::path &) { testCombat(); }},
                                    {"settled", [](const std::filesystem::path &) { testSettled(); }},
                                    {"plan", [](const std::filesystem::path &) { testPlan(); }},
                                    {"build list", [](const std::filesystem::path &) { testBuildList(); }},
                                    {"stances", [](const std::filesystem::path &) { testStances(); }},
                                    {"strategy", [](const std::filesystem::path &) { testStrategy(); }},
                                });
