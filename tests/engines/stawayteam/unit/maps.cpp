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
    TeamMember ann;
    ann.firstName = "Ann";
    ann.attributes = {{"Stealth", 30}};
    MapScreen screen(data, rules, map, options, 0, {ann});
    // A footstep's 100, by her Stealth 30 (0.85) and how she moves.
    const Animation::Noise step{2, 100.0f, 1.0f, "Stealth"};
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
    data.files["Cache/Models/Shots/Concussion.mdl16"] = modelFile("Concussion", 1, 1, 50.0f, {}, 150.0f, true, 400.0f);
    data.files["Cache/Models/Shots/Frag.mdl16"] = modelFile("Frag", 1, 1, 50.0f, {}, 60.0f, false, 400.0f);
    const auto bytesOf = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    const auto grenade = [&](const std::string &damage, const std::string &projectile) {
        return bytesOf(
            "CStarTrekInventoryItemDefinition\n{\n\tDisplay In Inventory=1\n\tWeapon Damage=" + damage +
            "\n\tStun Duration=30\n\tItem Range=500\n\tDischarge Sequence=Throw\n\tProjectile=" + projectile +
            "\n\tDischarge Action=CThrowProjectileWeaponAction\n\t{\n\t}\n"
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
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tScreen Pos=0,0\n"
                          "\t\tScreen Size=200,200\n\t}\n}\n",
                          rules));
    GameOptions options;
    TeamMember ann;
    ann.firstName = "Ann";
    ann.inventory = {{"Concussion Grenade", 2}, {"Frag Grenade", 1}, {"Hypospray", -1}};
    TeamMember bo;
    bo.firstName = "Bo";
    MapScreen screen(data, rules, map, options, 0, {ann, bo});
    const MapScreen::Officer &a = screen.officers()[0];
    const MapScreen::Officer &b = screen.officers()[1];
    // Her counts as the roster gives them, or the item's Rounds for -1.
    CHECK(a.items.size() == 3 && a.items[0].count == 2 && a.items[1].count == 1 && a.items[2].count == 5);
    CHECK(a.item == -1 && !screen.useItem(600.0f, 500.0f));
    uint64_t now = 0;
    const auto run = [&](uint64_t until) {
        while (now < until)
            screen.update(now += 50);
    };
    // A concussion grenade thrown at the guard, in range: it flies there,
    // and three seconds on stuns Guard A, in its 150, not Guard B.
    screen.choose(0);
    screen.chooseItem(0, 0);
    CHECK(screen.useItem(600.0f, 500.0f));
    run(100);
    CHECK(a.items[0].count == 1 && screen.thrown().size() == 1 && a.discharge == "Throw");
    run(1000);
    CHECK(screen.thrown()[0].x == 600.0f);
    run(3200);
    CHECK(screen.thrown().empty() && screen.enemies()[0].state == "Stun" && screen.enemies()[0].health == 100.0f);
    CHECK(screen.enemies()[1].state == "Patrol");
    // The frag grenade at Bo's feet: he's hurt, Ann, 100 off, isn't; then
    // there's none left.
    screen.chooseItem(0, 1);
    CHECK(screen.useItem(300.0f, 400.0f));
    run(7000);
    CHECK(b.health == 70.0f && a.health == 100.0f && a.items[1].count == 0);
    CHECK(!screen.useItem(300.0f, 400.0f));
    // The hypospray on Bo: she goes to him and heals him, up to his whole.
    screen.chooseItem(0, 2);
    CHECK(screen.useItem(300.0f, 420.0f));
    run(12000);
    CHECK(b.health == 100.0f && a.items[2].count == 4 && std::hypot(a.x - b.x, a.y - b.y) <= 30.0f);
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
                "\t\t\t\tModel to Create=Shots/Remote\n\t\t\t}\n\t\t}\n\t}\n"
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
                                            {"noise", [](const std::filesystem::path &) { testNoise(); }},
                                            {"objectives", [](const std::filesystem::path &) { testObjectives(); }},
                                            {"enemy AI", [](const std::filesystem::path &) { testEnemyAI(); }},
                                            {"items", [](const std::filesystem::path &) { testItems(); }},
                                            {"maps", [](const std::filesystem::path &) { testMaps(); }},
                                            {"interface", [](const std::filesystem::path &) { testInterface(); }},
                                            {"gadgets", [](const std::filesystem::path &) { testGadgets(); }},
                                        });
