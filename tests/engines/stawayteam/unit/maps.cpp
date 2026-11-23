// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The maps: a .zax read, its ground's textures blended by their values,
// the polygons laid on it, and the camera kept on the map.

#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/ground_fractal.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/water_warp.h"
#include "engines/stawayteam/ui/map_screen.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cmath>
#include <string>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

// A 200 x 200 map, its view kept to 128 x 128: a 3 x 3 grid of values, so
// 2 x 2 cells; a model, and a black polygon over the first cell's top left.
const char *const kMap = "CLayerSaveData\n{\n\tWidth=200\n\tHeight=200\n\tView Position=100.000000,20.000000\n"
                         "\tCustom Map Properties=CStarTrekLayerSaveData\n\t{\n\t\tMinimap Image=Test map\n"
                         "\t\tMap Song=Song.ogg\n\t}\n"
                         "\tTree List=CSortList2D\n\t{\n"
                         "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tVisible=1\n\t\t\tModel=Things/Crate\n"
                         "\t\t\tPosition X=40.5\n\t\t\tPosition Y=60\n\t\t\tRendering Height=0\n"
                         "\t\t\tCur Sequence=Idle\n\t\t}\n"
                         "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=0\n"
                         "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CSpawnPointAI\n"
                         "\t\t\t\t{\n\t\t\t\t\tFacing Angle=90\n\t\t\t\t}\n\t\t\t}\n"
                         "\t\t\tModel=Editor/Spawn Point\n\t\t\tPosition X=20\n\t\t\tPosition Y=40\n\t\t}\n"
                         "\t\tLevel Part=CRenderablePolygon\n\t\t{\n\t\t\tVisible=1\n"
                         "\t\t\tPolygon=0, 0, 8, 0, 8, 8, 0, 8, 0, 0\n\t\t\tOn Ground=1\n\t\t\tRender Type=Black\n"
                         "\t\t\tStatic Texture=Red\n\t\t}\n"
                         "\t}\n"
                         "\tPlasma Ground=CPlasmaTileMap\n\t{\n\t\tBlending=1\n\t\tNum Textures=2\n"
                         "\t\tTexture 0=Red\n\t\tTexture 1=Blue\n"
                         "\t\tElevations Row 0=0040FF\n\t\tSeeds Row 0=000000\n"
                         "\t\tElevations Row 1=0040FF\n\t\tSeeds Row 1=000000\n"
                         "\t\tElevations Row 2=0040FF\n\t\tSeeds Row 2=000000\n\t}\n}\n";

void testRead() {
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(kMap, kMap + std::string(kMap).size());
    GameMap map;
    CHECK(loadMap(data, "Levels/Missing.zax", map));
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    CHECK(map.width == 200 && map.height == 200 && map.viewX == 100.0f && map.viewY == 20.0f);
    CHECK(map.minimap == "Test map" && map.song == "Song.ogg");
    CHECK(map.ground.blending == 1.0f && map.ground.textures.size() == 2 && map.ground.textures[1] == "Blue");
    CHECK(map.ground.width == 3 && map.ground.height == 3);
    CHECK(map.ground.values[1] == 0x40 && map.ground.values[8] == 0xff);
    CHECK(map.parts.size() == 3 && map.parts[0].model == "Things/Crate" && map.parts[0].x == 40.5f);
    CHECK(map.parts[1].name == "Away Team Spawn Point 1" && map.parts[1].facing == 90.0f && !map.parts[1].visible);
    CHECK(map.parts[2].renderType == "Black" && map.parts[2].polygon.size() == 5);
    CHECK(map.parts[2].polygon[2].first == 8.0f && map.parts[2].onGround);
}

void testGround() {
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(kMap, kMap + std::string(kMap).size());
    data.files["Cache/Textures/Red.frm16"] = solidFrame(4, 4, 0xf800);
    data.files["Cache/Textures/Blue.frm16"] = solidFrame(4, 4, 0x001f);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=64,64\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    MapScreen screen(data, rules, map, options, 0);
    // The polygon blacks out the corner; value 0 is all the first texture.
    const image::Image &cell = screen.groundCell(0, 0);
    CHECK(cell.w == 64 && cell.pixel(2, 2)[0] == 0);
    CHECK(cell.pixel(0, 32)[0] > 200 && cell.pixel(0, 32)[2] < 50);
    // 0x40: a quarter through the textures, half into the first's band:
    // half of each.
    const image::Image &next = screen.groundCell(1, 0);
    CHECK(next.pixel(0, 0)[0] > 100 && next.pixel(0, 0)[0] < 160);
    CHECK(next.pixel(0, 0)[2] > 100 && next.pixel(0, 0)[2] < 160);
    // 0xff: the last texture alone.
    CHECK(next.pixel(63, 63)[2] > 200 && next.pixel(63, 63)[0] < 60);
    // The camera keeps the view on the map.
    CHECK(screen.cameraX() == 96.0f && screen.cameraY() == 32.0f);
    screen.moveCamera(-50.0f, 70.0f);
    CHECK(screen.cameraX() == 32.0f && screen.cameraY() == 70.0f);
}

void testPaths() {
    // A square's corners, its left side missing: round the other three.
    WayPoints w;
    w.points = {{0, 0, {1}}, {10, 0, {0, 2}}, {10, 10, {1, 3}}, {0, 10, {2}}};
    CHECK(w.nearest(1, 9) == 3 && w.nearest(9, -3) == 1);
    const auto path = w.path(-1, 1, 1, 11);
    CHECK(path.size() == 5 && path[1] == std::make_pair(10.0f, 0.0f) && path[4] == std::make_pair(1.0f, 11.0f));
    // Nowhere to go from a point without lines to the others.
    w.points.push_back({50, 50, {}});
    CHECK(w.path(0, 0, 50, 50).empty());
    CHECK(WayPoints{}.path(0, 0, 5, 5).empty());
    MemoryArchive data;
    WayPoints none;
    CHECK(loadWayPoints(data, "Levels/Missing.way", none));
}

void testTeam() {
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(kMap, kMap + std::string(kMap).size());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=64,64\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember officer;
    officer.firstName = "Ann";
    std::vector<TeamMember> team = {officer};
    MapScreen screen(data, rules, map, options, 0, team);
    // At the first spawn point, facing it its way, chosen.
    CHECK(screen.officers().size() == 1 && screen.selected() == 0);
    CHECK(screen.officers()[0].x == 20.0f && screen.officers()[0].y == 40.0f);
    CHECK(screen.officers()[0].direction == 3);
    // Without waypoints, straight there; without a walk of their model's,
    // at the retail walk's pace: 36.4 pixels a second, facing right.
    screen.order(100.0f, 40.0f);
    screen.update(1000);
    CHECK(std::abs(screen.officers()[0].x - 56.4f) < 0.1f && screen.officers()[0].direction == 6);
    screen.update(4000);
    CHECK(screen.officers()[0].x == 100.0f && screen.officers()[0].path.empty());
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    screen.draw(surface, 4000);
}

void testScripts() {
    // Pan 2 seconds to Pan Here, a line and the team beamed in there, a
    // second's wait; then at once to Home.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=100.000000,100.000000\n"
        "\tCustom Map Properties=CStarTrekLayerSaveData\n\t{\n\t\tCamera Script Manager=CCameraScriptManager\n\t\t{\n"
        "\t\t\tCamera Script Array=Array\n\t\t\t{\n\t\t\t\tArray Count=2\n"
        "\t\t\t\tArray Item=CCameraScript\n\t\t\t\t{\n\t\t\t\t\tWhere To Pan=Pan Here\n\t\t\t\t\tTime To Pan=2\n"
        "\t\t\t\t\tTime To Wait=1\n\t\t\t\t\tPan To Position=1\n"
        "\t\t\t\t\tDestination Action=CMultipleActionsAction\n\t\t\t\t\t{\n\t\t\t\t\t\tAction=Array\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tItem Count=2\n\t\t\t\t\t\t\tAction=CInfoDialogAction\n\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\tScript=Test\n\t\t\t\t\t\t\t\tLine=Hello\n\t\t\t\t\t\t\t\tWho is speaking=Data\n"
        "\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\tAction=CAddEffectToTeam\n\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\tWarp Behavior=CStarTrekTransporterEffectBehavior\n\t\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\t\tWarp Time=1.75\n\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
        "\t\t\t\t}\n"
        "\t\t\t\tArray Item=CCameraScript\n\t\t\t\t{\n\t\t\t\t\tWhere To Pan=Home\n\t\t\t\t\tTime To Pan=3\n"
        "\t\t\t\t\tTime To Wait=0\n\t\t\t\t\tPan To Position=0\n\t\t\t\t\tDestination Action=\n\t\t\t\t}\n"
        "\t\t\t}\n\t\t}\n\t}\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Pan Here\n\t\t\tVisible=0\n\t\t\tPosition X=500\n"
        "\t\t\tPosition Y=100\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Home\n\t\t\tVisible=0\n\t\t\tPosition X=500\n"
        "\t\t\tPosition Y=600\n\t\t}\n\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Levels/Test.RDS"] = std::vector<uint8_t>({':', 'H', 'e', 'l', 'l', 'o', '\n', 'H', 'i', '\n'});
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    CHECK(map.cameraScripts.size() == 2 && map.cameraScripts[0].where == "Pan Here");
    CHECK(map.cameraScripts[0].timeToPan == 2.0f && map.cameraScripts[0].panToPosition);
    CHECK(map.cameraScripts[0].action && map.cameraScripts[0].action->className == "CMultipleActionsAction");
    CHECK(!map.cameraScripts[1].action);
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember officer;
    officer.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {officer});
    // The team is hidden until a script beams them in.
    CHECK(screen.scripted() && !screen.teamShown());
    // Half way through the pan: half way there, eased.
    screen.update(1000);
    CHECK(std::abs(screen.cameraX() - 300.0f) < 0.5f && screen.cameraY() == 100.0f);
    screen.update(2000);
    CHECK(screen.cameraX() == 500.0f && screen.dialogText() == "Hi" && screen.teamShown());
    // The wait, then Home at once, though it takes its Time To Pan.
    screen.update(3001);
    screen.update(3002);
    CHECK(screen.cameraY() == 600.0f && screen.scripted());
    screen.update(6100);
    screen.update(6101);
    CHECK(!screen.scripted());
    // The line goes after its time without a voice-over.
    screen.update(7000);
    CHECK(screen.dialogText().empty());
}

void testTriggers() {
    // An officer walking into Gate's oval relays to Lights, which turns the
    // arrow on and, a second later, off; Gate is done after once.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=100.000000,100.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Gate\n\t\t\tVisible=0\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CTouchingOvalTriggerAI\n\t\t\t\t{\n"
        "\t\t\t\t\tTrigger Only Once=1\n\t\t\t\t\tAuto-Flip Switch=1\n\t\t\t\t\tTriggered By Players=1\n"
        "\t\t\t\t\tEnter Action=CTriggerRelayAction\n\t\t\t\t\t{\n\t\t\t\t\t\tRelay Name=Nobody, Lights\n\t\t\t\t\t}\n"
        "\t\t\t\t\tX Radius=20\n\t\t\t\t\tYX Ratio=0.5\n\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tPosition X=200\n\t\t\tPosition Y=100\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Lights\n\t\t\tVisible=0\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CRelayAI\n\t\t\t\t{\n"
        "\t\t\t\t\tAction=CMultipleActionsAction\n\t\t\t\t\t{\n\t\t\t\t\t\tAction=Array\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tItem Count=2\n\t\t\t\t\t\t\tAction=CActivateAction\n\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\tTarget Name=arrow\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\tAction=CDelayAction\n\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\tNext Action=CDeactivateAction\n\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\tTarget Name=Arrow\n"
        "\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t\tDelay=1\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
        "\t\t\t\t\tTrigger Only Once=0\n\t\t\t\t}\n\t\t\t}\n\t\t}\n"
        "\t\tLevel Part=CRenderablePolygon\n\t\t{\n\t\t\tName=Arrow\n\t\t\tVisible=1\n\t\t\tActive=0\n"
        "\t\t\tPolygon=10, 10, 20, 10, 20, 20, 10\n\t\t\tRender Type=Trans Green\n\t\t}\n"
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    CHECK(map.parts.size() == 3 && map.parts[0].activities.size() == 1 && !map.parts[2].active);
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember officer;
    officer.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {officer});
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    // Not there yet: nothing.
    screen.order(150.0f, 100.0f);
    screen.update(1000);
    CHECK(!screen.partState(2).active);
    // In the oval: the arrow on, the gate off.
    screen.order(195.0f, 103.0f);
    screen.update(4000);
    CHECK(screen.partState(2).active && !screen.partState(0).active);
    screen.draw(surface, 4000);
    // A second later, the arrow off; going out and back doesn't set it off
    // again.
    screen.update(5100);
    CHECK(!screen.partState(2).active);
    screen.order(100.0f, 100.0f);
    screen.update(9000);
    screen.order(200.0f, 100.0f);
    screen.update(13000);
    CHECK(!screen.partState(2).active);
    // A relay run by hand, as any action would.
    TextObject relay;
    CHECK(!readTextObject("CTriggerRelayAction\n{\n\tRelay Name=Lights\n}\n", relay));
    screen.runAction(&relay);
    CHECK(screen.partState(2).active);
}

void testEnemies() {
    // A guard at (500, 500) facing left walks its route's two corners, then
    // looks about; an officer coming into its sight alerts it, which sounds
    // the siren, and a second later it attacks.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=900.000000,900.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tPatrol=CBorgStatePatrol\n\t\t\t\t\t{\n\t\t\t\t\t\tPatrol AI=CSeriesAI\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tAIs=Array\n\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tItem Count=2\n"
        "\t\t\t\t\t\t\t\tAI=CFollowPathAI\n\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\tPath Poly=Route\n"
        "\t\t\t\t\t\t\t\t\tOrder=Go To End\n\t\t\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t\t\tAI=CScanAreaAI\n\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\tStarting Angle=0\n"
        "\t\t\t\t\t\t\t\t\tEnding Angle=-30\n\t\t\t\t\t\t\t\t\tTurning Speed=10\n"
        "\t\t\t\t\t\t\t\t\tStarting Facing Angle=-999\n\t\t\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
        "\t\t\t\t\tAlert=CBorgStateAlert\n\t\t\t\t\t{\n\t\t\t\t\t\tAlert Time=1\n"
        "\t\t\t\t\t\tStart Action=CActivateAction\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tTarget Name=Siren\n\t\t\t\t\t\t}\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t\tMax Distance=100\n\t\t\t\t\tVision Cone=90\n\t\t\t\t\tCurrent State=Patrol\n"
        "\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=500\n\t\t\tFacing Angle=0\n\t\t}\n"
        "\t\tLevel Part=CFreeRangePoly\n\t\t{\n\t\t\tName=Route\n\t\t\tVisible=1\n"
        "\t\t\tPolygon=500, 500, 400, 500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Siren\n\t\t\tVisible=0\n\t\t\tActive=0\n\t\t}\n"
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember officer;
    officer.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {officer});
    CHECK(screen.enemies().size() == 1 && screen.enemies()[0].state == "Patrol");
    // At the route's first corner, where it stands; then along it at the
    // retail walk's pace: a second, 36 pixels.
    screen.update(1000);
    screen.update(2000);
    CHECK(std::abs(screen.enemies()[0].x - 463.6f) < 0.5f && screen.enemies()[0].walking);
    // At the route's end, then looking about: 3 seconds turn it 30 degrees.
    screen.update(4000);
    CHECK(screen.enemies()[0].x == 400.0f);
    screen.update(4001);
    screen.update(4002);
    screen.update(7100);
    CHECK(std::abs(screen.enemies()[0].angle + 0.5236f) < 0.01f);
    // An officer just to its left, in its sight: alerted, the siren on.
    screen.order(350.0f, 500.0f);
    screen.update(60000);
    CHECK(screen.enemies()[0].state == "Alert" && screen.partState(2).active);
    screen.update(61500);
    CHECK(screen.enemies()[0].state == "Attack" && screen.enemies()[0].target == 0);
}

