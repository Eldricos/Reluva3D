// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: .jkl levels read.
//
//   reluva-tests sith.unit.levels

#include "engines/sith/formats/level.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

#include <string>

using namespace reluva;
using namespace reluva::sith;

namespace {

void testRead() {
    Level level;
    CHECK(!parseLevel(testing::makeLevelFile(), level));
    CHECK(level.header.version == 1 && level.header.gravity == 4.0f && level.header.horizonSkyOffset[1] == 0.25f);
    CHECK(level.header.lodDistances[3] == 1.2f && level.header.gouraudDistance == 2.0f);
    // Lists end at "end", short of their counts.
    CHECK(level.sounds.size() == 2 && level.materials.size() == 2 && level.models.size() == 2);
    CHECK(level.models.size() == 2 && level.models[1] == "other.3do");
    CHECK(level.materials.size() == 2 && level.materials[1].xTile == 2.0f);
    CHECK(level.colormaps.size() == 1 && level.vertices.size() == 8 && level.texVertices.size() == 4);
    CHECK(level.adjoins.size() == 2 && level.adjoins[0].mirror == 1 && level.adjoins[1].flags == 7);
    CHECK(level.surfaces.size() == 4);
    if (level.surfaces.size() == 4) {
        const Level::Surface &floor = level.surfaces[0];
        CHECK(floor.material == 0 && floor.surfaceFlags == 4 && floor.geometryMode == 4 && floor.adjoin == -1);
        CHECK(floor.vertices.size() == 4 && floor.texVertices[2] == 2 && floor.intensities[3] == 2.0f);
        CHECK(floor.normal.z == 1.0f);
        CHECK(level.surfaces[1].adjoin == 0 && level.surfaces[1].texVertices[0] == -1);
        CHECK(level.surfaces[3].extraLight == 0.25f && level.surfaces[3].normal.z == -1.0f);
    }
    CHECK(level.sectors.size() == 2);
    if (level.sectors.size() == 2) {
        const Level::Sector &room = level.sectors[0];
        CHECK(room.ambientLight == 0.37f && room.extraLight == 0.1f && room.collideBox && room.sound == "00wind03.wav");
        CHECK(room.soundVolume == 0.5f && room.vertices.size() == 4 && room.firstSurface == 0 &&
              room.surfaceCount == 2);
        CHECK(!level.sectors[1].collideBox && level.sectors[1].flags == 1 && level.sectors[1].tint.x == 0.5f);
    }
    CHECK(level.aiClasses.size() == 1 && level.cogs.size() == 1 && level.cogs[0].values.size() == 3);
    CHECK(level.templates.size() == 2 && level.things.size() == 2);
    if (level.things.size() == 2) {
        const Level::Thing &box = level.things[1];
        CHECK(box.name == "box" && box.rotation.y == 90.0f && box.sector == 0 && box.params.size() == 2);
        // Settings: the thing's own, then its template's and the bases'.
        CHECK(level.setting(level.things[0], "type") == "player");
        CHECK(level.setting(level.things[0], "MODEL3D") == "test.3do");
        CHECK(level.setting(box, "numframes") == "2");
        CHECK(level.setting(box, "light").empty());
    }
}

// Mysteries of the Sith's surfaces: four lights a corner, the light then
// red, green and blue.
void testColouredLight() {
    const std::vector<uint8_t> file = testing::makeLevelFile();
    std::string text(file.begin(), file.end());
    const std::string jk = "3\t4,-1\t5,-1\t6,-1\t1\t1\t1\r\n";
    text.replace(text.find(jk), jk.size(), "3\t4,-1\t5,-1\t6,-1\t.9 .9 .8 .7\t1 1 1 1\t.5 .1 .2 .3\r\n");
    Level level;
    CHECK(!parseLevel(std::vector<uint8_t>(text.begin(), text.end()), level));
    if (level.surfaces.size() == 4) {
        const Level::Surface &s = level.surfaces[1];
        CHECK(s.intensities.size() == 3 && s.intensities[0] == 0.9f && s.intensities[2] == 0.5f);
        CHECK(s.colours.size() == 3 && s.colours[0].z == 0.7f && s.colours[2].x == 0.1f);
        CHECK(level.surfaces[0].colours.empty());
    }
}

void testBad() {
    const std::vector<uint8_t> file = testing::makeLevelFile();
    const std::string text(file.begin(), file.end());
    Level level;
    // A surface naming a vertex the level hasn't.
    std::string bad = text;
    bad.replace(bad.find("0,0\t1,1"), 3, "9,0");
    CHECK(parseLevel(std::vector<uint8_t>(bad.begin(), bad.end()), level));
    // A sector whose surfaces run past the level's.
    bad = text;
    bad.replace(bad.find("SURFACES\t2\t2"), 13, "SURFACES\t2\t5");
    CHECK(parseLevel(std::vector<uint8_t>(bad.begin(), bad.end()), level));
    // An unknown header line.
    bad = text;
    bad.replace(bad.find("Gouraud"), 7, "Phong  ");
    CHECK(parseLevel(std::vector<uint8_t>(bad.begin(), bad.end()), level));
}

} // namespace

static const reluva::test::Suite
    kSuite("sith.unit.levels", {
                                   {"read", [](const std::filesystem::path &) { testRead(); }},
                                   {"coloured light", [](const std::filesystem::path &) { testColouredLight(); }},
                                   {"bad", [](const std::filesystem::path &) { testBad(); }},
                               });
