// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: a mission in play, its overview camera moved by
// the keys as the camera in the game's memory moved (camera.md).

#include "engines/storm3d/formats/gui_config.h"
#include "engines/storm3d/play/hud.h"
#include "engines/storm3d/play/mission_view.h"

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

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.play",
                                        {
                                            {"keys", [](const std::filesystem::path &) { testKeys(); }},
                                            {"moves", [](const std::filesystem::path &) { testMoves(); }},
                                            {"start", [](const std::filesystem::path &) { testStart(); }},
                                            {"layout", [](const std::filesystem::path &) { testLayout(); }},
                                            {"counters", [](const std::filesystem::path &) { testCounters(); }},
                                            {"selection", [](const std::filesystem::path &) { testSelection(); }},
                                            {"display", [](const std::filesystem::path &) { testDisplay(); }},
                                            {"box", [](const std::filesystem::path &) { testBox(); }},
                                            {"group orders", [](const std::filesystem::path &) { testGroupOrders(); }},
                                        });