void testDrawingOrder() {
    // A red wall at (100, 100) cut at its middle, its right strip sorted
    // 100 further down; a blue crate standing at (100, 150) lifted 50 by its
    // Rendering Height, so drawn over the wall: the wall's left strip under
    // it, its right over it.
    const std::string zax = "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=100.000000,100.000000\n"
                            "\tTree List=CSortList2D\n\t{\n"
                            "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tVisible=1\n\t\t\tModel=Things/Wall\n"
                            "\t\t\tPosition X=100\n\t\t\tPosition Y=100\n\t\t\tRendering Height=0\n\t\t}\n"
                            "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tVisible=1\n\t\t\tModel=Things/Crate\n"
                            "\t\t\tPosition X=100\n\t\t\tPosition Y=150\n\t\t\tRendering Height=50\n\t\t}\n"
                            "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    ModelLook wall;
    wall.colour = 0xf800;
    wall.size = 20;
    wall.cuts = {{0, 100}};
    data.files["Cache/Models/Things/Wall.mdl16"] = modelFile("Wall", 1, 1, 0.0f, {}, 0.0f, false, 0.0f, wall);
    ModelLook crate;
    crate.colour = 0x001f;
    crate.size = 40;
    data.files["Cache/Models/Things/Crate.mdl16"] = modelFile("Crate", 1, 1, 0.0f, {}, 0.0f, false, 0.0f, crate);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    MapScreen screen(data, rules, map, options, 0);
    CHECK(screen.cameraX() == 100.0f && screen.cameraY() == 100.0f);
    Surface surface;
    surface.create(200, 200, PixelFormat::kRGBA32);
    screen.draw(surface, 0);
    const auto pixel = [&](int x, int y) { return surface.row(y) + x * 4; };
    CHECK(pixel(95, 100)[2] > 200 && pixel(95, 100)[0] < 50);
    CHECK(pixel(105, 100)[0] > 200 && pixel(105, 100)[2] < 50);
    CHECK(pixel(85, 100)[2] > 200 && pixel(100, 150)[2] < 50);
}

