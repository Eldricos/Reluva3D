// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the front end's main menu, its buttons where the
// game's frame has them.

#include "engines/storm3d/shell/loading_screen.h"
#include "engines/storm3d/shell/main_menu.h"
#include "engines/storm3d/shell/single_player.h"

#include "tests/support/test.h"

#include <array>
#include <string>
#include <vector>

using reluva::storm3d::LoadingScreen;
using reluva::storm3d::MainMenu;
using reluva::storm3d::MenuChoice;
using reluva::storm3d::SinglePlayerScreen;

namespace {

// The labels' and the panels' sizes as the install's bitmaps and movies
// have them, in buttons()' order.
const std::vector<std::array<int, 4>> kSizes = {
    {136, 16, 226, 180}, {137, 16, 226, 174}, {100, 16, 370, 160}, {84, 15, 200, 200}, {67, 16, 0, 0}, {140, 16, 0, 0},
};

MenuChoice at(int x, int y) {
    const int i = MainMenu::buttonAt(x, y, kSizes);
    return i < 0 ? MenuChoice::kNone : MainMenu::buttons()[size_t(i)].choice;
}

// A label is its button, and so is its panel: the game's frame lit
// Options with the pointer over the construction yard.
void testButtons() {
    CHECK(MainMenu::buttons().size() == kSizes.size());
    CHECK(at(90, 18) == MenuChoice::kSinglePlayer);
    CHECK(at(100, 100) == MenuChoice::kSinglePlayer);
    CHECK(at(552, 18) == MenuChoice::kMultiplayer);
    CHECK(at(300, 300) == MenuChoice::kOptions);
    CHECK(at(450, 300) == MenuChoice::kCredits);
    CHECK(at(470, 458) == MenuChoice::kReplayIntro);
    // Quit's label is over the Credits panel's bottom rows: Quit.
    CHECK(at(560, 416) == MenuChoice::kQuit);
    CHECK(at(320, 10) == MenuChoice::kNone);
    CHECK(at(639, 479) == MenuChoice::kNone);
}

// The single-player screen's races: each panel 280 x 190, as the game's
// frame has them.
void testRaces() {
    CHECK(SinglePlayerScreen::races().size() == 4);
    CHECK(SinglePlayerScreen::raceAt(130, 150) == 0);
    CHECK(SinglePlayerScreen::raceAt(600, 100) == 1);
    CHECK(SinglePlayerScreen::raceAt(10, 430) == 2);
    CHECK(SinglePlayerScreen::raceAt(600, 300) == 3);
    CHECK(SinglePlayerScreen::raceAt(320, 240) == -1);
    CHECK(SinglePlayerScreen::raceAt(100, 445) == -1);
}

// mshell.set: six map names a campaign, "none" for none, as the
// install's (its last line without an end, a stray space).
void testCampaigns() {
    const std::string mshell = "fed1.bzn\r\nfed2.bzn\r\nfed5.bzn\r\nfed3.bzn\r\nnone\r\nnone\r\n"
                               "kling3.bzn\r\nkling1.bzn\r\nkling4.bzn\r\nkling5.bzn\r\nnone \r\nnone \r\n"
                               "finale4.bzn\r\nnone\r\nnone\r\nnone\r\nnone\r\nnone";
    const auto campaigns = SinglePlayerScreen::campaignsOf(mshell);
    CHECK(campaigns.size() == 3);
    CHECK((campaigns[0] == std::vector<std::string>{"fed1.bzn", "fed2.bzn", "fed5.bzn", "fed3.bzn"}));
    CHECK(campaigns[1].size() == 4 && campaigns[1][0] == "kling3.bzn");
    CHECK((campaigns[2] == std::vector<std::string>{"finale4.bzn"}));
}

// What the loading screen reads for a mission: each definition once,
// whatever its case, then its model.
void testLoadingFiles() {
    const std::vector<std::string> files =
        LoadingScreen::filesOf({"fente", "MBarisa", "fente", "mbarisa", "", "dlight"});
    CHECK((files == std::vector<std::string>{"fente.odf", "SOD/fente.sod", "MBarisa.odf", "SOD/MBarisa.sod",
                                             "dlight.odf", "SOD/dlight.sod"}));
}

} // namespace

static const reluva::test::Suite
    kSuite("storm3d.unit.shell", {
                                     {"buttons", [](const std::filesystem::path &) { testButtons(); }},
                                     {"races", [](const std::filesystem::path &) { testRaces(); }},
                                     {"campaigns", [](const std::filesystem::path &) { testCampaigns(); }},
                                     {"loading files", [](const std::filesystem::path &) { testLoadingFiles(); }},
                                 });
