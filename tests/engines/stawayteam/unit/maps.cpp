// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The maps: a .zax read, its ground's textures blended by their values,
// the polygons laid on it, and the camera kept on the map.

#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/ui/map_screen.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

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
    CHECK(map.parts.size() == 2 && map.parts[0].model == "Things/Crate" && map.parts[0].x == 40.5f);
    CHECK(map.parts[1].renderType == "Black" && map.parts[1].polygon.size() == 5);
    CHECK(map.parts[1].polygon[2].first == 8.0f && map.parts[1].onGround);
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

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.maps",
                                        {
                                            {"read", [](const std::filesystem::path &) { testRead(); }},
                                            {"ground", [](const std::filesystem::path &) { testGround(); }},
                                        });