void testPauseWhenHit() {
    // Two officers, Ann at Away Team Spawn Point 1 in the view's middle,
    // Bea at her own spawn point far off it, not at point 2. The first
    // hit stops time and runs the First Hit Action, once; after it, a hit
    // in the view doesn't, and one out of it does.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,500.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=0\n"
        "\t\t\tPosition X=500\n\t\t\tPosition Y=500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=bea Spawn Point\n\t\t\tVisible=0\n"
        "\t\t\tPosition X=1800\n\t\t\tPosition Y=1800\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 2\n\t\t\tVisible=0\n"
        "\t\t\tPosition X=520\n\t\t\tPosition Y=520\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Helped\n\t\t\tVisible=1\n\t\t\tActive=0\n\t\t}\n"
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n\tFirst Hit Action=CActivateAction\n\t{\n"
                          "\t\tTarget Name=Helped\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bea;
    bea.firstName = "Bea";
    MapScreen screen(data, rules, map, options, 0, {ann, bea});
    CHECK(screen.officers()[1].x == 1800.0f);
    screen.officerHit(0);
    CHECK(screen.paused() && !options.showPauseHelp && screen.partState(3).active);
    screen.setPaused(false);
    screen.officerHit(0);
    CHECK(!screen.paused());
    screen.officerHit(1);
    CHECK(screen.paused());
    options.pauseWhenHit = false;
    screen.setPaused(false);
    screen.officerHit(1);
    CHECK(!screen.paused());
}

void testFoes() {
    // A freed prisoner (Electronic Equipment, after Enemy) with a disruptor
    // sees a guard (Enemy, after the team) 200 away: alerted, then it shoots
    // the guard, which doesn't shoot back, its Valid Targets not naming
    // the prisoner.
    const auto guard = [](const std::string &name, int x, const std::string &category, const std::string &targets,
                          const std::string &weapon) {
        return "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=" + name +
               "\n\t\t\tVisible=1\n\t\t\tActive=1\n\t\t\tCategory=" + category +
               "\n\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
               "\t\t\t\t\tAlert=CBorgStateAlert\n\t\t\t\t\t{\n\t\t\t\t\t\tAlert Time=0.5\n\t\t\t\t\t}\n"
               "\t\t\t\t\tInactive=CBorgStateInactive\n\t\t\t\t\t{\n\t\t\t\t\t\tPlaySequenceBehavior=\n"
               "\t\t\t\t\t\t{\n\t\t\t\t\t\t\tSequence=shutdown\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
               "\t\t\t\t\tVision Cone=360\n\t\t\t\t\tTargeting=CBorgSetClosestTarget\n\t\t\t\t\t{\n"
               "\t\t\t\t\t\tValid Targets=" +
               targets + "\n\t\t\t\t\t}\n\t\t\t\t\tCurrent State=Patrol\n\t\t\t\t}\n\t\t\t}\n" +
               (weapon.empty() ? std::string()
                               : "\t\t\tInventory=CInventory\n\t\t\t{\n\t\t\t\tInventory List=Array\n\t\t\t\t{\n"
                                 "\t\t\t\t\tItem Count=1\n\t\t\t\t\tItem=CStarTrekEnergyWeaponInventoryItem\n"
                                 "\t\t\t\t\t{\n\t\t\t\t\t\tDefinition Index=" +
                                     weapon + "\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n") +
               "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=" + std::to_string(x) +
               "\n\t\t\tPosition Y=500\n\t\t}\n";
    };
    const std::string zax = "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,500.000000\n"
                            "\tTree List=CSortList2D\n\t{\n" +
                            guard("Prisoner", 400, "Electronic Equipment", "Enemy", "Disruptor") +
                            guard("Guard", 600, "Enemy", "Player,Team Member", "Disruptor") + "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1, 100.0f);
    const std::string disruptor =
        "CStarTrekInventoryItemDefinition\n{\n\tWeapon Damage=30\n\tReuse Delay=1\n"
        "\tItem Range=375\n\tMax Shoot Angle=10\n\tDischarge Action=CShootEnergyWeaponAction\n}\n";
    data.files["Inventory/Item Definitions/Disruptor.txt"] = std::vector<uint8_t>(disruptor.begin(), disruptor.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    MapScreen screen(data, rules, map, options, 0);
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    screen.update(now += 50);
    CHECK(enemies[0].state == "Alert" && enemies[0].foe == 1 && enemies[1].state == "Patrol");
    while (now < 3000)
        screen.update(now += 50);
    CHECK(enemies[0].state == "Attack" && enemies[1].health < 100.0f && enemies[1].foe == -1);
    // Deactivated, the prisoner shuts down and stops; activated, it's back
    // in the state it was in.
    TextObject off;
    CHECK(!readTextObject("CSendDeactivateAIMessage\n{\nTarget=Prisoner\n}\n", off));
    screen.runAction(&off);
    CHECK(enemies[0].state == "Inactive" && enemies[0].foe == -1);
    const float health = enemies[1].health;
    while (now < 6000)
        screen.update(now += 50);
    CHECK(enemies[0].state == "Inactive" && enemies[1].health == health);
    TextObject on;
    CHECK(!readTextObject("CSendActivateAIMessage\n{\nTarget=Prisoner\n}\n", on));
    screen.runAction(&on);
    CHECK(enemies[0].state == "Attack");
    // Made to investigate, the guard looks about where it is.
    TextObject force;
    CHECK(!readTextObject("CForceStateChangeAction\n{\nTarget=Guard\nState=Investigate\n}\n", force));
    screen.runAction(&force);
    CHECK(enemies[1].state == "Investigate");
}

void testHostage() {
    // Bea's spawn point lays her out and gives her a process for a
    // hypospray, then takes her off the team: she stays where she is, a
    // part in her place. Ann with a hypospray uses it on her: she walks
    // over, uses one up, and Bea's back on the team where she lay.
    const std::string spawn =
        "Spawn Action=CMultipleActionsAction\n{\nAction=Array\n{\nItem Count=3\n"
        "Action=CAddAIAction\n{\nTarget Name=$instigator\nAI=CTemporaryTaskAI\n{\n"
        "Temporary AI=CBehaviorActivity\n{\nBehavior Queue=CEntityBehaviorQueue\n{\nBehavior=Array\n{\n"
        "Item Count=1\nBehavior=CEntityBehaviorPlaySequenceOnce\n{\nSequence=dead\nDone Sequence=dead\n}\n}\n}\n}\n}\n}\n"
        "Action=CAddAIAction\n{\nTarget Name=$Instigator\nAI=CProcessListAI\n{\nProcess=Array\n{\nItem Count=1\n"
        "Process=CProcess\n{\nProcess Type=Hypospray Use\nRequired Selected Item=Hypospray\n"
        "Required Item In Inventory=Hypospray\nProcess Activity=Array\n{\nItem Count=1\n"
        "Process Activity=CGotoRadiusAI\n{\nDestination=$trigger\nRadius=30\nDone action=CMultipleActionsAction\n{\n"
        "Action=Array\n{\nItem Count=2\nAction=CUseInventoryItemAction\n{\nInventory Item Definition=Hypospray\n"
        "Count=1\n}\nAction=CAddToTeamAction\n{\nWho To Add=$trigger\n}\n}\n}\n}\n}\n}\n}\n}\n}\n"
        "Action=CRemoveFromTeamAction\n{\nWho To Remove=$Instigator\n}\n}\n}\n";
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,500.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nVisible=0\nPosition X=400\nPosition Y=500\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 2\nVisible=0\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CSpawnPointAI\n{\n" +
        spawn + "}\n}\nPosition X=600\nPosition Y=500\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Cache/Models/Characters/Body.mdl16"] = modelFile("Body", 1, 1);
    const std::string hypospray = "CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\n"
                                  "Discharge Action=CGiveHealthWeaponAction\n{\nHealth action=CGiveHealthAction\n{\n"
                                  "Health=30\n}\n}\n}\n";
    data.files["Inventory/Item Definitions/Hypospray.txt"] = std::vector<uint8_t>(hypospray.begin(), hypospray.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Hypospray", 3}};
    TeamMember bea;
    bea.firstName = "Bea";
    bea.bodyModel = "Characters/Body";
    MapScreen screen(data, rules, map, options, 0, {ann, bea});
    const auto &officers = screen.officers();
    float x = 0.0f;
    float y = 0.0f;
    CHECK(!officers[1].present && officers[1].standIn >= 0 && screen.partPosition("Bea", x, y) && x == 600.0f);
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(600.0f, 500.0f));
    uint64_t now = 0;
    while (now < 10000 && !officers[1].present)
        screen.update(now += 50);
    CHECK(officers[1].present && officers[1].x == 600.0f && officers[0].items[0].count == 2);
    CHECK(screen.partState(size_t(2)).deleted);
}

void testClosest() {
    // A guard seeing all round, Ann 80 away and Bea 40: it goes for Bea,
    // the nearer, though Ann's first.
    const std::string zax = "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=500.000000,500.000000\n"
                            "\tTree List=CSortList2D\n\t{\n"
                            "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
                            "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
                            "\t\t\t\t\tAlert=CBorgStateAlert\n\t\t\t\t\t{\n\t\t\t\t\t\tAlert Time=1\n\t\t\t\t\t}\n"
                            "\t\t\t\t\tVision Cone=360\n\t\t\t\t\tCurrent State=Patrol\n"
                            "\t\t\t\t}\n\t\t\t}\n"
                            "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=500\n\t\t}\n"
                            "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=0\n"
                            "\t\t\tPosition X=580\n\t\t\tPosition Y=500\n\t\t}\n"
                            "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 2\n\t\t\tVisible=0\n"
                            "\t\t\tPosition X=500\n\t\t\tPosition Y=460\n\t\t}\n"
                            "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bea;
    bea.firstName = "Bea";
    MapScreen screen(data, rules, map, options, 0, {ann, bea});
    CHECK(screen.officers()[1].y == 460.0f);
    screen.update(100);
    CHECK(screen.enemies()[0].state == "Alert" && screen.enemies()[0].target == 1);
}

void testCombat() {
    // A guard at (500, 500) facing left with a disruptor, a wall between
    // it and the officers at (300, 500), 20 x 80 about (400, 500). Two
    // touching triggers about (420, 548), on the guard's line to (300,
    // 620): one answering to projectiles, one to nothing.
    const auto shotTrigger = [](const std::string &by, const std::string &lamp) {
        return "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tActive=1\n\t\t\tPosition X=420\n\t\t\tPosition Y=548\n"
               "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CTouchingOvalTriggerAI\n"
               "\t\t\t\t{\n\t\t\t\t\tX Radius=15\n\t\t\t\t\tYX Ratio=1\n\t\t\t\t\t" +
               by + "\n\t\t\t\t\tEnter Action=CActivateAction\n\t\t\t\t\t{\n\t\t\t\t\t\tTarget Name=" + lamp +
               "\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=300.000000,500.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tAlert=CBorgStateAlert\n\t\t\t\t\t{\n\t\t\t\t\t\tAlert Time=0.5\n\t\t\t\t\t}\n"
        "\t\t\t\t\tMax Distance=300\n\t\t\t\t\tVision Cone=180\n\t\t\t\t\tCurrent State=Patrol\n"
        "\t\t\t\t\tTarget If Hit=1\n"
        "\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tInventory=CInventory\n\t\t\t{\n\t\t\t\tInventory List=Array\n\t\t\t\t{\n"
        "\t\t\t\t\tItem Count=1\n\t\t\t\t\tItem=CStarTrekEnergyWeaponInventoryItem\n\t\t\t\t\t{\n"
        "\t\t\t\t\t\tDefinition Index=Enemy Weapons/Disruptor\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=500\n\t\t\tFacing Angle=0\n"
        "\t\t\tDestroyed Action=CActivateAction\n\t\t\t{\n\t\t\t\tTarget Name=Trophy\n\t\t\t}\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Wall\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tModel=Walls/Wall\n\t\t\tPosition X=400\n\t\t\tPosition Y=500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CSpawnPointAI\n\t\t\t\t{\n"
        "\t\t\t\t\tSpawn Action=CAddMissionObjectiveAction\n\t\t\t\t\t{\n"
        "\t\t\t\t\t\tObjective=CProtectMissionObjective\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tObjective Name=Protect\n\t\t\t\t\t\t\tMission Status=Incomplete\n"
        "\t\t\t\t\t\t\tDisplay Status=Hidden\n\t\t\t\t\t\t\tWhoToProtect=$instigator\n"
        "\t\t\t\t\t\t\tFail Action=CActivateAction\n\t\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\t\tTarget Name=Flag\n\t\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tPosition X=300\n\t\t\tPosition Y=500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Flag\n\t\t\tVisible=0\n\t\t\tActive=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Trophy\n\t\t\tVisible=0\n\t\t\tActive=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 2\n\t\t\tVisible=1\n"
        "\t\t\tPosition X=300\n\t\t\tPosition Y=480\n\t\t}\n" +
        shotTrigger("Triggered By Projectiles=1", "Shot Lamp") + shotTrigger("", "Other Lamp") +
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Shot Lamp\n\t\t\tActive=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Other Lamp\n\t\t\tActive=0\n\t\t}\n"
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1, 50.0f);
    data.files["Cache/Models/Walls/Wall.mdl16"] =
        modelFile("Wall", 1, 1, 0.0f, {{-10, -40}, {10, -40}, {10, 40}, {-10, 40}, {-10, -40}});
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    data.files["Inventory/Item Definitions/Enemy Weapons/Disruptor.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tWeapon Damage=30\n\tReuse Delay=1\n\tItem Range=375\n"
                "\tMax Shoot Angle=10\n\tDischarge Action=CShootEnergyWeaponAction\n}\n");
    data.files["Inventory/Item Definitions/Phaser.txt"] = bytesOf(
        "CStarTrekInventoryItemDefinition\n{\n\tWeapon Damage=25\n\tReuse Delay=0.5\n\tItem Range=375\n"
        "\tDischarge Action=CShootEnergyWeaponAction\n\tDefault Inventory Item=CStarTrekEnergyWeaponInventoryItem\n\t{\n\t\tWeapon Setting=Kill\n"
        "\t}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bo;
    bo.firstName = "Bo";
    bo.inventory = {{"Phaser", 1}};
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    // The wall's footprint, about where it stands, hides one side from the
    // other; past its end there's a clear line.
    CHECK(!screen.clearSight(300.0f, 500.0f, 500.0f, 500.0f));
    CHECK(screen.clearSight(300.0f, 600.0f, 500.0f, 500.0f));
    const MapScreen::Enemy &guard = screen.enemies()[0];
    CHECK(guard.health == 50.0f && guard.weapon && guard.weapon->weaponDamage == 30.0f);
    CHECK(screen.officers()[0].health == 100.0f && !screen.officers()[0].weapon && screen.officers()[1].weapon);
    // Behind the wall, unseen.
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    run(2000);
    CHECK(guard.state == "Patrol");
    // Ann walks out past the wall's end: seen, alerted, and shot each
    // second, 30 a shot, till she falls and can't be chosen.
    screen.choose(0);
    screen.order(300.0f, 620.0f);
    run(6000);
    CHECK(guard.state == "Attack" && guard.target == 0);
    CHECK(screen.officers()[0].health < 100.0f);
    // Its shots cross the triggers: the one answering to projectiles goes.
    CHECK(screen.partState(8).active && !screen.partState(9).active);
    run(12000);
    CHECK(!screen.officers()[0].alive() && screen.officers()[0].health == 0.0f && !screen.officers()[0].chosen);
    screen.choose(0);
    CHECK(screen.selected() == -1);
    // Her spawn point's Spawn Action made an objective to protect her: it
    // has failed, and run its Fail Action.
    CHECK(screen.objectives().size() == 1 && screen.objectives()[0].officer == 0);
    CHECK(screen.objectives()[0].status == ObjectiveStatus::kFailed && screen.partState(3).active);
    // With her down it goes back to its patrol; Bo, sent to shoot it,
    // walks towards it till he sees it, and two of his phaser's shots kill
    // it.
    CHECK(screen.enemyAt(500.0f, 500.0f) == 0);
    screen.choose(1);
    screen.attack(0);
    run(20000);
    CHECK(!guard.alive() && guard.state == "Dead" && guard.health == 0.0f && screen.partState(4).active);
    CHECK(screen.enemyAt(500.0f, 500.0f) == -1 && screen.officers()[1].attacking == -1);
}

void testNoise() {
    // A guard at (500, 500) facing right, standing about on patrol, that
    // listens; an officer far off, out of its sight.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=500.000000,500.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tPatrol=CBorgStatePatrol\n\t\t\t\t\t{\n\t\t\t\t\t\tPatrol AI=CIdleAI\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
        "\t\t\t\t\tInvestigate=CBorgStateInvestigate\n\t\t\t\t\t{\n"
        "\t\t\t\t\t\tInvestigate AI=CChaseInvestigateAI\n\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tTime To Investigate=2\n\t\t\t\t\t\t\tRun To Investigate=0\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\tStart Action=CActivateAction\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tTarget Name=Lamp\n"
        "\t\t\t\t\t\t}\n\t\t\t\t\t}\n"
        "\t\t\t\t\tMax Distance=50\n\t\t\t\t\tVision Cone=90\n\t\t\t\t\tCurrent State=Patrol\n"
        "\t\t\t\t\tInvestigate Sounds=1\n"
        "\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=500\n"
        "\t\t\tFacing Angle=3.14159\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Lamp\n\t\t\tVisible=0\n\t\t\tActive=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=1\n"
        "\t\t\tPosition X=100\n\t\t\tPosition Y=900\n\t\t}\n"
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t\tCrouch Noise Modifier=0.25\n\t\tWalk Noise Modifier=3\n"
                          "\t\tRun Noise Modifier=6\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.attributes = {{"Stealth", 30}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    // A footstep's 100, by her Stealth 30 (0.85) and how she moves.
    const Animation::Noise step{2, 100.0f, 1.0f, "Stealth", {}};
    CHECK(std::abs(screen.footstepNoise(screen.officers()[0], step) - 255.0f) < 0.01f);
    screen.setMove(1);
    CHECK(std::abs(screen.footstepNoise(screen.officers()[0], step) - 510.0f) < 0.01f);
    screen.setMove(2);
    CHECK(std::abs(screen.footstepNoise(screen.officers()[0], step) - 21.25f) < 0.01f);
    // 200 above it is heard as 281 away: not with a radius of 250.
    const MapScreen::Enemy &guard = screen.enemies()[0];
    screen.makeNoise(500.0f, 300.0f, 250.0f, 1.0f);
    screen.update(100);
    CHECK(guard.state == "Patrol" && screen.noises().size() == 1);
    // A noise 100 to its left is heard: it goes to look, the Start Action
    // run; the first noise is gone after its second.
    screen.makeNoise(400.0f, 500.0f, 150.0f, 1.0f);
    screen.update(200);
    CHECK(guard.state == "Investigate" && screen.partState(1).active);
    uint64_t now = 200;
    while (now < 2500)
        screen.update(now += 50);
    CHECK(screen.noises().empty() && guard.x < 500.0f && guard.x > 400.0f && guard.walking);
    // There after 100 pixels at the walk's pace, then two seconds looking
    // about, then back on patrol.
    while (now < 3500)
        screen.update(now += 50);
    CHECK(guard.x == 400.0f && guard.state == "Investigate");
    while (now < 6000)
        screen.update(now += 50);
    CHECK(guard.state == "Patrol");
    // The sound button shows the noises.
    CHECK(!screen.listening());
    screen.setListening(true);
    CHECK(screen.listening());
}

void testObjectives() {
    // A mission's objectives: two scientists to rescue, required; a band to
    // find, hidden and not. Its Win Action ends it.
    const std::string mds =
        "CMissionDescription\n{\n\tObjective=CMissionObjectiveManager\n\t{\n\t\tBase Score for Mission=100\n"
        "\t\tWin Action=CDisplayMissionCompletedScreenAction\n\t\t{\n\t\t\tScript=Mission Ending Messages\n"
        "\t\t\tMission Over Text=Both Scientists \n\t\t}\n"
        "\t\tObjectives=Array\n\t\t{\n\t\t\tItem Count=2\n"
        "\t\t\tObjective=CCountMissionObjective\n\t\t\t{\n\t\t\t\tObjective Name=Rescue Scientists\n"
        "\t\t\t\tMission Status=Incomplete\n\t\t\t\tDisplay Status=Visible\n"
        "\t\t\t\tRequired To End Mission=1\n\t\t\t\tObjective Script=Mission Objectives\n"
        "\t\t\t\tObjective Line=Rescue the two\n\t\t\t\tCount Before Success=2\n"
        "\t\t\t\tCurrent Count=0\n\t\t\t}\n"
        "\t\t\tObjective=CStandardMissionObjective\n\t\t\t{\n\t\t\t\tObjective Name=Band\n"
        "\t\t\t\tMission Status=Incomplete\n\t\t\t\tDisplay Status=Hidden\n"
        "\t\t\t\tRequired To End Mission=0\n\t\t\t}\n\t\t}\n\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(kMap, kMap + std::string(kMap).size());
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    data.files["Levels/Mission 01/Mission 01.MDS"] = bytesOf(mds);
    data.files["Levels/Mission Objectives.RDS"] =
        bytesOf(":Rescue the two\r\nRescue the two scientists. (%d of 2)\r\n");
    data.files["Levels/Mission Ending Messages.RDS"] =
        bytesOf(":Both Scientists \r\nBoth scientists have been beamed aboard.\r\n");
    Mission mission;
    CHECK(!loadMission(data, 1, mission) && mission.objectives);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=64,64\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {ann});
    screen.setObjectives(readMissionObjectives(mission.objectives));
    const auto &objectives = screen.objectives();
    CHECK(objectives.size() == 2 && objectives[0].required && objectives[0].needed == 2);
    CHECK(!objectives[1].visible && !objectives[1].required);
    CHECK(screen.objectiveText(objectives[0]) == "Rescue the two scientists. (0 of 2)");
    // Each count adds one; at two it's complete, and with the only
    // required one complete, the Win Action ends the mission.
    TextObject count;
    CHECK(!readTextObject("CSetMissionObjectiveCountAction\n{\n\tObjective Name=Rescue Scientists\n"
                          "\tCount=1\n}\n",
                          count));
    screen.runAction(&count);
    CHECK(objectives[0].count == 1 && objectives[0].status == ObjectiveStatus::kIncomplete && !screen.missionOver());
    CHECK(screen.objectiveText(objectives[0]) == "Rescue the two scientists. (1 of 2)");
    TextObject next;
    CHECK(!readTextObject("CSetNextMissionAction\n{\n\tNext Mission=Mission 02/Mission 02\n}\n", next));
    screen.runAction(&next);
    screen.runAction(&count);
    CHECK(objectives[0].status == ObjectiveStatus::kComplete && screen.missionOver() && screen.missionCompleted());
    CHECK(screen.missionOverText() == "Both scientists have been beamed aboard.");
    CHECK(screen.nextMission() == "Mission 02/Mission 02");
    // The game's held still meanwhile; Esc doesn't leave it.
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    screen.draw(surface, 100);
    Event esc;
    esc.type = EventType::kKeyDown;
    esc.key = key::kEscape;
    CHECK(screen.handleEvent(esc, nullptr, 200) == MapScreen::Choice::kNone && screen.missionOver());
}

void testEnemyAI() {
    // A guard that sees Ann calls the guards about it to look where she is;
    // its gun's set to stun. A guard to send after Bo, one that retreats to
    // its alarm when it's hurt, and their places.
    const auto guard = [](const std::string &name, int x, int y, float angle, const std::string &ai,
                          const std::string &rest) {
        return "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=" + name +
               "\n\t\t\tVisible=1\n\t\t\tActive=1\n\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n"
               "\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n\t\t\t\t\tCurrent State=Patrol\n" +
               ai + "\t\t\t\t}\n\t\t\t}\n" + rest +
               "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=" + std::to_string(x) +
               "\n\t\t\tPosition Y=" + std::to_string(y) + "\n\t\t\tFacing Angle=" + std::to_string(angle) +
               "\n\t\t}\n";
    };
    const auto place = [](const std::string &name, int x, int y, bool active) {
        return "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=" + name +
               "\n\t\t\tVisible=1\n\t\t\tActive=" + (active ? "1" : "0") + "\n\t\t\tPosition X=" + std::to_string(x) +
               "\n\t\t\tPosition Y=" + std::to_string(y) + "\n\t\t}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,500.000000\n"
        "\tTree List=CSortList2D\n\t{\n" +
        guard("Guard", 500, 500, 0.0f,
              "\t\t\t\t\tMax Distance=300\n\t\t\t\t\tVision Cone=180\n"
              "\t\t\t\t\tAlert=CBorgStateAlert\n\t\t\t\t\t{\n\t\t\t\t\t\tAlert Time=0.5\n"
              "\t\t\t\t\t\tStart Action=CDoOnAllWithinRadiusAction\n\t\t\t\t\t\t{\n"
              "\t\t\t\t\t\t\tTarget Entity=$Trigger\n"
              "\t\t\t\t\t\t\tTouching Action=CSendInvestigateHereMessageAction\n\t\t\t\t\t\t\t{\n"
              "\t\t\t\t\t\t\t\tTarget=$Trigger\n\t\t\t\t\t\t\t}\n"
              "\t\t\t\t\t\t\tWho To Ignore=Guard 4\n\t\t\t\t\t\t\tName Filter=guard\n"
              "\t\t\t\t\t\t\tWho To Affect=Enemy\n\t\t\t\t\t\t\tRadius=400\n\t\t\t\t\t\t\tY To X Ratio=1\n"
              "\t\t\t\t\t\t}\n\t\t\t\t\t}\n",
              "\t\t\tInventory=CInventory\n\t\t\t{\n\t\t\t\tInventory List=Array\n\t\t\t\t{\n"
              "\t\t\t\t\tItem Count=1\n\t\t\t\t\tItem=CStarTrekEnergyWeaponInventoryItem\n\t\t\t\t\t{\n"
              "\t\t\t\t\t\tDefinition Index=Enemy Weapons/Borg Gun\n\t\t\t\t\t\tWeapon Setting=Stun\n"
              "\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n") +
        guard("Guard 2", 500, 800, 3.14159f,
              "\t\t\t\t\tMax Distance=50\n\t\t\t\t\tInvestigate=CBorgStateInvestigate\n\t\t\t\t\t{\n"
              "\t\t\t\t\t\tInvestigate AI=CChaseInvestigateAI\n\t\t\t\t\t\t{\n"
              "\t\t\t\t\t\t\tTime To Investigate=1\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n",
              "") +
        guard("Guard 3", 900, 900, 0.0f,
              "\t\t\t\t\tMax Distance=50\n\t\t\t\t\tPursue=CBorgStatePursue\n\t\t\t\t\t{\n"
              "\t\t\t\t\t\tMove Mode=Run\n\t\t\t\t\t\tPursue AI=CChasePursueAI\n\t\t\t\t\t\t{\n"
              "\t\t\t\t\t\t}\n\t\t\t\t\t}\n",
              "") +
        guard("Guard 4", 500, 200, 3.14159f,
              "\t\t\t\t\tMax Distance=50\n\t\t\t\t\tRetreat At Percent=80\n"
              "\t\t\t\t\tRetreat=CBorgStateRetreat\n\t\t\t\t\t{\n\t\t\t\t\t\tMove Mode=Run\n"
              "\t\t\t\t\t\tRetreat AI=CGoToAI\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tDestination=Alarm Spot\n"
              "\t\t\t\t\t\t\tDone action=CActivateAction\n\t\t\t\t\t\t\t{\n"
              "\t\t\t\t\t\t\t\tTarget Name=Alarm\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n",
              "") +
        place("Alarm Spot", 900, 200, true) + place("Alarm", 0, 0, false) +
        place("Away Team Spawn Point 1", 350, 500, true) + place("Away Team Spawn Point 2", 300, 200, true) +
        "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    data.files["Inventory/Item Definitions/Enemy Weapons/Borg Gun.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tWeapon Damage=40\n\tReuse Delay=1\n\tItem Range=375\n"
                "\tMax Shoot Angle=10\n\tStun Duration=5\n\tDischarge Action=CShootEnergyWeaponAction\n}\n");
    data.files["Inventory/Item Definitions/Phaser.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tWeapon Damage=25\n\tReuse Delay=0.5\n\tItem Range=375\n"
                "\tDischarge Action=CShootEnergyWeaponAction\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    CHECK(map.parts[0].stuns && !map.parts[1].stuns);
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bo;
    bo.firstName = "Bo";
    bo.inventory = {{"Phaser", 1}};
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    // The guard sees Ann: its alert calls Guard 2, in its radius, to look
    // where she is, but not Guard 3, out of it, nor Guard 4, ignored. Then
    // it shoots her with its stun gun: down, unhurt, and she can't walk.
    run(1000);
    CHECK(enemies[1].state == "Investigate" && enemies[1].heardX == 350.0f && enemies[1].heardY == 500.0f);
    CHECK(enemies[2].state == "Patrol" && enemies[3].state == "Patrol");
    const MapScreen::Officer &a = screen.officers()[0];
    CHECK(a.stunned > 0.0f && a.health == 100.0f && a.alive());
    screen.choose(0);
    screen.order(350.0f, 700.0f);
    run(2000);
    CHECK(a.x == 350.0f && a.y == 500.0f);
    // Bo shoots Guard 4 once: at 75 of its 100 it retreats, running to its
    // Alarm Spot, sets off the alarm there and holds the place.
    screen.choose(1);
    screen.attack(3);
    run(2050);
    CHECK(enemies[3].health == 75.0f && enemies[3].state == "Retreat");
    screen.order(300.0f, 150.0f);
    run(16000);
    CHECK(enemies[3].retreated && enemies[3].state == "Attack" && enemies[3].x == 900.0f);
    CHECK(screen.partState(5).active);
    // Guard 3, sent after Bo, goes after him though it doesn't see him.
    TextObject message;
    CHECK(!readTextObject("CSendInvestigateTargetMessageAction\n{\n\tTarget=Guard 3\n}\n", message));
    screen.runAction(&message, 1);
    CHECK(enemies[2].state == "Attack" && enemies[2].target == 1);
    const float before = std::hypot(enemies[2].x - 300.0f, enemies[2].y - 150.0f);
    run(19000);
    CHECK(enemies[2].state == "Attack" && enemies[2].walking && enemies[2].running);
    CHECK(std::hypot(enemies[2].x - 300.0f, enemies[2].y - 150.0f) < before - 50.0f);
}

void testItems() {
    // Ann carries two concussion grenades, a frag grenade and a hypospray;
    // Bo stands beside her. A guard 300 off, looking away, and one far off.
    const auto part = [](const std::string &name, int x, int y, const std::string &rest) {
        return "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=" + name + "\n\t\t\tVisible=1\n\t\t\tActive=1\n" +
               rest + "\t\t\tPosition X=" + std::to_string(x) + "\n\t\t\tPosition Y=" + std::to_string(y) + "\n\t\t}\n";
    };
    const std::string guardAI =
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tCurrent State=Patrol\n\t\t\t\t\tMax Distance=50\n\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tModel=Characters/Guard\n\t\t\tFacing Angle=3.14159\n";
    const std::string zax = "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,500.000000\n"
                            "\tTree List=CSortList2D\n\t{\n" +
                            part("Guard A", 600, 500, guardAI) + part("Guard B", 1200, 500, guardAI) +
                            part("Away Team Spawn Point 1", 300, 500, "") +
                            part("Away Team Spawn Point 2", 300, 420, "") + "\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Cache/Models/Shots/Concussion.mdl16"] =
        modelFile("Concussion", 1, 1, 50.0f, {}, 150.0f, true, 400.0f, grenadeLook());
    data.files["Cache/Models/Shots/Frag.mdl16"] =
        modelFile("Frag", 1, 1, 50.0f, {}, 60.0f, false, 400.0f, grenadeLook());
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto grenade = [&](const std::string &damage, const std::string &projectile) {
        return bytesOf(
            "CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tWeapon Damage=" + damage +
            "\n\tStun Duration=30\n\tItem Range=500\n\tDischarge Sequence=Throw\n\tProjectile=" + projectile +
            "\n\tDischarge Action=CThrowProjectileWeaponAction\n\t{\n\t\tHeight=31\n\t\tVelocity Factor=0.725\n"
            "\t\tGravity AI=CThrowGravityAI\n\t\t{\n\t\t\tGravity=17\n\t\t}\n\t}\n"
            "\tDefault Inventory Item=CStarTrekProjectileWeaponInventoryItem\n\t{\n"
            "\t\tRounds=3\n\t}\n}\n");
    };
    data.files["Inventory/Item Definitions/Concussion Grenade.txt"] = grenade("0", "Shots/Concussion");
    data.files["Inventory/Item Definitions/Frag Grenade.txt"] = grenade("30", "Shots/Frag");
    data.files["Inventory/Item Definitions/Hypospray.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tCan Use On Self=1\n"
                "\tDefault Inventory Item=CStarTrekCountInventoryItem\n\t{\n\t\tRounds=5\n\t}\n"
                "\tProcess=CProcess\n\t{\n\t\tProcess Type=Hypospray Use\n\t}\n"
                "\tDischarge Action=CGiveHealthWeaponAction\n\t{\n\t\tHealth action=CGiveHealthAction\n\t\t{\n"
                "\t\t\tHealth=40\n\t\t}\n\t}\n}\n");
    data.files["Inventory/Item Definitions/Weapon Enhancement.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tCan Use On Self=1\n"
                "\tDefault Inventory Item=CStarTrekCountInventoryItem\n\t{\n\t\tRounds=5\n\t}\n"
                "\tProcess=CProcess\n\t{\n\t\tProcess Type=Weapon Enhancement\n\t}\n"
                "\tDischarge Action=CExecuteActionWeaponAction\n\t{\n\t\tAction=CEnhanceInventoryAction\n\t\t{\n"
                "\t\t\tTarget=$trigger\n\t\t\tTime To Enhance=60\n\t\t}\n\t}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Concussion Grenade", 2}, {"Frag Grenade", 1}, {"Hypospray", -1}, {"Weapon Enhancement", -1}};
    TeamMember bo;
    bo.firstName = "Bo";
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    const MapScreen::Officer &a = screen.officers()[0];
    const MapScreen::Officer &b = screen.officers()[1];
    // Her counts as the roster gives them, or the item's Rounds for -1.
    CHECK(a.items.size() == 4 && a.items[0].count == 2 && a.items[1].count == 1 && a.items[2].count == 5);
    CHECK(a.item == -1 && !screen.useItem(600.0f, 500.0f));
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    // A concussion grenade thrown at the guard, in range: three seconds on
    // it stuns Guard A, in its 150, not Guard B.
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(600.0f, 500.0f));
    run(100);
    CHECK(a.items[0].count == 1 && screen.thrown().size() == 1 && a.discharge == "Throw");
    // Thrown at 45 degrees at 0.725 of what would land it there, it comes
    // down about half way, then skips on along the ground toward it.
    float top = 0.0f;
    float landed = 0.0f;
    while (now < 1300) {
        run(now + 50);
        top = std::max(top, screen.thrown()[0].z);
        if (landed == 0.0f && screen.thrown()[0].z == 0.0f)
            landed = screen.thrown()[0].x;
    }
    CHECK(top > 60.0f && landed > 450.0f && landed < 520.0f);
    run(3000);
    CHECK(screen.thrown()[0].x > 580.0f && screen.thrown()[0].x < 640.0f && screen.thrown()[0].y == 500.0f);
    run(3200);
    CHECK(screen.thrown().empty() && screen.enemies()[0].state == "Stun" && screen.enemies()[0].health == 100.0f);
    CHECK(screen.enemies()[1].state == "Patrol");
    // The frag grenade thrown 50 toward Bo skips on to his feet: he's hurt,
    // Ann, 100 off, isn't; then there's none left.
    screen.chooseItem(0, 1);
    CHECK(screen.useItem(300.0f, 450.0f));
    run(7000);
    CHECK(b.health == 70.0f && a.health == 100.0f && a.items[1].count == 0);
    CHECK(!screen.useItem(300.0f, 400.0f));
    // The hypospray on Bo: she goes to him and heals him, up to his whole.
    screen.chooseItem(0, 2);
    CHECK(screen.useItem(300.0f, 420.0f));
    run(12000);
    CHECK(b.health == 100.0f && a.items[2].count == 4 && std::hypot(a.x - b.x, a.y - b.y) <= 30.0f);
    // The enhancement on Bo: for its 60 seconds his weapons do a quarter
    // more; then as before.
    screen.chooseItem(0, 3);
    CHECK(screen.useItem(b.x, b.y));
    run(now + 3000);
    CHECK(a.items[3].count == 4 && b.enhanced > 55.0f && b.enhanced <= 60.0f);
    ItemDefinition phaser;
    phaser.weaponDamage = 20.0f;
    screen.hurtEnemy(1, phaser, 1, false);
    CHECK(screen.enemies()[1].health == 75.0f);
    run(now + 60000);
    screen.hurtEnemy(1, phaser, 1, false);
    CHECK(b.enhanced == 0.0f && screen.enemies()[1].health == 55.0f);
}

