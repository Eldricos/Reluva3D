// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the front end's screens, their buttons where the
// game's frames have them.

#include "engines/storm3d/shell/debriefing_screen.h"
#include "engines/storm3d/shell/game_setup_screen.h"
#include "engines/storm3d/shell/loading_screen.h"
#include "engines/storm3d/shell/main_menu.h"
#include "engines/storm3d/shell/multiplayer_screen.h"
#include "engines/storm3d/shell/options_screen.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/single_player.h"

#include "tests/support/test.h"

#include <array>
#include <string>
#include <vector>

using reluva::storm3d::DebriefingChoice;
using reluva::storm3d::DebriefingScreen;
using reluva::storm3d::GameSetupScreen;
using reluva::storm3d::LoadingScreen;
using reluva::storm3d::MainMenu;
using reluva::storm3d::ConnectionChoice;
using reluva::storm3d::MenuChoice;
using reluva::storm3d::MultiplayerScreen;
using reluva::storm3d::OptionsChoice;
using reluva::storm3d::OptionsMenu;
using reluva::storm3d::OptionsScreen;
using reluva::storm3d::SettingsPage;
using reluva::storm3d::SettingsScreen;
using reluva::storm3d::SinglePlayerScreen;

namespace {

// The labels' and the panels' sizes as the install's bitmaps and movies
// have them, in buttons()' order.
const std::vector<std::array<int, 4>> kSizes = {
    {136, 16, 226, 180}, {137, 16, 226, 174}, {100, 16, 370, 160}, {84, 15, 0, 0}, {67, 16, 200, 200}, {140, 16, 0, 0},
};

MenuChoice at(int x, int y) {
    const int i = MainMenu::buttonAt(x, y, kSizes);
    return i < 0 ? MenuChoice::kNone : MainMenu::buttons()[size_t(i)].choice;
}

// A label is its button, and so is its panel: the game's frames lit
// Options with the pointer over the construction yard, and Quit over the
// ship flying off.
void testButtons() {
    CHECK(MainMenu::buttons().size() == kSizes.size());
    CHECK(at(90, 18) == MenuChoice::kSinglePlayer);
    CHECK(at(100, 100) == MenuChoice::kSinglePlayer);
    CHECK(at(552, 18) == MenuChoice::kMultiplayer);
    CHECK(at(300, 300) == MenuChoice::kOptions);
    CHECK(at(450, 300) == MenuChoice::kQuit);
    CHECK(at(555, 441) == MenuChoice::kCredits);
    CHECK(at(470, 458) == MenuChoice::kReplayIntro);
    // Credits' label is over Quit's panel's bottom rows: Credits.
    CHECK(at(520, 440) == MenuChoice::kCredits);
    CHECK(at(560, 416) == MenuChoice::kQuit);
    CHECK(at(320, 10) == MenuChoice::kNone);
    CHECK(at(639, 479) == MenuChoice::kNone);
}

// The single-player screen's races: each panel 280 x 190, as the game's
// frame has them.
void testRaces() {
    CHECK(SinglePlayerScreen::races().size() == 5);
    CHECK(SinglePlayerScreen::raceAt(130, 150) == 0);
    CHECK(SinglePlayerScreen::raceAt(600, 100) == 1);
    CHECK(SinglePlayerScreen::raceAt(10, 430) == 2);
    CHECK(SinglePlayerScreen::raceAt(600, 300) == 3);
    CHECK(SinglePlayerScreen::raceAt(320, 240) == 4); // the finale's strip
    CHECK(SinglePlayerScreen::raceAt(320, 100) == -1);
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

// The Options screen's buttons, 324 x 32 and 112 x 30, where the game's
// frame has them; the gaps between them answer nothing.
// The debriefing's two plates: Continue across the middle, Quit To
// Windows on the right; nothing on the left, where others have Previous
// Menu.
void testDebriefing() {
    CHECK(DebriefingScreen::choiceAt(320, 460) == DebriefingChoice::kContinue);
    CHECK(DebriefingScreen::choiceAt(123, 445) == DebriefingChoice::kContinue);
    CHECK(DebriefingScreen::choiceAt(580, 460) == DebriefingChoice::kQuit);
    CHECK(DebriefingScreen::choiceAt(60, 460) == DebriefingChoice::kNone);
    CHECK(DebriefingScreen::choiceAt(320, 300) == DebriefingChoice::kNone);
}

void testOptions() {
    CHECK(OptionsScreen::buttons(OptionsMenu::kFrontEnd).size() == 3);
    CHECK(OptionsScreen::choiceAt(320, 208, OptionsMenu::kFrontEnd) == OptionsChoice::kGraphics);
    CHECK(OptionsScreen::choiceAt(158, 232, OptionsMenu::kFrontEnd) == OptionsChoice::kSound);
    CHECK(OptionsScreen::choiceAt(481, 303, OptionsMenu::kFrontEnd) == OptionsChoice::kGame);
    CHECK(OptionsScreen::choiceAt(320, 228, OptionsMenu::kFrontEnd) == OptionsChoice::kNone);
    CHECK(OptionsScreen::choiceAt(60, 460, OptionsMenu::kFrontEnd) == OptionsChoice::kPreviousMenu);
    CHECK(OptionsScreen::choiceAt(117, 460, OptionsMenu::kFrontEnd) == OptionsChoice::kNone);
    CHECK(OptionsScreen::choiceAt(560, 460, OptionsMenu::kFrontEnd) == OptionsChoice::kNone);
    // In a game: Surrender above, Abort Mission and Return to Game below,
    // and no Previous Menu.
    CHECK(OptionsScreen::buttons(OptionsMenu::kSkirmish).size() == 6);
    CHECK(OptionsScreen::choiceAt(320, 168, OptionsMenu::kSkirmish) == OptionsChoice::kSurrender);
    CHECK(OptionsScreen::choiceAt(320, 208, OptionsMenu::kSkirmish) == OptionsChoice::kGraphics);
    CHECK(OptionsScreen::choiceAt(320, 328, OptionsMenu::kSkirmish) == OptionsChoice::kAbort);
    CHECK(OptionsScreen::choiceAt(320, 368, OptionsMenu::kSkirmish) == OptionsChoice::kReturn);
    CHECK(OptionsScreen::choiceAt(60, 460, OptionsMenu::kSkirmish) == OptionsChoice::kNone);
    // A campaign's: Restate Objectives, Save Game and Load Game above, no
    // Surrender; Save Game not in a cut scene.
    CHECK(OptionsScreen::buttons(OptionsMenu::kCampaign).size() == 8);
    CHECK(OptionsScreen::choiceAt(320, 88, OptionsMenu::kCampaign) == OptionsChoice::kRestate);
    CHECK(OptionsScreen::choiceAt(320, 128, OptionsMenu::kCampaign) == OptionsChoice::kSaveGame);
    CHECK(OptionsScreen::choiceAt(320, 168, OptionsMenu::kCampaign) == OptionsChoice::kLoadGame);
    CHECK(OptionsScreen::choiceAt(320, 128, OptionsMenu::kCampaignCutScene) == OptionsChoice::kNone);
    CHECK(OptionsScreen::choiceAt(320, 368, OptionsMenu::kCampaign) == OptionsChoice::kReturn);
}

// The settings pages' controls where the game's frames have them, and
// sliders' steps and lines as the game's preferences and frames had them.
void testSettings() {
    using Control = SettingsScreen::Control;
    CHECK(SettingsScreen::controlAt(SettingsPage::kGraphics, 100, 130) == 0);
    CHECK(SettingsScreen::controlAt(SettingsPage::kGraphics, 450, 180) == 3);
    CHECK(SettingsScreen::controlAt(SettingsPage::kGraphics, 340, 225) == 4);
    CHECK(SettingsScreen::controlAt(SettingsPage::kSound, 422, 89) == 0);
    CHECK(SettingsScreen::controlAt(SettingsPage::kSound, 400, 269) == 4);
    CHECK(SettingsScreen::controlAt(SettingsPage::kGame, 400, 79) == 0);
    CHECK(SettingsScreen::controlAt(SettingsPage::kGame, 400, 379) == 6);
    CHECK(SettingsScreen::controlAt(SettingsPage::kGame, 200, 300) == -1);
    const Control &music = SettingsScreen::layoutOf(SettingsPage::kSound).controls[0];
    CHECK(SettingsScreen::sliderAfterClick(music, 10, 9) == 9);
    CHECK(SettingsScreen::lineOf(music, 9) == 153);
    CHECK(SettingsScreen::sliderAfterClick(music, 10, 214) == 10);
    CHECK(SettingsScreen::sliderAfterClick(music, 4, 100) == 4);
    const std::vector<Control> &game = SettingsScreen::layoutOf(SettingsPage::kGame).controls;
    CHECK(SettingsScreen::lineOf(game[1], 3) == 84);
    CHECK(SettingsScreen::lineOf(game[2], 1) == 80);
    CHECK(SettingsScreen::lineOf(game[3], 6) == 28);
    // Mouse scroll keeps tenths: 1 and three clicks of + is 1.3.
    double scroll = 1;
    for (int i = 0; i < 3; ++i)
        scroll = SettingsScreen::sliderAfterClick(game[2], scroll, 214);
    CHECK(scroll == 1.3);
    // Keyboard scroll, 6 and two clicks of −, stops at 0; game speed at 1.
    CHECK(SettingsScreen::sliderAfterClick(game[3], SettingsScreen::sliderAfterClick(game[3], 6, 3), 3) == 0);
    CHECK(SettingsScreen::sliderAfterClick(game[1], 1, 3) == 1);
    CHECK(SettingsScreen::lineOf(SettingsScreen::layoutOf(SettingsPage::kGraphics).controls[4], 3) == 126);
}

// The Multiplayer Connection screen's buttons where the game's frame has
// them, and the name box keeping printable letters, 31 at most.
void testConnection() {
    CHECK(MultiplayerScreen::choiceAt(320, 178) == ConnectionChoice::kInternet);
    CHECK(MultiplayerScreen::choiceAt(320, 244) == ConnectionChoice::kLan);
    CHECK(MultiplayerScreen::choiceAt(320, 310) == ConnectionChoice::kInstantAction);
    CHECK(MultiplayerScreen::choiceAt(320, 111) == ConnectionChoice::kNone);
    CHECK(MultiplayerScreen::choiceAt(60, 460) == ConnectionChoice::kPreviousMenu);
    CHECK(MultiplayerScreen::typed("Pic", "ard\t!") == "Picard!");
    CHECK(MultiplayerScreen::typed(std::string(30, 'x'), "yz") == std::string(30, 'x') + "y");
}

// Launch's rolls, as the game deals them: each player in turn a start
// not yet taken, by its roll among those left; a Random side the
// game's race for its roll, Federation, Borg, Romulan, Klingon.
void testLaunchRolls() {
    const std::vector<int> rolls = {0, 2};
    size_t next = 0;
    const auto below = [&](int n) { return rolls[next++] % n; };
    CHECK(GameSetupScreen::placeAtRandom({1, 2, 3, 4}, 2, below) == std::vector<int>({1, 4}));
    CHECK(GameSetupScreen::placeAtRandom({1, 2}, 3, [](int) { return 0; }) == std::vector<int>({1, 2}));
    const auto &sides = GameSetupScreen::sides();
    CHECK(std::string(sides[size_t(GameSetupScreen::randomSide(0))]) == "federation");
    CHECK(std::string(sides[size_t(GameSetupScreen::randomSide(1))]) == "borg");
    CHECK(std::string(sides[size_t(GameSetupScreen::randomSide(2))]) == "romulan");
    CHECK(std::string(sides[size_t(GameSetupScreen::randomSide(3))]) == "klingon");
}

// The Game Setup screen's controls where the game's frame has them: the
// slots' three lists, the options, the bottom bar a pixel right and down.
void testGameSetup() {
    CHECK(GameSetupScreen::targetAt(70, 63) == 0);
    CHECK(GameSetupScreen::targetAt(200, 82) == 101);
    CHECK(GameSetupScreen::targetAt(290, 196) == 702);
    CHECK(GameSetupScreen::targetAt(580, 66) == 1000);
    CHECK(GameSetupScreen::targetAt(580, 380) == 1013);
    CHECK(GameSetupScreen::targetAt(450, 280) == 1014);
    CHECK(GameSetupScreen::targetAt(590, 428) == 1019);
    CHECK(GameSetupScreen::targetAt(5, 460) == -1);
    CHECK(GameSetupScreen::targetAt(6, 460) == 2000);
    CHECK(GameSetupScreen::targetAt(320, 461) == 2001);
    CHECK(GameSetupScreen::targetAt(580, 461) == 2002);
    CHECK(GameSetupScreen::options().size() == 20);
    // Random sides take the races in turn by slot; a side chosen is its own.
    GameSetupScreen::Slot slot;
    CHECK(GameSetupScreen::raceOf(slot, 0) == "fed" && GameSetupScreen::raceOf(slot, 5) == "kling");
    slot.side = 4;
    CHECK(GameSetupScreen::raceOf(slot, 0) == "borg");
    CHECK(std::string(GameSetupScreen::colourName(2)) == "red" &&
          std::string(GameSetupScreen::colourName(9)) == "empty");
    reluva::storm3d::BznMap map;
    for (const char *name : {"Camera_1", "Camera_2", "Camera_4", "Camera_9", "ZOOMOUT"}) {
        reluva::storm3d::BznMap::Path p;
        p.name = name;
        map.paths.push_back(p);
    }
    CHECK(GameSetupScreen::playersOf(map) == 3);
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
                                     {"options", [](const std::filesystem::path &) { testOptions(); }},
                                     {"debriefing", [](const std::filesystem::path &) { testDebriefing(); }},
                                     {"settings", [](const std::filesystem::path &) { testSettings(); }},
                                     {"connection", [](const std::filesystem::path &) { testConnection(); }},
                                     {"game setup", [](const std::filesystem::path &) { testGameSetup(); }},
                                     {"launch rolls", [](const std::filesystem::path &) { testLaunchRolls(); }},
                                     {"loading files", [](const std::filesystem::path &) { testLoadingFiles(); }},
                                 });
