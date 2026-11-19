// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Saved games, read and written.

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

void testSaveGame() {
    SaveGame s;
    s.description = "TEST";
    s.time = "10:41";
    s.date = "11-02-06";
    s.weather = 1;
    s.location = "calais.hall";
    s.scene = "Locations\\Calais.Hall\\Calais.Hall_03_06b.ac2";
    s.actMajor = 6, s.actMinor = 2;
    s.act.postLoad = "scene calais.hall_03 1 0, fade in";
    s.player = SaveGame::Placement{{1, 2, 3}, {0, 1.5f, 0}};
    s.tags = {{"poirotleave01", 0}, {"Sirkeci.Market_02.ac2 dia_cleaver 0", 2}};
    s.timers = {{"hamtimera", 298}};
    s.texts = {{"savedscene", "calais.hall_03.ac2 -1 0"}};
    s.positions.push_back({"Sirkeci.Market_01.ac2", "klaus_model.gr2", {{-1121.6f, 0, -1513.8f}, {0, 2.27f, 0}}});
    s.talks.push_back({"dia_fountain", "01a", 1, 96, 1, 0});
    s.items.push_back({"ticket", 2, 0, 0});
    const std::vector<uint8_t> data = writeSaveGame(s);
    // The tags as the game writes them: "DESC" is the bytes "CSED".
    CHECK(data.size() > 4 && std::memcmp(data.data(), "CSED", 4) == 0);
    SaveGame back;
    CHECK(!readSaveGame(data, back));
    CHECK(back.description == "TEST" && back.date == "11-02-06" && back.weather == 1 && back.actMinor == 2);
    CHECK(back.scene == s.scene && back.act.postLoad == s.act.postLoad && back.player && !back.follower);
    CHECK(back.player->rotation[1] == 1.5f && back.tags.size() == 2 && back.tags[1].second == 2);
    CHECK(back.timers[0].second == 298 && back.texts[0].second == "calais.hall_03.ac2 -1 0");
    CHECK(back.positions.size() == 1 && back.positions[0].character == "klaus_model.gr2");
    CHECK(back.talks.size() == 1 && back.talks[0].thread == 96 && back.talks[0].nest == 0);
    CHECK(back.items.size() == 1 && back.items[0].name == "ticket");
    std::string d, date, time;
    CHECK(!readSaveHeader(data, d, date, time) && d == "TEST" && time == "10:41" && date == "11-02-06");
    // Cut short, or a record it doesn't know: an error.
    CHECK(readSaveGame(std::vector<uint8_t>(data.begin(), data.end() - 3), back));
    std::vector<uint8_t> bad = data;
    std::memcpy(bad.data(), "XXXX", 4);
    CHECK(readSaveGame(bad, back));
    // The thumbnail's BMP decodes again.
    std::vector<uint8_t> rgba(4 * 4 * 4, 0);
    rgba[0] = 255;
    image::Image img;
    CHECK(!image::decode(encodeBmp24(4, 4, rgba), img) && img.w == 4 && img.h == 4 && img.pixel(0, 0)[0] == 255);
}

} // namespace

static const reluva::test::Suite suite("awe.unit.saves", {
    {"save games", [](const std::filesystem::path&) { testSaveGame(); }},
});