void testOverload() {
    // Ann's phaser set to overload, a guard 100 off: ten seconds on it goes
    // off where she is, hurting both, and it's gone. Bo's, set and thrown
    // at a guard 300 off, goes off there when its time's up.
    const auto part = [](const std::string &name, int x, int y, const std::string &rest) {
        return "Level Part=CEntityWalking\n{\nName=" + name + "\nVisible=1\nActive=1\n" + rest +
               "Position X=" + std::to_string(x) + "\nPosition Y=" + std::to_string(y) + "\n}\n";
    };
    const std::string guardAI = "Activity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\nCurrent State=Patrol\n"
                                "Max Distance=50\n}\n}\nModel=Characters/Guard\nFacing Angle=3.14159\n";
    const std::string zax = "CLayerSaveData\n{\nWidth=3000\nHeight=2000\nView Position=500.000000,500.000000\n"
                            "Tree List=CSortList2D\n{\n" +
                            part("Guard A", 400, 500, guardAI) + part("Guard B", 1600, 1500, guardAI) +
                            part("Away Team Spawn Point 1", 300, 500, "") +
                            part("Away Team Spawn Point 2", 1300, 1500, "") + "}\n}\n";
    const auto bytesOf = [](const std::string &text) { return std::vector<uint8_t>(text.begin(), text.end()); };
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Shots/Concussion.mdl16"] =
        modelFile("Concussion", 1, 1, 50.0f, {}, 150.0f, false, 400.0f, grenadeLook());
    data.files["Inventory/Item Definitions/Phaser.txt"] = bytesOf(
        "CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\nWeapon Damage=40\nItem Range=375\n"
        "Discharge Action=CShootEnergyWeaponAction\n{\n}\nOverload Discharge Action=CThrowProjectileWeaponAction\n"
        "{\nVelocity Factor=0.725\nGravity AI=CThrowGravityAI\n{\nGravity=17\n}\n}\nOverload Discharge Sequence=Throw\n"
        "Is Overloadable=1\nOverload Projectile=Shots/Concussion\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(
        !readTextObject("CRules\n{\nCustom Rules=CCustomRules\n{\nScreen Pos=0,0\nScreen Size=200,200\n}\n}\n", rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Phaser", -1}};
    TeamMember bo;
    bo.firstName = "Bo";
    bo.inventory = {{"Phaser", -1}};
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    const MapScreen::Officer &a = screen.officers()[0];
    const MapScreen::Officer &b = screen.officers()[1];
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    screen.choose(0);
    screen.chooseItem(0, 0);
    screen.toggleOverload();
    CHECK(a.items[0].overload == MapScreen::kOverloadTime);
    screen.toggleOverload();
    CHECK(a.items[0].overload < 0.0f);
    screen.toggleOverload();
    run(9000);
    CHECK(a.items.size() == 1 && screen.enemies()[0].health == 100.0f);
    run(11000);
    CHECK(a.items.empty() && a.weapon == nullptr && screen.enemies()[0].health == 60.0f && a.health == 60.0f);
    // Bo's thrown: it's gone from him as it's thrown, and goes off by the guard.
    screen.choose(1);
    screen.chooseItem(1, 0);
    screen.toggleOverload();
    CHECK(screen.useItem(1600.0f, 1500.0f));
    run(now + 1000);
    CHECK(b.items.empty() && screen.thrown().size() == 1 && screen.thrown()[0].model == "Shots/Concussion");
    run(now + 10000);
    CHECK(screen.thrown().empty() && screen.enemies()[1].health == 60.0f && b.health == 100.0f);
}

void testMeld() {
    // Slovaak's Psychic Rack, three guards: A, armed, 200 off; B, 300 past
    // it, unarmed; C, Immune to Psyche Displacement.
    const auto part = [](const std::string &name, int x, const std::string &category, const std::string &rest) {
        return "Level Part=CEntityWalking\n{\nName=" + name + "\nVisible=1\nActive=1\nCategory=" + category +
               "\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\nCurrent State=Patrol\nVision Cone=360\nMax Distance=500\n}\n}\n" +
               rest + "Model=Characters/Guard\nFacing Angle=3.14159\nPosition X=" + std::to_string(x) +
               "\nPosition Y=500\n}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=3000\nHeight=2000\nView Position=500.000000,500.000000\n"
        "Tree List=CSortList2D\n{\n" +
        part("Guard A", 500, "Enemy",
             "Inventory=CInventory\n{\nInventory List=Array\n{\nItem Count=1\n"
             "Item=CStarTrekEnergyWeaponInventoryItem\n{\nDefinition Index=Disruptor\n}\n}\n}\n") +
        part("Guard B", 800, "Enemy", "") + part("Guard C", 400, "Enemy,Immune to Psyche Displacement", "") +
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=500\n}\n}\n}\n";
    const auto bytesOf = [](const std::string &text) { return std::vector<uint8_t>(text.begin(), text.end()); };
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1, 100.0f);
    data.files["Inventory/Item Definitions/Disruptor.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\nWeapon Damage=30\nReuse Delay=1\nItem Range=375\n"
                "Max Shoot Angle=10\nDischarge Action=CShootEnergyWeaponAction\n}\n");
    data.files["Inventory/Item Definitions/Psychic Rack.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\nItem Range=300\nEnergy Use=3\n"
                "Energy Recharge=0.03\nDefault Inventory Item=CStarTrekPsycheDisplacementInventoryItem\n{\n"
                "Current Energy=100\nController Item=Psyche Displacement Controller\n}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(
        !readTextObject("CRules\n{\nCustom Rules=CCustomRules\n{\nScreen Pos=0,0\nScreen Size=200,200\n}\n}\n", rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember slovaak;
    slovaak.firstName = "Slovaak";
    slovaak.inventory = {{"Psychic Rack", -1}};
    MapScreen screen(data, rules, map, options, 0, {slovaak});
    const MapScreen::Officer &s = screen.officers()[0];
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    screen.choose(0);
    screen.chooseItem(0, 0);
    // The immune one can't be taken: nothing happens but its Fail Sound.
    CHECK(screen.useItem(400.0f, 500.0f));
    run(500);
    CHECK(screen.melded() == -1 && s.task == MapScreen::Officer::Task::kNone);
    // A is: chosen in his place, of the team for the while; he stands.
    CHECK(screen.useItem(500.0f, 500.0f));
    run(1000);
    CHECK(screen.melded() == 0 && screen.melder() == 0 && screen.meldChosen() && !s.chosen);
    CHECK(enemies[0].state == "Controlled" && screen.map().parts[enemies[0].part].category == "Enemy,Team Member");
    const float sx = s.x;
    // Sent after B, A walks into range and shoots it; its charge goes
    // down 3 a second.
    screen.orderMelded(800.0f, 500.0f);
    run(5000);
    CHECK(enemies[1].health < 100.0f && enemies[2].health == 100.0f && s.x == sx);
    // B, whose Valid Targets name the team, goes after it.
    CHECK(enemies[1].foe == 0);
    CHECK(s.items[0].energy > 86.0f && s.items[0].energy < 90.0f);
    // A hurt, Slovaak's hurt as much, at most half what he has.
    ItemDefinition hit;
    hit.weaponDamage = 20.0f;
    screen.hurtEnemy(0, hit, -1, false);
    CHECK(enemies[0].health == 80.0f && s.health == 80.0f);
    hit.weaponDamage = 60.0f;
    screen.hurtEnemy(0, hit, -1, false);
    CHECK(enemies[0].health == 20.0f && s.health == 40.0f);
    // Let go, A is on patrol again, an enemy as before.
    screen.breakMeld();
    CHECK(screen.melded() == -1 && enemies[0].state == "Patrol" && !s.items[0].on);
    CHECK(screen.map().parts[enemies[0].part].category == "Enemy");
    // Taken again, the charge runs out and it ends by itself.
    screen.choose(0);
    CHECK(screen.useItem(enemies[0].x, enemies[0].y));
    run(now + 1000);
    CHECK(screen.melded() == 0);
    run(now + 40000);
    CHECK(screen.melded() == -1 && s.items[0].energy < 1.0f);
}

void testFullInventory() {
    // The bar with two places: a phaser, and two grenades. Right-clicked,
    // the second place's both show over it; a click on one chooses it, and
    // it closes 1.5 seconds after the mouse goes.
    MemoryArchive data;
    data.files["Cache/Interface/Screens/Inventory/Background.frm16"] = solidFrame(320, 60, 0x001f);
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\nCustom Rules=CCustomRules\n{\nScreen Pos=0,0\nScreen Size=640,480\n"
                          "Inventory Pos=300,400\nInventory Bar Offset=37,12\nInventory Icon Size=34,34\n"
                          "Inventory Icon Spacing=2\nInventory Next Button Pos=287,12\n}\n}\n",
                          rules));
    TeamMember ann;
    ann.firstName = "Ann";
    GameHud hud(data, rules, {ann}, "", 1000, 1000);
    hud.setInventory({{"Phaser", -1}, {"Concussion Grenade", 3}, {"Frag Grenade", 2}}, {{0, 1, {0}}, {1, 2, {1, 2}}},
                     0);
    Surface s;
    s.create(640, 480, PixelFormat::kRGBA32);
    const auto drawn = [&](uint64_t now) {
        std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(255));
        hud.draw(s, now, 0, 500.0f, 500.0f, 400, 400);
    };
    const auto mouse = [&](EventType type, int x, int y, MouseButton button, uint64_t now) {
        Event ev;
        ev.type = type;
        ev.button = button;
        ev.mouseX = x;
        ev.mouseY = y;
        return hud.handleEvent(ev, now);
    };
    // The second place's column: x 373, its items from 376 up.
    drawn(1000);
    CHECK(!hud.fullInventory() && s.row(345)[380 * 4] == 255);
    mouse(EventType::kMouseDown, 340, 420, MouseButton::kRight, 1000);
    CHECK(hud.fullInventory());
    drawn(1100);
    CHECK(s.row(345)[380 * 4] == 0 && s.row(380)[380 * 4] == 0 && s.row(345)[340 * 4] == 255);
    const GameHud::Result chosen = mouse(EventType::kMouseDown, 380, 345, MouseButton::kLeft, 1200);
    CHECK(chosen.used && chosen.item == 2 && !hud.fullInventory());
    mouse(EventType::kMouseDown, 340, 420, MouseButton::kRight, 2000);
    mouse(EventType::kMouseMove, 100, 100, MouseButton::kLeft, 2000);
    drawn(3000);
    CHECK(hud.fullInventory());
    drawn(3600);
    CHECK(!hud.fullInventory());
}

