// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"

#include "common/serializer.h"
#include "engines/stgenerations/game/campaign.h"
#include "engines/stgenerations/game/optionsmenu.h"

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

// A made-up script in the shape of the game's.
const char* const kScript =
    "[Start]\r\n"
    "Intro\r\n"
    "Jump NewGame\r\n"
    "\r\n"
    "[NewGame]\r\n"
    "Movie 1\r\n"
    "SetFlag 3 TRUE\t\t\t;a comment\r\n"
    "ShowStellar\r\n"
    "Jump Planet\r\n"
    "[Planet]\r\n"
    "OnTransport PlanetPlay\r\n"
    "Jump Missed\r\n"
    "[PlanetPlay]\r\n"
    "ActionGame 4 PlanetWon PlanetLost\r\n"
    "[PlanetLost]\r\n"
    "IfNotFlag 3 Jump Missed\r\n"
    "IfFlag 3 IfActionGamePlayed 4 Movie 7\r\n"
    "Destroy Planet\r\n"
    "IfSunsDestroyed 1 Jump Fight\r\n"
    "Jump Missed\r\n"
    "[PlanetWon]\r\n"
    "AutoSave\r\n"
    "JumpRandom Missed Fight\r\n"
    "[Fight]\r\n"
    "OnTactical FightPlay\r\n"
    "Jump Won\r\n"
    "[FightPlay]\r\n"
    "SpaceCombat 2 Won Missed\r\n"
    "[Won]\r\n"
    "Movie 15\r\n"
    "EndGame\r\n"
    "[Missed]\r\n"
    "ScrollText 1\r\n"
    "Options\r\n";

Campaign::Action::Type next(Campaign& c, int* number = nullptr) {
    Campaign::Action a = c.next();
    if (number)
        *number = a.number;
    return a.type;
}

} // namespace

static void campaign_runs_the_script() {
    typedef Campaign::Action A;
    Campaign c;
    CHECK(!c.load("[Other]\nIntro\n"));
    CHECK(c.load(kScript));
    c.start("Start");
    int n = 0;
    CHECK_EQUAL(next(c), A::kIntro);
    CHECK_EQUAL(next(c, &n), A::kMovie);
    CHECK_EQUAL(n, 1);
    CHECK_EQUAL(next(c, &n), A::kActionGame);
    CHECK_EQUAL(n, 4);
    // Nothing runs while the mission does.
    CHECK_EQUAL(next(c), A::kNone);
    CHECK(c.isRunning());

    // Losing: the flags and the conditions, then a destroyed sun.
    c.missionEnded(false);
    CHECK_EQUAL(next(c, &n), A::kMovie);
    CHECK_EQUAL(n, 7);
    // No fleet is ever engaged: the section goes on.
    CHECK_EQUAL(next(c, &n), A::kMovie);
    CHECK_EQUAL(n, 15);
    CHECK_EQUAL(next(c), A::kEndGame);
    CHECK(!c.isRunning());
    CHECK_EQUAL(next(c), A::kNone);

    // Winning, then the second random choice.
    c.setRandom([](int count) { return count - 1; });
    c.start("NewGame");
    CHECK_EQUAL(next(c), A::kMovie);
    CHECK_EQUAL(next(c), A::kActionGame);
    c.missionEnded(true);
    CHECK_EQUAL(next(c), A::kAutoSave);

    // A save made here carries on the same way.
    MemoryWriteStream out;
    Serializer save(nullptr, &out);
    c.sync(save);
    Campaign d;
    d.load(kScript);
    d.setRandom([](int count) { return count - 1; });
    MemoryReadStream in(out.data());
    Serializer load(&in, nullptr);
    d.sync(load);
    CHECK(!load.err());
    CHECK_EQUAL(d.getSection(), std::string("planetwon"));
    CHECK_EQUAL(next(d, &n), A::kMovie);
    CHECK_EQUAL(n, 15);

    c.setRandom([](int) { return 0; });
    CHECK_EQUAL(next(c, &n), A::kScrollText);
    CHECK_EQUAL(n, 1);
    CHECK_EQUAL(next(c), A::kOptions);
    CHECK(!c.isRunning());
}

static void options_menu_panels() {
    typedef OptionsMenu M;
    ResourceManager resources;
    M menu(resources); // no pictures: the logic works without them
    std::vector<int> sounds;
    menu.setSoundCallback([&](int id) { sounds.push_back(id); });
    GameSettings settings;
    menu.open(settings, false, false, true);
    CHECK_EQUAL(sounds.back(), 1000);
    auto click = [&](int x, int y) {
        menu.mouseDown(x, y);
        menu.mouseUp(x, y);
    };

    // Nothing to return to, nothing to save.
    click(400, 400);
    click(150, 70);
    CHECK_EQUAL(menu.getPanel(), M::kPanelMain);
    CHECK_EQUAL(menu.takeResult(), M::kNone);

    // Settings: the difficulty steps round; Accept keeps it.
    click(150, 160);
    CHECK_EQUAL(menu.getPanel(), M::kPanelSettings);
    click(227 + 190, 29 + 195);
    click(227 + 110 + 212, 29 + 130); // past the slider's end: no change
    click(227 + 106 + 70, 29 + 130);  // a third of the way along
    click(400, 400);
    CHECK_EQUAL(menu.getPanel(), M::kPanelMain);
    CHECK(menu.takeSettingsChanged());
    CHECK_EQUAL(menu.getSettings().difficulty, 2);
    CHECK_EQUAL(menu.getSettings().effectsVolume, 9);

    // Load: an empty slot can't be picked.
    std::vector<M::Slot> slots(M::kAutoSaveSlot + 1);
    slots[1] = { true, "Before the door", "10/22 14:05" };
    menu.setSlots(slots);
    click(150, 40);
    click(300, 29 + 33 + 10);
    CHECK_EQUAL(sounds.back(), 1002);
    click(300, 29 + 33 + 30 + 10);
    click(400, 400);
    CHECK_EQUAL(menu.takeResult(), M::kLoad);
    CHECK_EQUAL(menu.getSlot(), 1);

    // Quit asks first; Cancel goes back.
    click(150, 130);
    CHECK_EQUAL(menu.getPanel(), M::kPanelQuit);
    menu.keyDown(KeyState(key::Escape));
    CHECK_EQUAL(sounds.back(), 2000);
    CHECK_EQUAL(menu.takeResult(), M::kNone);
    click(150, 100);
    menu.keyDown(KeyState(key::Return));
    CHECK_EQUAL(menu.takeResult(), M::kNewGame);

    // In a game: Return, and Save with a name typed in.
    menu.open(settings, true, true, false);
    click(150, 70);
    click(300, 29 + 33 + 10);
    menu.keyDown(KeyState('a', 'a'));
    menu.keyDown(KeyState('b', 'b'));
    menu.keyDown(KeyState(key::Backspace));
    click(400, 400);
    CHECK_EQUAL(menu.takeResult(), M::kSave);
    CHECK_EQUAL(menu.getSlot(), 0);
    CHECK_EQUAL(menu.getSaveName(), std::string("a"));
    click(400, 400);
    CHECK_EQUAL(menu.takeResult(), M::kReturn);
}

static const reluva::test::Suite suite("stgenerations.unit.campaign", {
    {"campaign runs the script", [](const std::filesystem::path&) { campaign_runs_the_script(); }},
    {"options menu panels", [](const std::filesystem::path&) { options_menu_panels(); }},
});
