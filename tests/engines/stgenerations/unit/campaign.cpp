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
const char *const kScript = "[Start]\r\n"
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

Campaign::Action::Type next(Campaign &c, int *number = nullptr) {
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
    slots[1] = {true, "Before the door", "10/22 14:05"};
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

static void key_maps() {
    KeyMap away = KeyMap::awayDefaults();
    CHECK_EQUAL(away.get(0x26), (int)KeyMap::kAwayForward);
    CHECK_EQUAL(away.get(0x02), (int)KeyMap::kAwayMouseLook);
    CHECK_EQUAL(away.get('W'), (int)KeyMap::kNone);
    // Two keys: the panel shows the higher one first.
    int first;
    int second;
    away.findKeys(KeyMap::kAwayAttack, first, second);
    CHECK_EQUAL(first, 0x2D);
    CHECK_EQUAL(second, 0x01);
    away.findKeys(KeyMap::kAwayRun, first, second);
    CHECK_EQUAL(first, 0x10);
    CHECK_EQUAL(second, -1);
    KeyMap ship = KeyMap::starshipDefaults();
    CHECK_EQUAL(ship.get(0x65), KeyMap::kShipHelm + 3);
    CHECK_EQUAL(ship.get('7'), KeyMap::kShipHelm + 6);

    // Through the configuration and back.
    KeyMap read;
    CHECK(read.fromString(away.toString()));
    CHECK(read == away);
    CHECK(!read.fromString("26:1,zz"));
    CHECK(read == away);
    CHECK(read.fromString(""));
    CHECK_EQUAL(read.get(0x26), (int)KeyMap::kNone);

    CHECK_EQUAL(KeyMap::virtualKey('w'), 'W');
    CHECK_EQUAL(KeyMap::virtualKey(reluva::key::KpPlus), 0x6B);
    CHECK_EQUAL(KeyMap::virtualKey(reluva::key::RCtrl), 0x11);
    CHECK_EQUAL(KeyMap::keyName('W'), std::string("W"));
    CHECK_EQUAL(KeyMap::keyName(0x64), std::string("num 4"));
    CHECK_EQUAL(KeyMap::keyName(0x7A), std::string("F11"));
    CHECK_EQUAL(KeyMap::keyName(0x02), std::string("mb 2"));
    CHECK_EQUAL(KeyMap::keyName(0x1B), std::string("?!?"));
}

static void options_menu_controls() {
    typedef OptionsMenu M;
    ResourceManager resources;
    M menu(resources);
    GameSettings settings;
    menu.open(settings, false, false, true);
    auto click = [&](int x, int y) {
        menu.mouseDown(x, y);
        menu.mouseUp(x, y);
    };
    // The middle of a key cell: two to an action, down two columns.
    auto cell = [&](int action, int which) {
        int x = 227 + (action < 10 ? 84 : 252) + 34 * which + 16;
        int y = 29 + 37 + 30 * (action % 10) + 13;
        click(x, y);
    };
    auto press = [&](uint32_t key) { menu.keyDown(KeyState(key)); };
    click(150, 195);
    CHECK_EQUAL(menu.getPanel(), M::kPanelControls);

    // W for Forward in place of Up.
    cell(1, 0);
    press('w');
    // Then W for Jump: it leaves Forward.
    cell(7, 1);
    press((reluva::key::F1 + 4)); // not the function keys
    press('w');
    // Mouse Look takes only the left and right buttons; Escape gives up.
    cell(13, 0);
    press('k');
    menu.mouseDown(10, 10, 4);
    press(reluva::key::Escape);
    CHECK_EQUAL(menu.getPanel(), M::kPanelControls);
    click(400, 400);
    CHECK(menu.takeSettingsChanged());
    const KeyMap &keys = menu.getSettings().awayKeys;
    CHECK_EQUAL(keys.get('W'), (int)KeyMap::kAwayJump);
    CHECK_EQUAL(keys.get(0x26), (int)KeyMap::kNone);
    CHECK_EQUAL(keys.get(0x0D), (int)KeyMap::kAwayJump);
    CHECK_EQUAL(keys.get('K'), (int)KeyMap::kNone);
    CHECK_EQUAL(keys.get(0x02), (int)KeyMap::kAwayMouseLook);

    // The starship page; Cancel keeps the old keys.
    click(150, 195);
    click(227 + 200, 29 + 10);
    cell(0, 0);
    press('f');
    menu.keyDown(KeyState(reluva::key::Escape));
    CHECK_EQUAL(menu.getPanel(), M::kPanelMain);
    CHECK(!menu.takeSettingsChanged());
    CHECK_EQUAL(menu.getSettings().starshipKeys.get('F'), (int)KeyMap::kNone);
    click(150, 195);
    click(227 + 200, 29 + 10);
    cell(0, 1);
    menu.mouseDown(10, 10, 2);
    click(400, 400);
    CHECK_EQUAL(menu.getSettings().starshipKeys.get(0x02), (int)KeyMap::kShipPhasers);
    CHECK_EQUAL(menu.getSettings().starshipKeys.get(0x10), (int)KeyMap::kShipPhasers); // the other cell keeps Shift
}

namespace {

// A bridge that is never shown: the script runs straight on.
struct HiddenBridge : CampaignWorld {
    void bridgeCommand(const std::string &, const std::vector<std::string> &) override {}
    bool isBridgeShown() const override { return false; }
    bool isEnterpriseAt(const std::string &) const override { return false; }
    bool getObjectFlag(const std::string &, int) const override { return false; }
    double getStardate() const override { return 0; }
    bool isAudioQueueEmpty() const override { return true; }
    bool isWarping() const override { return false; }
    bool isZooming() const override { return false; }
    bool isEnterpriseIdle() const override { return true; }
};

} // namespace

static void campaign_saved_in_a_combat_starts_it_again() {
    typedef Campaign::Action A;
    HiddenBridge bridge;
    Campaign c;
    c.load("[Start]\nMovie 1\nSpaceCombat 5 Won Lost\n[Won]\nMovie 2\n[Lost]\nMovie 3\n");
    c.setWorld(&bridge);
    c.start("Start");
    int n = 0;
    CHECK_EQUAL(next(c), A::kMovie);
    CHECK_EQUAL(next(c, &n), A::kSpaceCombat);
    CHECK_EQUAL(n, 5);
    CHECK_EQUAL(c.getCombat(), 5);

    // Saved during the fight: loading starts the fight again, not the section.
    MemoryWriteStream out;
    Serializer save(nullptr, &out);
    c.sync(save);
    Campaign d;
    d.load("[Start]\nMovie 1\nSpaceCombat 5 Won Lost\n[Won]\nMovie 2\n[Lost]\nMovie 3\n");
    d.setWorld(&bridge);
    MemoryReadStream in(out.data());
    Serializer load(&in, nullptr);
    d.sync(load);
    CHECK(!load.err());
    d.restartSection();
    CHECK_EQUAL(next(d, &n), A::kSpaceCombat);
    CHECK_EQUAL(n, 5);
    CHECK_EQUAL(next(d), A::kNone);
    d.combatEnded(Campaign::kCombatWon);
    CHECK_EQUAL(next(d, &n), A::kMovie);
    CHECK_EQUAL(n, 2);
}

static const reluva::test::Suite
    suite("stgenerations.unit.campaign",
          {
              {"campaign runs the script", [](const std::filesystem::path &) { campaign_runs_the_script(); }},
              {"options menu panels", [](const std::filesystem::path &) { options_menu_panels(); }},
              {"key maps", [](const std::filesystem::path &) { key_maps(); }},
              {"options menu controls", [](const std::filesystem::path &) { options_menu_controls(); }},
              {"campaign saved in a combat starts it again",
               [](const std::filesystem::path &) { campaign_saved_in_a_combat_starts_it_again(); }},
          });