void testThrowRange() {
    // Ann at (300, 500), a wall 100 to her right. A grenade needs no clear
    // line: she throws over it from where she stands. One that Needs LOS
    // has her walk round. Its range is on the ground, the map's
    // foreshortening undone: 340 up the map is 477 away, in its 500; 380
    // up is 533, out of it.
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,500.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CEntityBase\n{\nName=Wall\nVisible=1\nActive=1\nCollideable=1\nModel=Walls/Wall\n"
        "Position X=400\nPosition Y=500\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=500\n}\n}\n}\n";
    const auto bytesOf = [](const std::string &text) { return std::vector<uint8_t>(text.begin(), text.end()); };
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Walls/Wall.mdl16"] =
        modelFile("Wall", 1, 1, 0.0f, {{-10, -80}, {10, -80}, {10, 80}, {-10, 80}, {-10, -80}});
    data.files["Cache/Models/Shots/Grenade.mdl16"] =
        modelFile("Grenade", 1, 1, 1.0f, {}, 0.0f, false, 400.0f, grenadeLook());
    const auto grenade = [&](const std::string &more) {
        return bytesOf("CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\nItem Range=500\n" + more +
                       "Projectile=Shots/Grenade\nDischarge Action=CThrowProjectileWeaponAction\n{\n"
                       "Velocity Factor=0.725\nGravity AI=CThrowGravityAI\n{\nGravity=17\n}\n}\n"
                       "Default Inventory Item=CStarTrekProjectileWeaponInventoryItem\n{\nRounds=9\n}\n}\n");
    };
    data.files["Inventory/Item Definitions/Grenade.txt"] = grenade("Need LOS=0\n");
    data.files["Inventory/Item Definitions/Sighted Grenade.txt"] = grenade("Need LOS=1\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(
        !readTextObject("CRules\n{\nCustom Rules=CCustomRules\n{\nScreen Pos=0,0\nScreen Size=200,200\n}\n}\n", rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Grenade", -1}, {"Sighted Grenade", -1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    uint64_t now = 0;
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 20)
            screen.update(now += 20);
    };
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(600.0f, 500.0f));
    run(100);
    CHECK(screen.thrown().size() == 1 && a.x == 300.0f && a.y == 500.0f);
    run(5000);
    CHECK(screen.thrown().empty());
    screen.chooseItem(0, 1);
    CHECK(screen.useItem(600.0f, 500.0f));
    run(100);
    CHECK(screen.thrown().empty() && (!a.path.empty() || a.x != 300.0f));
    screen.cancelOrders();
    // Back where she started, the first grenade up the map.
    screen.order(300.0f, 500.0f);
    run(3000);
    CHECK(std::hypot(a.x - 300.0f, a.y - 500.0f) < 1.0f);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(300.0f, 120.0f));
    run(100);
    CHECK(screen.thrown().empty() && a.y < 500.0f);
    screen.cancelOrders();
    screen.order(300.0f, 500.0f);
    run(3000);
    CHECK(screen.useItem(300.0f, 160.0f));
    run(100);
    CHECK(screen.thrown().size() == 1 && std::hypot(a.x - 300.0f, a.y - 500.0f) < 1.0f);
}

void testMaps() {
    // A mission's two maps: on the first, Ann and Bo, and a door's polygon
    // round Ann; on the second, a landing.
    const auto zaxOf = [](const std::string &parts) {
        const std::string s = "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=500.000000,500.000000\n"
                              "\tTree List=CSortList2D\n\t{\n" +
                              parts + "\t}\n}\n";
        return std::vector<uint8_t>(s.begin(), s.end());
    };
    MemoryArchive data;
    data.files["Levels/Test A.zax"] = zaxOf(
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tPosition X=100\n"
        "\t\t\tPosition Y=100\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 2\n\t\t\tPosition X=300\n"
        "\t\t\tPosition Y=300\n\t\t}\n"
        "\t\tLevel Part=CFreeRangePoly\n\t\t{\n\t\t\tName=Door\n\t\t\tPolygon=50, 50, 150, 50, 150, 150, 50, 150, 50, 50\n"
        "\t\t}\n");
    data.files["Levels/Test B.zax"] =
        zaxOf("\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Landing\n\t\t\tPosition X=700\n\t\t\tPosition Y=600\n"
              "\t\t}\n");
    GameMap a;
    GameMap b;
    CHECK(!loadMap(data, "Levels/Test A.zax", a) && !loadMap(data, "Levels/Test B.zax", b));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bo;
    bo.firstName = "Bo";
    MapScreen first(data, rules, a, options, 0, {ann, bo});
    MapScreen second(data, rules, b, options, 0, {ann, bo}, {}, nullptr, true);
    second.shareProgress(first.progress());
    // The later map has no one on it till they're sent there.
    CHECK(first.officers()[0].present && !second.officers()[0].present && !second.officers()[1].present);
    // A relocation through the door sends Ann, inside it, not Bo.
    TextObject move;
    CHECK(!readTextObject("CStarTrekRelocateTeamAction\n{\n\tNew Map Name=Test B.zax\n\tNew Location=Landing\n"
                          "\tWho To Switch=$Instigator\n\tTouching Poly=Door\n\tMake this map active=1\n"
                          "\tOnly Relocated Selected Members=0\n}\n",
                          move));
    first.runAction(&move, 0);
    const std::vector<Relocation> moves = first.takeRelocations();
    CHECK(moves.size() == 1 && moves[0].map == "Test B.zax" && moves[0].show);
    CHECK((moves[0].officers == std::vector<int>{0}) && first.takeRelocations().empty());
    // A group's, with Bo chosen and Ann setting it off, sends them both.
    TextObject group;
    CHECK(!readTextObject("CStarTrekRelocateGroupAction\n{\n\tNew Map Name=Test B.zax\n\tNew Location=Landing\n"
                          "\tWho To Switch=$Instigator\n\tGroup Name=Door Team\n}\n",
                          group));
    first.choose(1);
    first.runAction(&group, 0);
    const std::vector<Relocation> groupMoves = first.takeRelocations();
    CHECK(groupMoves.size() == 1 && (groupMoves[0].officers == std::vector<int>{0, 1}));
    // She leaves the first and arrives at the landing, as she was.
    float x = 0.0f;
    float y = 0.0f;
    CHECK(second.partPosition(moves[0].location, x, y) && x == 700.0f && y == 600.0f);
    second.arrive(0, first.officers()[0], x, y);
    first.depart(0);
    CHECK(!first.officers()[0].present && second.officers()[0].present && second.officers()[0].x == 700.0f);
    // Chosen on the first map's team bar, she's on the other map.
    first.choose(0);
    CHECK(!first.officers()[0].chosen);
    // The mission's progress is the same on both: ended on one, it's over
    // on the other.
    TextObject end;
    CHECK(!readTextObject("CDisplayMissionFailedScreenAction\n{\n\tScript=None\n\tMission Over Text=None\n}\n", end));
    second.runAction(&end);
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    first.draw(surface, 100);
    CHECK(first.missionOver() && !first.missionCompleted());
}

void testInterface() {
    // Ann at (100, 100); a guard at (300, 300) facing left, seeing 100
    // ahead, 90 degrees across.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=1000\n\tHeight=1000\n\tView Position=300.000000,300.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tCurrent State=Patrol\n\t\t\t\t\tMax Distance=100\n\t\t\t\t\tVision Cone=90\n"
        "\t\t\t\t}\n\t\t\t}\n\t\t\tModel=Characters/Guard\n\t\t\tPosition X=300\n\t\t\tPosition Y=300\n"
        "\t\t\tFacing Angle=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tPosition X=100\n"
        "\t\t\tPosition Y=100\n\t\t}\n\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=400,400\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    // Time stopped: an order's given, but she doesn't move till it goes on.
    screen.setPaused(true);
    screen.order(100.0f, 200.0f);
    screen.update(1000);
    CHECK(a.y == 100.0f && !a.path.empty());
    screen.setPaused(false);
    screen.update(2000);
    CHECK(a.y > 100.0f);
    // Cancel Orders: she stops where she is.
    screen.cancelOrders();
    const float y = a.y;
    screen.update(3000);
    CHECK(a.path.empty() && a.y == y);
    // The vision button: choosing, then showing the guard's sight, the one
    // nearest the view's middle; then off.
    CHECK(screen.visionState() == 0);
    screen.pressVision();
    CHECK(screen.visionState() == 1 && screen.visionEnemy() == -1);
    screen.pressVision();
    CHECK(screen.visionState() == 2 && screen.visionEnemy() == 0);
    // In its sight, 50 to its left, the ground's tinted; behind it it isn't.
    Surface shown;
    shown.create(640, 480, PixelFormat::kRGBA32);
    screen.moveCamera(300.0f, 300.0f);
    screen.draw(shown, 3000);
    screen.pressVision();
    CHECK(screen.visionState() == 0);
    Surface hidden;
    hidden.create(640, 480, PixelFormat::kRGBA32);
    screen.draw(hidden, 3000);
    const auto px = [](const Surface &s, int x, int y) { return s.row(y)[x * 4]; };
    CHECK(px(shown, 150, 200) != px(hidden, 150, 200));
    CHECK(px(shown, 260, 200) == px(hidden, 260, 200));
    // Esc opens the escape menu, saving and loading not there yet; Esc
    // again goes back.
    Event esc;
    esc.type = EventType::kKeyDown;
    esc.key = key::kEscape;
    CHECK(screen.handleEvent(esc, nullptr, 3000) == MapScreen::Choice::kNone && screen.escapeShown());
    CHECK(screen.escapeShown());
    Surface s;
    s.create(640, 480, PixelFormat::kRGBA32);
    screen.draw(s, 3100);
    screen.handleEvent(esc, nullptr, 3200);
    CHECK(!screen.escapeShown());
}

