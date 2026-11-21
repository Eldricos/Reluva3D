// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The maps: a .zax read, its ground's textures blended by their values,
// the polygons laid on it, and the camera kept on the map.

#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/ui/map_screen.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cmath>
#include <string>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

// A 128 x 128 map: a 3 x 3 grid of values, so 2 x 2 cells; a model, and a
// black polygon over the first cell's top left.
const char *const kMap = "CLayerSaveData\n{\n\tWidth=128\n\tHeight=128\n\tView Position=100.000000,20.000000\n"
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
    CHECK(map.width == 128 && map.height == 128 && map.viewX == 100.0f && map.viewY == 20.0f);
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

void testCombat() {
    // A guard at (500, 500) facing left with a disruptor, a wall between
    // it and the officers at (300, 500), 20 x 80 about (400, 500).
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
        "\t\t\tModel=Characters/Guard\n\t\t\tPosition X=500\n\t\t\tPosition Y=500\n\t\t\tFacing Angle=0\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Wall\n\t\t\tVisible=1\n\t\t\tActive=1\n"
        "\t\t\tModel=Walls/Wall\n\t\t\tPosition X=400\n\t\t\tPosition Y=500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 1\n\t\t\tVisible=1\n"
        "\t\t\tPosition X=300\n\t\t\tPosition Y=500\n\t\t}\n"
        "\t\tLevel Part=CEntityBase\n\t\t{\n\t\t\tName=Away Team Spawn Point 2\n\t\t\tVisible=1\n"
        "\t\t\tPosition X=300\n\t\t\tPosition Y=480\n\t\t}\n"
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
    run(12000);
    CHECK(!screen.officers()[0].alive() && screen.officers()[0].health == 0.0f && !screen.officers()[0].chosen);
    screen.choose(0);
    CHECK(screen.selected() == -1);
    // With her down it goes back to its patrol; Bo, sent to shoot it,
    // walks towards it till he sees it, and two of his phaser's shots kill
    // it.
    CHECK(screen.enemyAt(500.0f, 500.0f) == 0);
    screen.choose(1);
    screen.attack(0);
    run(20000);
    CHECK(!guard.alive() && guard.state == "Dead" && guard.health == 0.0f);
    CHECK(screen.enemyAt(500.0f, 500.0f) == -1 && screen.officers()[1].attacking == -1);
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

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.maps",
                                        {
                                            {"read", [](const std::filesystem::path &) { testRead(); }},
                                            {"ground", [](const std::filesystem::path &) { testGround(); }},
                                            {"paths", [](const std::filesystem::path &) { testPaths(); }},
                                            {"team", [](const std::filesystem::path &) { testTeam(); }},
                                            {"scripts", [](const std::filesystem::path &) { testScripts(); }},
                                            {"triggers", [](const std::filesystem::path &) { testTriggers(); }},
                                            {"enemies", [](const std::filesystem::path &) { testEnemies(); }},
                                            {"choosing", [](const std::filesystem::path &) { testChoosing(); }},
                                            {"combat", [](const std::filesystem::path &) { testCombat(); }},
                                        });