void testGadgets() {
    // Ann with a phaser that can stun, remote mines and a tricorder; a
    // guard 200 off, looking away.
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,300.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityWalking\n\t\t{\n\t\t\tName=Guard\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CBorgAI\n\t\t\t\t{\n"
        "\t\t\t\t\tCurrent State=Patrol\n\t\t\t\t\tMax Distance=50\n\t\t\t\t}\n\t\t\t}\n"
        "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=300\n\t\t\tFacing Angle=3.14159\n"
        "\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tPosition X=300\n"
        "\t\t\tPosition Y=300\n\t\t}\n\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1);
    data.files["Cache/Models/Shots/Remote.mdl16"] = modelFile("Remote", 1, 1, 1.0f, {}, 100.0f, false, 0.0f);
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    data.files["Levels/Model Descriptions.RDS"] = bytesOf(":A test model\r\nA guard, armed.\r\n");
    data.files["Inventory/Item Definitions/Phaser.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tWeapon Damage=25\n"
                "\tReuse Delay=0.5\n\tItem Range=375\n\tStun Duration=15\n\tHas Multiple Settings=1\n"
                "\tDischarge Action=CShootEnergyWeaponAction\n\t{\n\t}\n"
                "\tDefault Inventory Item=CStarTrekEnergyWeaponInventoryItem\n\t{\n\t\tWeapon Setting=Kill\n\t}\n"
                "\tInfo Renderer=CPhaserInfoRenderer\n\t{\n\t\tStun Pos=155,39\n\t}\n}\n");
    data.files["Inventory/Item Definitions/Remote Mine.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tWeapon Damage=60\n"
                "\tDefault Inventory Item=CStarTrekRemoteMineInventoryItem\n\t{\n\t\tRounds=2\n\t}\n"
                "\tProcess=CProcess\n\t{\n\t\tProcess Type=Shoot\n\t\tProcess Activity=CDropItemAI\n\t\t{\n"
                "\t\t\tDestination Action=CDropItemAtTargetAction\n\t\t\t{\n"
                "\t\t\t\tModel to Create=Shots/Remote\n\t\t\t\tDeploy Sequence=Deploy\n\t\t\t}\n\t\t}\n\t}\n"
                "\tInfo Renderer=CDetonateInfoRenderer\n\t{\n\t\tDetonate Button Pos=140,14\n\t}\n}\n");
    data.files["Inventory/Item Definitions/Tricorder.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tItem Range=450\n"
                "\tProcess=CProcess\n\t{\n\t\tProcess Type=Scan\n\t}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Phaser", -1}, {"Remote Mine", -1}, {"Tricorder", -1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    const MapScreen::Enemy &guard = screen.enemies()[0];
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    // The phaser, set to kill as it comes, switched to stun: the guard's
    // stunned, not hurt.
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(!a.items[0].stun);
    screen.toggleStun();
    CHECK(a.items[0].stun);
    screen.attack(0);
    run(500);
    CHECK(guard.state == "Stun" && guard.health == 100.0f);
    // A remote mine put down by the guard; she walks off, and it goes
    // when it's set off.
    screen.chooseItem(0, 1);
    CHECK(a.items[1].count == 2 && screen.useItem(480.0f, 300.0f));
    run(8000);
    CHECK(screen.mines().size() == 1 && a.items[1].count == 1);
    CHECK(screen.mines()[0].item->deploySequence == "Deploy");
    screen.order(250.0f, 300.0f);
    run(16000);
    screen.detonate();
    run(16100);
    CHECK(screen.mines().empty() && guard.health == 40.0f && a.health == 100.0f);
    // The tricorder on the guard: the description of its model.
    screen.chooseItem(0, 2);
    CHECK(screen.useItem(500.0f, 300.0f));
    run(17000);
    CHECK(screen.scanText() == "A guard, armed.");
}

void testProcesses() {
    // A console whose process, used with nothing chosen, walks the officer
    // to it and puts a line on the tricorder's panel and the crate on the
    // minimap; a guard with a scan of its own for the tricorder.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const std::string process = [] {
        return std::string("\t\t\tActivity=Array\n\t\t\t{\n\t\t\t\tItem Count=1\n\t\t\t\tActivity=CProcessListAI\n"
                           "\t\t\t\t{\n\t\t\t\t\tProcess=Array\n\t\t\t\t\t{\n\t\t\t\t\t\tItem Count=2\n");
    }();
    const auto use = [](const std::string &type, const std::string &item, const std::string &line,
                        const std::string &more) {
        return "\t\t\t\t\t\tProcess=CProcess\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tProcess Type=" + type +
               "\n\t\t\t\t\t\t\tProcess Cursor=Generic Use\n\t\t\t\t\t\t\tRequired Selected Item=" + item +
               "\n\t\t\t\t\t\t\tRequired Item In Inventory=!None\n\t\t\t\t\t\t\tProcess Activity=Array\n"
               "\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tItem Count=2\n\t\t\t\t\t\t\t\tProcess Activity=CGoToAI\n"
               "\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\tDestination=$trigger\n\t\t\t\t\t\t\t\t}\n"
               "\t\t\t\t\t\t\t\tProcess Activity=CActionAI\n\t\t\t\t\t\t\t\t{\n"
               "\t\t\t\t\t\t\t\t\tAction=CMultipleActionsAction\n\t\t\t\t\t\t\t\t\t{\n"
               "\t\t\t\t\t\t\t\t\t\tAction=Array\n\t\t\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\t\t\tItem Count=2\n"
               "\t\t\t\t\t\t\t\t\t\t\tAction=CTricorderDialogAction\n\t\t\t\t\t\t\t\t\t\t\t{\n"
               "\t\t\t\t\t\t\t\t\t\t\t\tScript=Test\n\t\t\t\t\t\t\t\t\t\t\t\tLine=" +
               line + "\n\t\t\t\t\t\t\t\t\t\t\t}\n" + more +
               "\t\t\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}\n";
    };
    const std::string poi = "\t\t\t\t\t\t\t\t\t\t\tAction=CAddAIAction\n\t\t\t\t\t\t\t\t\t\t\t{\n"
                            "\t\t\t\t\t\t\t\t\t\t\t\tTarget Name=Crate\n"
                            "\t\t\t\t\t\t\t\t\t\t\t\tAI=CMiniMapPointOfInterestAI\n\t\t\t\t\t\t\t\t\t\t\t\t{\n"
                            "\t\t\t\t\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t\t\t\t\t}\n";
    const std::string none = "\t\t\t\t\t\t\t\t\t\t\tAction=CDelayAction\n\t\t\t\t\t\t\t\t\t\t\t{\n"
                             "\t\t\t\t\t\t\t\t\t\t\t}\n";
    const std::string zax =
        "CLayerSaveData\n{\n\tWidth=2000\n\tHeight=2000\n\tView Position=500.000000,300.000000\n"
        "\tTree List=CSortList2D\n\t{\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Console\n\t\t\tVisible=1\n\t\t\tActive=1\n" +
        process + use("Use", "!None", "Console", poi) + use("Scan", "Tricorder", "Scanned", none) +
        "\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t\tModel=Props/Console\n\t\t\tPosition X=600\n\t\t\tPosition Y=300\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Crate\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tPosition X=900\n\t\t\tPosition Y=900\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tPosition X=300\n"
        "\t\t\tPosition Y=300\n\t\t}\n\t}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Props/Console.mdl16"] = modelFile("Console", 1, 1);
    data.files["Levels/Test.RDS"] = bytesOf(":Console\r\nAccess granted.\r\n:Scanned\r\nA console.\r\n");
    data.files["Inventory/Item Types/!None.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");
    data.files["Inventory/Item Types/Gadget.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");
    data.files["Inventory/Item Types/Primary.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");
    const auto item = [&](const std::string &name, const std::string &type, const std::string &more) {
        data.files["Inventory/Item Definitions/" + name + ".txt"] =
            bytesOf("CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tInventory Type=" + type + "\n" +
                    more + "}\n");
    };
    item("Phaser", "Primary",
         "\tWeapon Damage=25\n\tDischarge Action=CShootEnergyWeaponAction\n\t{\n\t}\n"
         "\tProcess=CProcess\n\t{\n\t\tProcess Type=Shoot\n\t\tProcess Cursor=Phaser\n\t}\n"
         "\tEntity Hovering Cursor=Phaser Active\n");
    item("Tricorder", "Gadget",
         "\tItem Range=450\n\tProcess=CProcess\n\t{\n\t\tProcess Type=Scan\n"
         "\t\tProcess Cursor=Tricorder Active\n\t\tCan't Use Cursor=Tricorder Inactive\n\t}\n"
         "\tEntity Hovering Cursor=Tricorder Active\n");
    item("Hypospray", "Gadget", "");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Tricorder", -1}, {"Phaser", -1}, {"Hypospray", 3}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    screen.choose(0);
    // The bar: a place for each type, the last type's first, the
    // Tricorder and Hypospray sharing the gadgets'.
    const std::vector<std::vector<int>> places = screen.itemPlaces(a);
    CHECK(places.size() == 2 && places[0] == std::vector<int>{1} && (places[1] == std::vector<int>{0, 2}));
    // Inventory Next: along the gadgets, then on to the phaser; Prev back.
    screen.chooseItem(0, 0);
    screen.stepItem(0, 1);
    CHECK(a.item == 2);
    screen.stepItem(0, 1);
    CHECK(a.item == 1);
    screen.stepItem(0, -1);
    CHECK(a.item == 2);
    // The pointer: the edge's arrow, the phaser's over the map, the
    // console's process cursor.
    Event mv;
    mv.type = EventType::kMouseMove;
    mv.mouseX = 100;
    mv.mouseY = 1;
    screen.handleEvent(mv, nullptr, now);
    CHECK(screen.cursorName() == "Move Up");
    screen.chooseItem(0, 1);
    mv.mouseX = 180;
    mv.mouseY = 100;
    screen.handleEvent(mv, nullptr, now);
    CHECK(screen.cursorName() == "Phaser");
    screen.moveCamera(600.0f, 300.0f);
    mv.mouseX = int(600.0f - screen.mapX(0));
    mv.mouseY = int(300.0f - screen.mapY(0));
    screen.handleEvent(mv, nullptr, now);
    CHECK(screen.cursorName() == "Generic Use");
    // Clicked, she walks to the console; its line's on the tricorder's
    // panel, and the crate's on the minimap.
    Event click;
    click.type = EventType::kMouseDown;
    click.button = MouseButton::kLeft;
    click.mouseX = mv.mouseX;
    click.mouseY = mv.mouseY;
    screen.handleEvent(click, nullptr, now);
    click.type = EventType::kMouseUp;
    screen.handleEvent(click, nullptr, now);
    CHECK(a.task == MapScreen::Officer::Task::kProcess);
    run(10000);
    CHECK(a.task == MapScreen::Officer::Task::kNone && std::hypot(a.x - 600.0f, a.y - 300.0f) <= 30.0f);
    CHECK(screen.scanText() == "Access granted.");
    const std::vector<GameHud::Mark> marks = screen.minimapMarks();
    CHECK(marks.size() == 2 && marks[1].kind == GameHud::Mark::kInterest && marks[1].x == 900.0f);
    // The tricorder chosen: the console's scan for it.
    screen.chooseItem(0, 0);
    CHECK(screen.cursorName() == "Generic Use");
    CHECK(screen.useItem(600.0f, 300.0f));
    run(12000);
    CHECK(screen.scanText() == "A console.");
    // Zoom Out held a second shows 30 steps more of the map, then no more
    // than 1.5 times; Reset; Zoom In held a little, let go, eases back.
    screen.holdZoom(-1, true);
    run(now + 1000);
    CHECK(std::abs(1.0f / screen.zoom() - 1.3f) < 0.02f);
    run(now + 5000);
    CHECK(std::abs(1.0f / screen.zoom() - 1.5f) < 0.001f);
    screen.holdZoom(-1, false);
    screen.holdZoom(0, true);
    CHECK(screen.zoom() == 1.0f && screen.mapX(0) == screen.cameraX() - 100.0f);
    screen.holdZoom(1, true);
    run(now + 200);
    screen.holdZoom(1, false);
    CHECK(1.0f / screen.zoom() < 0.97f);
    run(now + 2000);
    CHECK(screen.zoom() == 1.0f);
    // The mouse at the view's right edge a second: the scrolling gaining
    // 15 a frame to 450 a second, about 232 pixels; moved off, it slows by
    // 0.9 a frame, 135 pixels more, and stops.
    const float startX = screen.cameraX();
    Event move;
    move.type = EventType::kMouseMove;
    move.mouseX = 199;
    move.mouseY = 50;
    screen.handleEvent(move, nullptr, now);
    run(now + 1000);
    const float moved = screen.cameraX() - startX;
    CHECK(moved > 215.0f && moved < 250.0f);
    move.mouseX = 100;
    screen.handleEvent(move, nullptr, now);
    run(now + 4000);
    const float stopped = screen.cameraX();
    run(now + 1000);
    CHECK(screen.cameraX() == stopped && std::abs(stopped - startX - moved - 135.0f) < 5.0f);
}

void testPolygonProcesses() {
    // A gas cloud's polygon about (450, 300): the tricorder's scan of it runs
    // as the pointer goes over it, the medical tricorder's once clicked. A
    // panel at (800, 300) used only by its hot spot, a polygon to its right.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto process = [](const std::string &type, const std::string &item, bool hovering, const std::string &lamp) {
        return "Process=CProcess\n{\nProcess Type=" + type + "\nRequired Selected Item=" + item +
               "\nRequired Item In Inventory=!None\nExecute if Hovering=" + (hovering ? "1" : "0") +
               "\nProcess Activity=Array\n{\nItem Count=1\nProcess Activity=CActionAI\n{\n"
               "Action=CActivateAction\n{\nTarget Name=" +
               lamp + "\n}\n}\n}\n}\n";
    };
    const auto lamp = [](const std::string &name) {
        return "Level Part=CEntityBase\n{\nName=" + name + "\nVisible=1\nActive=0\n}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=450.000000,300.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CRenderablePolygon\n{\nVisible=1\nActive=1\nCollideable=0\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CProcessListAI\n{\nProcess=Array\n{\nItem Count=2\n" +
        process("Scan", "Tricorder", true, "Gas Lamp") +
        process("Medical Scan", "Medical Tricorder", false, "Med Lamp") +
        "}\n}\n}\nPolygon=400, 250, 500, 250, 500, 350, 400, 350, 400, 250\n}\n"
        "Level Part=CEntityBase\n{\nName=Panel\nVisible=1\nActive=1\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CProcessListAI\n{\nHot Spot=Panel Spot\nProcess=Array\n{\nItem Count=1\n" +
        process("Use", "!None", false, "Panel Lamp") +
        "}\n}\n}\nModel=Props/Panel\nPosition X=800\nPosition Y=300\n}\n"
        "Level Part=CFreeRangePoly\n{\nName=Panel Spot\nVisible=1\nActive=1\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CProcessListAI\n{\nProcess=Array\n{\nItem Count=0\n}\n}\n}\n"
        "Polygon=840, 280, 880, 280, 880, 320, 840, 320, 840, 280\n}\n" +
        lamp("Gas Lamp") + lamp("Med Lamp") + lamp("Panel Lamp") +
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Props/Panel.mdl16"] = modelFile("Panel", 1, 1);
    for (const std::string name : {"Tricorder", "Medical Tricorder"})
        data.files["Inventory/Item Definitions/" + name + ".txt"] =
            bytesOf("CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\nItem Range=450\n"
                    "Process=CProcess\n{\nProcess Type=" +
                    std::string(name == "Tricorder" ? "Scan" : "Medical Scan") + "\n}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Tricorder", -1}, {"Medical Tricorder", -1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    const auto point = [&](float x, float y) {
        Event mv;
        mv.type = EventType::kMouseMove;
        mv.mouseX = int(x - screen.mapX(0));
        mv.mouseY = int(y - screen.mapY(0));
        screen.handleEvent(mv, nullptr, now);
    };
    run(1000);
    screen.choose(0);
    screen.moveCamera(450.0f, 300.0f);
    // The tricorder over the gas: scanned, no click needed; still over it,
    // not again.
    screen.chooseItem(0, 0);
    point(450.0f, 300.0f);
    CHECK(a.task == MapScreen::Officer::Task::kProcess);
    run(now + 3000);
    CHECK(screen.partState(3).active && a.task == MapScreen::Officer::Task::kNone);
    point(460.0f, 310.0f);
    CHECK(a.task == MapScreen::Officer::Task::kNone);
    // The medical tricorder over it does nothing till it's clicked.
    screen.chooseItem(0, 1);
    point(360.0f, 300.0f);
    point(450.0f, 300.0f);
    CHECK(a.task == MapScreen::Officer::Task::kNone && !screen.partState(4).active);
    CHECK(screen.useItem(450.0f, 300.0f));
    run(now + 3000);
    CHECK(screen.partState(4).active);
    // The panel isn't where it stands, but in its hot spot.
    CHECK(!screen.useItem(800.0f, 300.0f));
    CHECK(screen.useItem(860.0f, 300.0f));
    run(now + 3000);
    CHECK(screen.partState(5).active);
}

void testStandingInTheWay() {
    // Ann sent east along waypoints at 300, 400 and 500, Bo standing 8
    // pixels from the middle one. With a way round by (400, 360) she takes
    // it, the way past Bo costing five times its length; without, she goes
    // through him. Bo doesn't move either way.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=400.000000,300.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 2\nPosition X=392\nPosition Y=297\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    TeamMember ann;
    ann.firstName = "Ann";
    TeamMember bo;
    bo.firstName = "Bo";
    for (const bool wayRound : {true, false}) {
        WayPoints way;
        way.points = {{300.0f, 300.0f, {1}}, {400.0f, 300.0f, {0, 2}}, {500.0f, 300.0f, {1}}};
        if (wayRound) {
            way.points.push_back({400.0f, 360.0f, {0, 2}});
            way.points[0].neighbours.push_back(3);
            way.points[2].neighbours.push_back(3);
        }
        GameOptions options;
        MapScreen screen(data, rules, map, options, 0, {ann, bo}, way);
        const MapScreen::Officer &a = screen.officers()[0];
        const MapScreen::Officer &b = screen.officers()[1];
        uint64_t now = 0;
        screen.update(now += 50);
        screen.choose(0);
        screen.order(500.0f, 300.0f);
        float nearest = 1e9f;
        while (now < 20000) {
            screen.update(now += 50);
            nearest = std::min(nearest, std::hypot(a.x - b.x, a.y - b.y));
        }
        CHECK(std::hypot(a.x - 500.0f, a.y - 300.0f) < 2.0f && a.path.empty());
        CHECK(std::hypot(b.x - 392.0f, b.y - 297.0f) < 1.0f);
        CHECK(wayRound ? nearest > 40.0f : nearest < 10.0f);
    }
}

void testScriptsHoldActors() {
    // A script whose manager has Pause Actors, waiting 3 seconds: Ann,
    // sent off meanwhile, stays where she is till it's over, then goes.
    const std::string zax =
        "CLayerSaveData\n{\nWidth=1000\nHeight=1000\nView Position=300.000000,300.000000\n"
        "Custom Map Properties=CStarTrekLayerSaveData\n{\nCamera Script Manager=CCameraScriptManager\n{\n"
        "Pause Game=0\nPause Actors=1\nCamera Script Array=Array\n{\nArray Count=1\n"
        "Array Item=CCameraScript\n{\nWhere To Pan=Spot\nTime To Pan=0\nTime To Wait=3\nPan To Position=0\n}\n}\n}\n}\n"
        "Tree List=CSortList2D\n{\n"
        "Level Part=CEntityBase\n{\nName=Spot\nVisible=0\nPosition X=300\nPosition Y=300\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    CHECK(map.cameraScripts.size() == 1 && map.cameraScripts[0].holdsActors);
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    uint64_t now = 0;
    screen.update(now += 50);
    screen.choose(0);
    screen.order(400.0f, 300.0f);
    while (now < 2500)
        screen.update(now += 50);
    CHECK(screen.scripted() && a.x == 300.0f);
    while (now < 8000)
        screen.update(now += 50);
    CHECK(!screen.scripted() && std::abs(a.x - 400.0f) < 1.0f);
}

void testDoorWays() {
    // A door across the way at 400, 300, its outline 20 by 80, and a way
    // round it by 400, 380. Locked, Ann's way goes round; unlocked and
    // Triggered By Players, it's planned through, as it'll open for her.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    TeamMember ann;
    ann.firstName = "Ann";
    for (const bool locked : {true, false}) {
        const std::string zax =
            "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=400.000000,300.000000\nTree List=CSortList2D\n{\n"
            "Level Part=CEntityAnimated\n{\nName=Door\nCollideable=1\nActivity=Array\n{\nItem Count=1\n"
            "Activity=CDoorAI\n{\nIs Locked=" +
            std::string(locked ? "1" : "0") +
            "\nTriggered By Players=1\nTriggered By Enemies=1\n}\n}\n"
            "Model=Props/Door\nCur Sequence=Closed\nPosition X=400\nPosition Y=300\n}\n"
            "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
        MemoryArchive data;
        data.files["Levels/Test.zax"] = bytesOf(zax);
        data.files["Cache/Models/Props/Door.mdl16"] =
            modelFile("Door", 1, 1, 0.0f, {{-10, -40}, {10, -40}, {10, 40}, {-10, 40}, {-10, -40}});
        GameMap map;
        CHECK(!loadMap(data, "Levels/Test.zax", map));
        WayPoints way;
        way.points = {
            {300.0f, 300.0f, {1, 3}}, {400.0f, 300.0f, {0, 2}}, {500.0f, 300.0f, {1, 3}}, {400.0f, 380.0f, {0, 2}}};
        GameOptions options;
        MapScreen screen(data, rules, map, options, 0, {ann}, way);
        const MapScreen::Officer &a = screen.officers()[0];
        uint64_t now = 0;
        screen.update(now += 50);
        screen.choose(0);
        screen.order(500.0f, 300.0f);
        float lowest = 0.0f;
        while (now < 15000) {
            screen.update(now += 50);
            lowest = std::max(lowest, a.y);
        }
        CHECK(std::hypot(a.x - 500.0f, a.y - 300.0f) < 2.0f);
        CHECK(locked ? lowest > 370.0f : lowest < 310.0f);
    }
}

void testKit() {
    // Ann with the cloaking band, a decoy to put down, a phaser and a
    // grenade; a guard looking her way, one in a stasis field's oval, one of
    // Electronic Equipment, and one to shoot whose body's then of Dead Bodies.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto guard = [](const std::string &name, int x, int y, float angle, const std::string &more) {
        return "Level Part=CEntityWalking\n{\nName=" + name +
               "\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\nCurrent State=Patrol\nMax Distance=400\n"
               "Vision Cone=90\n}\n}\nModel=Characters/Guard\nPosition X=" +
               std::to_string(x) + "\nPosition Y=" + std::to_string(y) + "\nFacing Angle=" + std::to_string(angle) +
               "\n" + more + "}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,300.000000\nTree List=CSortList2D\n{\n" +
        guard("Watcher", 500, 300, 0.0f, "") + guard("Trapped", 900, 900, 0.0f, "") +
        guard("Camera", 900, 300, 0.0f, "Category=Electronic Equipment,Enemy\n") +
        guard("Target", 300, 600, 1.5708f, "Default Death Type=Test\n") +
        "Level Part=CEntityBase\n{\nName=Field\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CEveryTouchingOvalTriggerAI\n{\nTriggered by=Enemy,Player,Team Member\nX Radius=40\nYX Ratio=1\n"
        "Enter Action=CConditionalAction\n{\nTry Action=CCheckForItemAction\n{\nWho To Check=$Instigator\n"
        "Inventory Definition=Field\n}\nFail Action=CAssignTemporaryTaskAction\n{\nTarget=$Instigator\n"
        "Temporary Task AI=CDoNothingAI\n{\n}\n}\n}\n}\n}\nPosition X=900\nPosition Y=900\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1);
    data.files["Cache/Models/Props/Box.mdl16"] = modelFile("Box", 1, 1);
    data.files["Cache/Models/Shots/Grenade.mdl16"] =
        modelFile("Grenade", 1, 1, 1.0f, {}, 0.0f, false, 400.0f, grenadeLook());
    data.files["Death Behaviors/Test.txt"] =
        bytesOf("CDeathBehaviorDefinition\n{\nDeath Behaviors=CEntityBehaviorExecuteAction\n{\n"
                "Action=CSetCategoryAction\n{\nWho to Set=$trigger\nCategory to Set=Dead Bodies\n}\n}\n}\n");
    const auto item = [&](const std::string &name, const std::string &body) {
        data.files["Inventory/Item Definitions/" + name + ".txt"] =
            bytesOf("CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\n" + body + "}\n");
    };
    item("Band", "Default Inventory Item=CStarTrekInvisBandInventoryItem\n{\n}\nEnergy Use=4.5\nEnergy Recharge=0\n"
                 "Process=CProcess\n{\nProcess Type=!None\n}\n");
    item("Decoy",
         "Item Range=400\nProcess=CProcess\n{\nProcess Type=Item use\nProcess Activity=Array\n{\n"
         "Item Count=1\nProcess Activity=CDropItemAI\n{\nDestination Action=CMultipleActionsAction\n{\n"
         "Action=Array\n{\nItem Count=2\nAction=CDropItemAtTargetAction\n{\nModel to Create=Props/Box\n"
         "New Name=Decoy 1\nDeploy Sequence=Idle\n}\nAction=CUseItemOnTargetAction\n{\n"
         "Inventory Item Definition=Decoy\n}\n}\n}\n}\n}\n}\nDefault Inventory Item=CStarTrekCountInventoryItem\n"
         "{\nRounds=2\n}\n");
    item("Phaser",
         "Weapon Damage=200\nReuse Delay=0.5\nItem Range=375\nDischarge Action=CShootEnergyWeaponAction\n{\n}\n");
    item("Grenade", "Item Range=500\nProjectile=Shots/Grenade\nDischarge Action=CThrowProjectileWeaponAction\n{\n"
                    "Height=30\nGravity AI=CThrowGravityAI\n{\nGravity=17\n}\n}\n"
                    "Default Inventory Item=CStarTrekProjectileWeaponInventoryItem\n{\nRounds=1\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Band", -1}, {"Decoy", 2}, {"Phaser", -1}, {"Grenade", 1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    const MapScreen::Officer &a = screen.officers()[0];
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    // Cloaked, the watcher doesn't see her; the band drains at its Energy
    // Use a second, and she fades to a quarter.
    screen.choose(0);
    screen.chooseItem(0, 0);
    screen.toggleCharge();
    run(2000);
    CHECK(a.items[0].on && std::abs(a.items[0].energy - 91.0f) < 0.5f && a.opacity == 0.25f);
    CHECK(enemies[0].state == "Patrol");
    // The stasis field's oval holds the guard in it till it's told it's
    // done; Ann's own doesn't hold her.
    CHECK(enemies[1].held == MapScreen::kHeldForever);
    TextObject done;
    CHECK(!readTextObject("CSendAIDoneMessage\n{\nTarget=Trapped\n}\n", done));
    screen.runAction(&done);
    CHECK(enemies[1].held == 0.0f);
    // A radius's action on Electronic Equipment, as an EM pulse's: the
    // camera's held 15 seconds, the others not.
    TextObject pulse;
    CHECK(!readTextObject("CDoOnAllWithinRadiusAction\n{\nTarget Entity=Camera\nWho To Affect=Electronic Equipment\n"
                          "Radius=2000\nY To X Ratio=1\nTouching Action=CAssignOverrideTemporaryTaskAction\n{\n"
                          "Target=$instigator\nTemporary Task AI=CWaitAI\n{\nTime To Wait=15\n}\n}\n}\n",
                          pulse));
    screen.runAction(&pulse);
    CHECK(enemies[2].held == 15.0f && enemies[0].held == 0.0f && enemies[3].held == 0.0f);
    run(3000);
    CHECK(std::abs(enemies[2].held - 14.0f) < 0.01f);
    // The decoy put down where it's used, within its range, one used up.
    screen.chooseItem(0, 1);
    CHECK(screen.useItem(500.0f, 400.0f));
    run(6000);
    float x = 0.0f;
    float y = 0.0f;
    CHECK(screen.partPosition("Decoy 1", x, y) && x == 500.0f && y == 400.0f && a.items[1].count == 1);
    // Off, she's seen.
    screen.chooseItem(0, 0);
    screen.toggleCharge();
    run(8000);
    CHECK(!a.items[0].on && a.opacity == 1.0f && enemies[0].state != "Patrol");
    // A grenade flies up and comes down where it's thrown.
    screen.chooseItem(0, 3);
    CHECK(screen.useItem(300.0f, 500.0f));
    float highest = 0.0f;
    for (int k = 0; k < 40; ++k) {
        run(now + 50);
        if (!screen.thrown().empty())
            highest = std::max(highest, screen.thrown()[0].z);
    }
    CHECK(highest > 30.0f);
}

void testProjectiles() {
    // A grenade thrown east at a wall: it passes a trigger answering to
    // anything, collideable as that is, which turns a lamp on, and is turned back off the wall, low
    // and unseen as that is, high as the grenade flies. A touching trigger
    // answering to anything turns a second lamp on as it comes in, and off
    // as it goes off; one answering to projectiles doesn't answer to it.
    // Ann stands in a collideable outline, which lets the grenade out.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=300.000000,300.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CFreeRangePoly\n{\nName=Wall\nVisible=0\nActive=1\nCollideable=1\nFull Height=0\n"
        "Polygon=400, 100, 420, 100, 420, 500, 400, 500, 400, 100\n}\n"
        "Level Part=CFreeRangePoly\n{\nVisible=1\nActive=1\nCollideable=1\nActivity=Array\n{\nItem Count=1\n"
        "Activity=CPassThroughTriggerAI\n{\nTriggered By Anything=1\n"
        "Action=CActivateAction\n{\nTarget Name=Lamp\n}\n}\n}\n"
        "Polygon=340, 100, 380, 100, 380, 500, 340, 500, 340, 100\n}\n"
        "Level Part=CEntityBase\n{\nName=Lamp\nVisible=1\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nPosition X=360\nPosition Y=300\nActivity=Array\n{\nItem Count=2\n"
        "Activity=CTouchingOvalTriggerAI\n{\nTriggered By Anything=1\nX Radius=30\nYX Ratio=10\n"
        "Enter Action=CActivateAction\n{\nTarget Name=Second Lamp\n}\n"
        "Exit Action=CDeactivateAction\n{\nTarget Name=Second Lamp\n}\n}\n"
        "Activity=CTouchingOvalTriggerAI\n{\nTriggered By Projectiles=1\nX Radius=30\nYX Ratio=10\n"
        "Enter Action=CActivateAction\n{\nTarget Name=Third Lamp\n}\n}\n}\n}\n"
        "Level Part=CEntityBase\n{\nName=Second Lamp\nVisible=1\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nName=Third Lamp\nVisible=1\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n"
        "Level Part=CFreeRangePoly\n{\nVisible=1\nActive=1\nCollideable=1\nFull Height=1\n"
        "Polygon=280, 280, 320, 280, 320, 320, 280, 320, 280, 280\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Shots/Grenade.mdl16"] =
        modelFile("Grenade", 1, 1, 1.0f, {}, 0.0f, false, 400.0f, grenadeLook());
    data.files["Inventory/Item Definitions/Grenade.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\nDisplay In Inventory=1\nItem Range=500\n"
                "Projectile=Shots/Grenade\nDischarge Action=CThrowProjectileWeaponAction\n{\nHeight=30\n"
                "Gravity AI=CThrowGravityAI\n{\nGravity=17\n}\n}\n"
                "Default Inventory Item=CStarTrekProjectileWeaponInventoryItem\n{\nRounds=1\n}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Grenade", 1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    uint64_t now = 0;
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(600.0f, 300.0f));
    float furthest = 0.0f;
    bool lit = false;
    for (int k = 0; k < 60; ++k) {
        screen.update(now += 50);
        if (!screen.thrown().empty())
            furthest = std::max(furthest, screen.thrown()[0].x);
        lit = lit || screen.partState(4).active;
    }
    CHECK(furthest > 340.0f && furthest <= 400.0f && screen.partState(2).active); // stopped at the wall's face
    CHECK(lit && !screen.partState(5).active);
    for (int k = 0; k < 200 && !screen.thrown().empty(); ++k)
        screen.update(now += 50);
    CHECK(screen.thrown().empty() && !screen.partState(4).active);
}

void testStates() {
    // Guards whose states' actions and transitions turn relays' arrows
    // on: one stunned (its Stun's transition), one with no Valid Targets,
    // one that sees a body, one recharging, and one beamed out by its own
    // alarm.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto guard = [](const std::string &name, int x, int y, float angle, const std::string &ai,
                          const std::string &more) {
        return "Level Part=CEntityWalking\n{\nName=" + name +
               "\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\nMax Distance=400\nVision Cone=360\n" + ai +
               "}\n}\nModel=Characters/Guard\nPosition X=" + std::to_string(x) + "\nPosition Y=" + std::to_string(y) +
               "\nFacing Angle=" + std::to_string(angle) + "\n" + more + "}\n";
    };
    const auto lamp = [](const std::string &name) {
        return "Level Part=CEntityBase\n{\nName=" + name + "\nActive=0\nPosition X=0\nPosition Y=0\n}\n";
    };
    const auto activate = [](const std::string &name) { return "CActivateAction\n{\nTarget Name=" + name + "\n}\n"; };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=3000\nHeight=3000\nView Position=500.000000,300.000000\nTree List=CSortList2D\n{\n" +
        guard("Stunned", 1500, 1500, 0.0f,
              "Current State=Patrol\nStun=CBorgStateStun\n{\nNext State=Investigate\nStart Action=" +
                  activate("Stun Start") +
                  "}\nTransitions=CBorgTransitionHolder\n{\nTransition=Array\n{\n"
                  "Item Count=1\nTransition=CBorgTransition\n{\nTrigger=Every Time\nFrom State=Stun\nTo State=Any\n"
                  "Action=" +
                  activate("Stun Over") + "}\n}\n}\n",
              "") +
        guard("Prisoner", 400, 300, 0.0f,
              "Current State=Patrol\nTargeting=CBorgSetClosestTarget\n{\nValid Targets=\n}\n", "") +
        guard("Looker", 2500, 2500, 0.0f,
              "Current State=Patrol\nGotATarget=CBorgStateGotATarget\n{\nDead Body=Investigate Dead Body\n}\n"
              "InvestigateDeadBody=CBorgStateInvestigateDeadBody\n{\nStart Action=" +
                  activate("Body Found") +
                  "Investigate Dead Body AI=CChaseInvestigateAI\n{\nTime To Investigate=1\n}\n}\n",
              "") +
        guard("Body", 2600, 2500, 0.0f, "Current State=Patrol\n", "Category=Dead Bodies\n") +
        guard("Sleeper", 2500, 500, 0.0f,
              "Current State=Recharge\nRecharge=CBorgStateRecharge\n{\nTime To Recharge=2\n}\n"
              "EndRecharge=CBorgStateEndRecharge\n{\nEnd Recharge AI=CGoForwardAI\n{\nDist to Move=50\n}\n}\n",
              "") +
        lamp("Stun Start") + lamp("Stun Over") + lamp("Body Found") +
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {ann});
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    const auto lit = [&](const char *name) {
        for (size_t i = 0; i < map.parts.size(); ++i)
            if (map.parts[i].name == name)
                return screen.partState(i).active;
        return false;
    };
    // The prisoner, Ann beside it, doesn't take her for a target.
    run(1000);
    CHECK(enemies[1].state == "Patrol" && enemies[1].target < 0);
    // Stunned: its Stun's Start Action; up again, the transition from Stun
    // and its Next State, Investigate.
    ItemDefinition stunner;
    stunner.stunDuration = 1.0f;
    screen.hurtEnemy(0, stunner, -1, true);
    CHECK(enemies[0].state == "Stun" && lit("Stun Start") && !lit("Stun Over"));
    run(2500);
    CHECK(lit("Stun Over") && enemies[0].state == "Investigate");
    // The looker sees the body: Investigate Dead Body, its Start Action,
    // then back on patrol, not looking at it again.
    CHECK(lit("Body Found"));
    run(8000);
    CHECK(enemies[2].state == "Patrol" && enemies[2].bodiesSeen.size() == 1);
    // The sleeper recharges its two seconds, goes 50 forward and patrols.
    CHECK(enemies[4].state == "Patrol" && std::abs(enemies[4].x - 2450.0f) < 1.0f);
    // Beamed out by an action naming it its instigator: gone once its warp
    // time's done.
    TextObject beam;
    CHECK(!readTextObject(
        "CAddEffectAction\n{\nWho To Warp=$Instigator\nWarp Behavior=CStarTrekTransporterEffectBehavior\n"
        "{\nWarp Time=1\nDelete When Finished=1\n}\n}\n",
        beam));
    screen.runAction(&beam, MapScreen::kEnemyInstigator - 4);
    run(8500);
    CHECK(!screen.partState(enemies[4].part).deleted);
    run(9500);
    CHECK(screen.partState(enemies[4].part).deleted);
}

void testDoors() {
    // A door that opens for anyone near (its anything-touching trigger,
    // Enemy and Player) and closes half a second after they've gone; a
    // trigger set off by enemies; a crate to bump into; the team's square.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,300.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CEntityAnimated\n{\nName=Door\nCollideable=1\nActivity=Array\n{\nItem Count=2\n"
        "Activity=CDoorAI\n{\nAfter Opened=CActivateAction\n{\nTarget Name=Opened\n}\n}\n"
        "Activity=CAnyTouchingOvalTriggerAI\n{\nTriggered by=Enemy,Player\nX Radius=120\nYX Ratio=1\n"
        "Enter Action=COpenDoorAction\n{\nDoor Name=$trigger\n}\nExit Action=CCloseDoorAction\n{\nDoor Name=$trigger\n}\n"
        "Exit Action Delay=0.5\n}\n}\nModel=Props/Door\nCur Sequence=Closed\nPosition X=400\nPosition Y=300\n}\n"
        "Level Part=CEntityBase\n{\nName=Tripwire\nActivity=Array\n{\nItem Count=1\nActivity=CTouchingOvalTriggerAI\n{\n"
        "Triggered By Enemies=1\nX Radius=40\nYX Ratio=1\nEnter Action=CActivateAction\n{\nTarget Name=Tripped\n}\n}\n}\n"
        "Position X=1500\nPosition Y=1500\n}\n"
        "Level Part=CEntityBase\n{\nName=Crate\nActivity=Array\n{\nItem Count=1\nActivity=CollideTriggerAI\n{\n"
        "Triggered By Players=1\nAction=CActivateAction\n{\nTarget Name=Bumped\n}\n}\n}\n"
        "Position X=300\nPosition Y=600\n}\n"
        "Level Part=CRenderablePolygon\n{\nName=Square\nPolygon=250, 850, 350, 850, 350, 950, 250, 950, 250, 850\n"
        "Activity=Array\n{\nItem Count=1\nActivity=CTeamTouchingPolygonTriggerAI\n{\nTrigger Only Once=1\n"
        "Action=CActivateAction\n{\nTarget Name=Gathered\n}\n}\n}\n}\n"
        "Level Part=CEntityWalking\n{\nName=Walker\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\n"
        "Current State=Patrol\nMax Distance=10\nInvestigate=CBorgStateInvestigate\n{\nInvestigate AI=CChaseInvestigateAI\n{\n"
        "Time To Investigate=1\n}\n}\n}\n}\nModel=Props/Door\nPosition X=1400\nPosition Y=1500\n}\n"
        "Level Part=CEntityBase\n{\nName=Opened\nActive=0\n}\nLevel Part=CEntityBase\n{\nName=Tripped\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nName=Bumped\nActive=0\n}\nLevel Part=CEntityBase\n{\nName=Gathered\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Props/Door.mdl16"] = modelFile("Door", 1, 1);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    MapScreen screen(data, rules, map, options, 0, {ann});
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    const auto part = [&](const char *name) {
        for (size_t i = 0; i < map.parts.size(); ++i)
            if (map.parts[i].name == name)
                return i;
        return size_t(0);
    };
    // Ann's 100 off the door: it opens, out of the way, After Opened run.
    CHECK(screen.partState(part("Door")).collideable);
    run(500);
    CHECK(!screen.partState(part("Door")).collideable && screen.partState(part("Opened")).active);
    // She walks off: half a second on, it's closed.
    screen.choose(0);
    screen.order(300.0f, 600.0f);
    run(10000);
    CHECK(screen.partState(part("Door")).collideable);
    // She bumped into the crate on the way; the walker's not on its wire.
    CHECK(screen.partState(part("Bumped")).active && !screen.partState(part("Tripped")).active);
    // The walker sent onto the wire sets it off.
    TextObject send;
    CHECK(!readTextObject("CSendInvestigateHereMessageAction\n{\nTarget=Walker\n}\n", send));
    screen.runAction(&send, -1, int(part("Tripwire")));
    run(14000);
    CHECK(screen.partState(part("Tripped")).active);
    // The team in its square.
    screen.order(300.0f, 900.0f);
    run(24000);
    CHECK(screen.partState(part("Gathered")).active);
}

void testCompany() {
    // Ann and Bo; a prisoner to rescue, another to join the team, a third
    // held still; a lamp lit for a scientist.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto character = [](const std::string &name, int x, int y) {
        return "Level Part=CEntityWalking\n{\nName=" + name +
               "\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\nCurrent State=Patrol\n"
               "Targeting=CBorgSetClosestTarget\n{\nValid Targets=\n}\n}\n}\nModel=Characters/Guard\nPosition X=" +
               std::to_string(x) + "\nPosition Y=" + std::to_string(y) + "\n}\n";
    };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,300.000000\nTree List=CSortList2D\n{\n" +
        character("Prisoner", 1500, 1500) + character("Helper", 1200, 300) + character("Sleeper", 1500, 300) +
        "Level Part=CEntityBase\n{\nName=Lamp\nActive=0\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 2\nPosition X=400\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Levels/Test.RDS"] = bytesOf(":Rescue\r\nRescue %s.\r\n");
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1);
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.field = "Science";
    TeamMember bo;
    bo.firstName = "Bo";
    bo.inventory = {{"Phaser", 1}};
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    const auto &officers = screen.officers();
    const auto &enemies = screen.enemies();
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    const auto act = [&](const std::string &text) {
        TextObject action;
        CHECK(!readTextObject(text, action));
        screen.runAction(&action);
    };
    // With no waypoints to go round by, Ann walks to the far side of Bo
    // through him, one character not stopping another; sent onto his
    // spot, she goes onto it.
    screen.choose(0);
    screen.order(500.0f, 300.0f);
    float nearest = 1e9f;
    for (int k = 0; k < 200; ++k) {
        run(now + 50);
        nearest = std::min(nearest, std::hypot(officers[0].x - officers[1].x, officers[0].y - officers[1].y));
    }
    CHECK(std::hypot(officers[0].x - 500.0f, officers[0].y - 300.0f) < 2.0f && nearest < 2.0f);
    screen.order(officers[1].x, officers[1].y);
    run(now + 6000);
    CHECK(officers[0].path.empty() && std::hypot(officers[0].x - officers[1].x, officers[0].y - officers[1].y) < 2.0f);
    // The prisoner's rescue, its words naming them; killed, it fails.
    act("CAddMissionObjectiveAction\n{\nObjective=CRescueMissionObjective\n{\nObjective Name=Rescue\n"
        "Mission Status=Incomplete\nDisplay Status=Visible\nObjective Script=Test\nObjective Line=Rescue\n"
        "WhoToRescue=Prisoner\n}\n}\n");
    CHECK(screen.objectiveLines().size() == 1 && screen.objectiveLines()[0].text == "Rescue Prisoner.");
    ItemDefinition blast;
    blast.weaponDamage = 1000.0f;
    screen.hurtEnemy(0, blast, -1, false);
    CHECK(screen.objectives()[0].status == ObjectiveStatus::kFailed);
    // The helper, to be rescued too, joins the team where it stands, its
    // part gone: its rescue's done. A need-item objective for Bo's
    // phaser holds while Bo lives, and fails as Bo dies, no one else
    // having one.
    act("CAddMissionObjectiveAction\n{\nObjective=CRescueMissionObjective\n{\nObjective Name=Rescue Helper\n"
        "Mission Status=Incomplete\nWhoToRescue=Helper\n}\n}\n");
    act("CAddMissionObjectiveAction\n{\nObjective=CNeedItemMissionObjective\n{\nObjective Name=Keep\n"
        "Mission Status=Incomplete\nItem Needed=Phaser\n}\n}\n");
    act("CAddToTeamAction\n{\nWho To Add=Helper\n}\n");
    CHECK(officers.size() == 3 && screen.team()[officers[2].member].name() == "Helper" && officers[2].x == 1200.0f &&
          screen.partState(enemies[1].part).deleted);
    CHECK(screen.objectives()[1].status == ObjectiveStatus::kComplete);
    screen.hurtEnemy(2, blast, -1, false);
    CHECK(screen.objectives()[2].status == ObjectiveStatus::kIncomplete);
    screen.hurtOfficer(1, blast, false);
    CHECK(!officers[1].alive() && screen.objectives()[2].status == ObjectiveStatus::kFailed);
    // The sleeper paused and let go.
    act("CPauseActorAction\n{\nTarget=Sleeper\n}\n");
    CHECK(enemies[2].held == MapScreen::kHeldForever);
    act("CUnpauseActorAction\n{\nTarget=Sleeper\n}\n");
    CHECK(enemies[2].held == 0.0f);
    // Ann, of Science, lights the lamp; Bo doesn't.
    act("CConditionalAction\n{\nTry Action=CCheckForClassAction\n{\nWho To Check=Bo\nClass=Science\n}\n"
        "Succeed Action=CActivateAction\n{\nTarget Name=Lamp\n}\n}\n");
    CHECK(!screen.partState(3).active);
    act("CConditionalAction\n{\nTry Action=CCheckForClassAction\n{\nWho To Check=Ann\nClass=Science\n}\n"
        "Succeed Action=CActivateAction\n{\nTarget Name=Lamp\n}\n}\n");
    CHECK(screen.partState(3).active);
}

void testBeams() {
    // Ann's phaser, a beam weapon, at a guard.
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const std::string zax =
        "CLayerSaveData\n{\nWidth=2000\nHeight=2000\nView Position=500.000000,300.000000\nTree List=CSortList2D\n{\n"
        "Level Part=CEntityWalking\n{\nName=Guard\nActivity=Array\n{\nItem Count=1\nActivity=CBorgAI\n{\n"
        "Current State=Patrol\nMax Distance=10\n}\n}\nModel=Characters/Guard\nPosition X=500\nPosition Y=300\n}\n"
        "Level Part=CEntityBase\n{\nName=Away Team Spawn Point 1\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = bytesOf(zax);
    data.files["Cache/Models/Characters/Guard.mdl16"] = modelFile("Guard", 1, 1, 1000.0f);
    data.files["Inventory/Item Definitions/Phaser.txt"] = bytesOf(
        "CStarTrekInventoryItemDefinition\n{\nWeapon Damage=1\nReuse Delay=5\nItem Range=375\nShot Duration=0.5\n"
        "Discharge Action=CShootEnergyWeaponAction\n{\nForward Position=20\nLateral Position=-5\nHeight=30\n"
        "Outside Color=255,83,0\nMiddle Color=255,255,0\n}\n}\n");
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Phaser", -1}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    uint64_t now = 0;
    screen.choose(0);
    screen.attack(0);
    for (int k = 0; k < 10 && screen.beams().empty(); ++k)
        screen.update(now += 50);
    // From her muzzle, 20 on towards the guard and 5 across, 30 up, to the
    // guard as high; in its colours.
    CHECK(screen.beams().size() == 1);
    const MapScreen::Beam &b = screen.beams()[0];
    CHECK(b.x0 == 320.0f && b.y0 == 300.0f - 5.0f - 30.0f && b.x1 == 500.0f && b.y1 == 270.0f);
    CHECK((b.outside == std::array<uint8_t, 3>{255, 83, 0}) && (b.middle == std::array<uint8_t, 3>{255, 255, 0}));
    // Gone after its Shot Duration.
    const uint64_t shot = now;
    while (now < shot + 600)
        screen.update(now += 50);
    CHECK(screen.beams().empty());
}

void testChoosing() {
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(kMap, kMap + std::string(kMap).size());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=64,64\n\t}\n}\n",
                          rules));
    GameOptions options;
    options.pauseWhenHit = false; // the hits here don't stop time
    TeamMember a;
    a.firstName = "Ann";
    TeamMember b;
    b.firstName = "Bob";
    MapScreen screen(data, rules, map, options, 0, {a, b});
    // The first chosen to start with; both at the one spawn point there is
    // being only for the first, the second where the view starts.
    CHECK(screen.selected() == 0 && screen.officers()[0].chosen && !screen.officers()[1].chosen);
    screen.chooseAll();
    CHECK(screen.officers()[0].chosen && screen.officers()[1].chosen);
    // Shift-click takes one away; a box about the other chooses them alone.
    screen.choose(0, true);
    CHECK(!screen.officers()[0].chosen && screen.selected() == 1);
    screen.chooseIn(10, 30, 30, 50);
    CHECK(screen.officers()[0].chosen && !screen.officers()[1].chosen && screen.selected() == 0);
    // Sent together: side by side, the first on the place.
    screen.chooseAll();
    screen.order(60.0f, 60.0f);
    screen.update(10000);
    CHECK(screen.officers()[0].x == 60.0f && screen.officers()[0].y == 60.0f);
    CHECK(std::abs(screen.officers()[1].x - 60.0f) > 1.0f || std::abs(screen.officers()[1].y - 60.0f) > 1.0f);
    // How they move, all at once.
    screen.setMove(1);
    CHECK(screen.officers()[0].move == 1 && screen.officers()[1].move == 1);
}

void testWater() {
    TextObject o;
    CHECK(!readTextObject("CWaterWarpEffectFrame\n{\n\tH1 Wave=CWaterWaveFormData\n\t{\n\t\tNum Waves=1\n"
                          "\t\tAmplitude=2\n\t\tPixels Per Second=10\n\t}\n\tRender Type=Horizontal\n"
                          "\tStatic Texture=Planet1/Water 5\n}\n",
                          o));
    const WaterWarp warp = readWaterWarp(o);
    CHECK(warp.order == WaterWarp::Order::kHorizontal && warp.texture == "Planet1/Water 5");
    // A cosine once over the 128: 2 at the start, -2 halfway, 0 a quarter.
    CHECK(warp.h1.shift[0] == 2 && warp.h1.shift[64] == -2 && warp.h1.shift[32] == 0);
    CHECK(warp.v1.shift[0] == 0);

    // Each pixel's red its column: row 0 is shifted 2 along at the start,
    // and by a second at 10 pixels a second, row 0 is as row 10 was.
    image::Image tex;
    tex.w = 128;
    tex.h = 128;
    tex.rgba.assign(size_t(128) * 128 * 4, 255);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            tex.pixel(x, y)[0] = uint8_t(x);
    image::Image out;
    warpWater(warp, tex, 0.0, out);
    CHECK(out.w == 128 && out.pixel(0, 0)[0] == 2 && out.pixel(127, 0)[0] == 1);
    CHECK(out.pixel(5, 64)[0] == 3);
    warpWater(warp, tex, 1.0, out);
    CHECK(out.pixel(0, 0)[0] == uint8_t(warp.h1.shift[10]));
}

void testFractal() {
    // Copies of a generator give the same numbers; another seed others.
    R250 a = groundGenerator(7);
    R250 b = groundGenerator(7);
    R250 c = groundGenerator(8);
    bool same = true;
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        const uint32_t x = a.next();
        same = same && x == b.next();
        differs = differs || x != c.next();
    }
    CHECK(same && differs);

    // Two cells side by side share their edge, made by the same seeds;
    // their corners are the map's.
    GroundMap g;
    g.width = 3;
    g.height = 2;
    g.values = {10, 200, 90, 250, 40, 128};
    g.seeds = {0, 5, 9, 3, 0, 7};
    const std::vector<uint8_t> left = groundFractal(g, 0, 0);
    const std::vector<uint8_t> right = groundFractal(g, 1, 0);
    const size_t side = kGroundCell + 1;
    CHECK(left.size() == side * side && left[0] == 10 && left[kGroundCell] == 200);
    CHECK(left[side * kGroundCell] == 250 && left[side * kGroundCell + kGroundCell] == 40);
    bool shared = true;
    bool filled = true;
    for (size_t y = 0; y < side; ++y)
        shared = shared && left[y * side + kGroundCell] == right[y * side];
    for (size_t i = 0; i < left.size(); ++i)
        filled = filled && left[i] != 0;
    CHECK(shared && filled);
    CHECK(groundFractal(g, 0, 0) == left);
}

} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.maps",
           {
               {"read", [](const std::filesystem::path &) { testRead(); }},
               {"ground", [](const std::filesystem::path &) { testGround(); }},
               {"paths", [](const std::filesystem::path &) { testPaths(); }},
               {"team", [](const std::filesystem::path &) { testTeam(); }},
               {"scripts", [](const std::filesystem::path &) { testScripts(); }},
               {"triggers", [](const std::filesystem::path &) { testTriggers(); }},
               {"enemies", [](const std::filesystem::path &) { testEnemies(); }},
               {"closest", [](const std::filesystem::path &) { testClosest(); }},
               {"foes", [](const std::filesystem::path &) { testFoes(); }},
               {"hostage", [](const std::filesystem::path &) { testHostage(); }},
               {"pause when hit", [](const std::filesystem::path &) { testPauseWhenHit(); }},
               {"projectiles", [](const std::filesystem::path &) { testProjectiles(); }},
               {"drawing order", [](const std::filesystem::path &) { testDrawingOrder(); }},
               {"choosing", [](const std::filesystem::path &) { testChoosing(); }},
               {"combat", [](const std::filesystem::path &) { testCombat(); }},
               {"noise", [](const std::filesystem::path &) { testNoise(); }},
               {"objectives", [](const std::filesystem::path &) { testObjectives(); }},
               {"enemy AI", [](const std::filesystem::path &) { testEnemyAI(); }},
               {"items", [](const std::filesystem::path &) { testItems(); }},
               {"overload", [](const std::filesystem::path &) { testOverload(); }},
               {"throw range", [](const std::filesystem::path &) { testThrowRange(); }},
               {"meld", [](const std::filesystem::path &) { testMeld(); }},
               {"maps", [](const std::filesystem::path &) { testMaps(); }},
               {"interface", [](const std::filesystem::path &) { testInterface(); }},
               {"full inventory", [](const std::filesystem::path &) { testFullInventory(); }},
               {"gadgets", [](const std::filesystem::path &) { testGadgets(); }},
               {"processes", [](const std::filesystem::path &) { testProcesses(); }},
               {"polygon processes", [](const std::filesystem::path &) { testPolygonProcesses(); }},
               {"standing in the way", [](const std::filesystem::path &) { testStandingInTheWay(); }},
               {"scripts hold actors", [](const std::filesystem::path &) { testScriptsHoldActors(); }},
               {"door ways", [](const std::filesystem::path &) { testDoorWays(); }},
               {"kit", [](const std::filesystem::path &) { testKit(); }},
               {"states", [](const std::filesystem::path &) { testStates(); }},
               {"doors", [](const std::filesystem::path &) { testDoors(); }},
               {"company", [](const std::filesystem::path &) { testCompany(); }},
               {"beams", [](const std::filesystem::path &) { testBeams(); }},
               {"water", [](const std::filesystem::path &) { testWater(); }},
               {"fractal", [](const std::filesystem::path &) { testFractal(); }},
           });
