// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: the main menu, the options, Load
// Game and Hall of Fame screens, and the game setup and empire selection
// screens.

#include "common/events.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/game_files.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/empire_screen.h"
#include "engines/botf/ui/fame_screen.h"
#include "engines/botf/ui/load_screen.h"
#include "engines/botf/ui/main_menu.h"
#include "engines/botf/ui/options_screen.h"
#include "engines/botf/ui/setup_screen.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

// A layout of a window and nothing in it.
Bytes emptyLayout(uint32_t id) {
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, id, 0, 0, 800, 600, "none");
    putWindowCounts(f, {});
    return f;
}

void testSetupScreen() {
    // A setup layout with the screen's buttons in a row, 10x10 each, and an
    // empty main menu.
    const std::vector<int32_t> ids = {9201, 9202, 9207, 9209, 9211, 9212, 9214, 9215, 9217,
                                      9219, 9221, 9223, 9225, 9227, 9229, 9231, 9233, 9234};
    Bytes setup;
    put32(setup, 2);
    putWidget(setup, 1, 9200, 0, 0, 800, 600, "none");
    putWindowCounts(setup, {{2, uint32_t(ids.size())}});
    for (size_t i = 0; i < ids.size(); ++i) {
        putWidget(setup, 2, uint32_t(ids[i]), uint32_t(i * 20), 100, 10, 10, "none");
        put32(setup, 0xffffffff);
        setup.push_back(0);
        put32(setup, 0);
        put32(setup, 0);
        putName(setup, "none");
        putName(setup, "none");
    }
    Bytes menu;
    put32(menu, 2);
    putWidget(menu, 1, 138, 0, 0, 800, 600, "none");
    putWindowCounts(menu, {});
    MemoryArchive archive;
    archive.files["gmesetup.wdf"] = setup;
    archive.files["opening.wdf"] = menu;
    Assets assets(archive);
    Lexicon lexicon;
    SetupScreen screen(assets, lexicon);
    CHECK(!screen.load());

    auto click = [&](int32_t id, uint16_t mods = 0) {
        const size_t i = size_t(std::find(ids.begin(), ids.end(), id) - ids.begin());
        SetupScreen::Result r = SetupScreen::Result::kOpen;
        for (EventType t : {EventType::kMouseDown, EventType::kMouseUp}) {
            Event e;
            e.type = t;
            e.button = MouseButton::kLeft;
            e.mods = mods;
            e.mouseX = int(i) * 20 + 5;
            e.mouseY = 105;
            r = screen.handle(e);
        }
        return r;
    };
    // The manual's defaults.
    const GameSetup &s = screen.setup();
    CHECK(s.civilization == (std::array<int, 5>{1, 1, 1, 1, 1}) && s.minorRaces == 3 && s.difficulty == 2);
    CHECK(s.randomEvents && s.manualCombat && s.victory == 0 && s.galaxyShape == 0 && s.galaxySize == 1);
    // Each click moves to the next choice, round to the first.
    click(9209);
    CHECK(s.difficulty == 3);
    click(9209);
    click(9209);
    CHECK(s.difficulty == 0);
    click(9207);
    CHECK(s.minorRaces == 0);
    click(9217);
    click(9219);
    click(9221);
    CHECK(!s.randomEvents && !s.manualCombat && s.victory == 1);
    click(9234);
    click(9233);
    CHECK(s.galaxyShape == 1 && s.galaxySize == 2);
    // Civilization levels: one empire, or with Shift all of them.
    click(9225);
    CHECK(s.civilization == (std::array<int, 5>{1, 2, 1, 1, 1}));
    click(9225, keymod::kShift);
    CHECK(s.civilization == (std::array<int, 5>{2, 2, 2, 2, 2}));
    click(9225, keymod::kShift);
    CHECK(s.civilization == (std::array<int, 5>{3, 3, 3, 3, 3}));
    // Only "none" is known for the timers.
    click(9212);
    click(9211);
    CHECK(s.strategicTimer == 0);
    // Letting go off the button does nothing; Accept and Cancel end it.
    Event down;
    down.type = EventType::kMouseDown;
    down.button = MouseButton::kLeft;
    down.mouseX = 2 * 20 + 5;
    down.mouseY = 105;
    screen.handle(down);
    Event up = down;
    up.type = EventType::kMouseUp;
    up.mouseY = 300;
    screen.handle(up);
    CHECK(s.minorRaces == 0);
    CHECK(click(9201) == SetupScreen::Result::kAccepted && click(9202) == SetupScreen::Result::kCancelled);
    Event ret;
    ret.type = EventType::kKeyDown;
    ret.key = key::kReturn;
    CHECK(screen.handle(ret) == SetupScreen::Result::kAccepted);

    // Drawing without the pictures and fonts doesn't fail.
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);

    // A layout without the screen's widgets is refused.
    archive.files["gmesetup.wdf"] = menu;
    SetupScreen other(assets, lexicon);
    CHECK(other.load());
}

void testMainMenu() {
    // The menu's nine buttons in a column, 40 pixels apart, as opening.wdf
    // has them (by id); nothing under it.
    const std::vector<uint32_t> ids = {139, 140, 141, 142, 355, 356, 144, 9600, 143};
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 138, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, uint32_t(ids.size())}});
    for (size_t i = 0; i < ids.size(); ++i) {
        putWidget(f, 2, ids[i], 600, uint32_t(100 + 40 * i), 160, 36, "none");
        put32(f, 0xffffffff);
        f.push_back(0);
        put32(f, 0);
        put32(f, 0);
        putName(f, "none");
        putName(f, "none");
    }
    MemoryArchive archive;
    archive.files["opening.wdf"] = f;
    Assets assets(archive);
    Lexicon lexicon;
    MainMenu menu(assets, lexicon);
    CHECK(!menu.load());
    auto click = [&](size_t i) {
        MainMenu::Choice c = MainMenu::Choice::kNone;
        for (EventType t : {EventType::kMouseDown, EventType::kMouseUp}) {
            Event e;
            e.type = t;
            e.button = MouseButton::kLeft;
            e.mouseX = 650;
            e.mouseY = 110 + 40 * int(i);
            c = menu.handle(e);
        }
        return c;
    };
    // Without a game, Continue, Save Game and Retire do nothing.
    CHECK(click(0) == MainMenu::Choice::kNone && click(4) == MainMenu::Choice::kNone &&
          click(7) == MainMenu::Choice::kNone);
    CHECK(click(1) == MainMenu::Choice::kNewSinglePlayer && click(2) == MainMenu::Choice::kNewMultiplayer);
    CHECK(click(3) == MainMenu::Choice::kLoadGame && click(5) == MainMenu::Choice::kOptions);
    CHECK(click(6) == MainMenu::Choice::kHallOfFame && click(8) == MainMenu::Choice::kQuit);
    // With one, they can be used.
    menu.setGameInProgress(true);
    CHECK(click(0) == MainMenu::Choice::kContinue && click(4) == MainMenu::Choice::kSaveGame &&
          click(7) == MainMenu::Choice::kRetire);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    menu.draw(surface);

    // The game setup layout isn't the main menu.
    archive.files["opening.wdf"] = Bytes(8, 0);
    MainMenu other(assets, lexicon);
    CHECK(bool(other.load()));
}

void testOptionsScreen() {
    // The options layout's buttons in a row, 10x10 each, its title's text
    // box, and four volume sliders of 41 cells, 7 pixels each, one under
    // another from x 100; an empty main menu.
    const std::vector<uint32_t> ids = {8100, 8101, 8106, 8107, 8108, 8109, 8114, 8115, 8118, 8119};
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 43, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, uint32_t(ids.size())}, {7, 4}, {14, 1}});
    for (size_t i = 0; i < ids.size(); ++i) {
        putWidget(f, 2, ids[i], uint32_t(i * 20), 500, 10, 10, "none");
        put32(f, 0xffffffff);
        f.push_back(2);
        put32(f, 0);
        put32(f, 0);
        putName(f, "none");
        putName(f, "none");
    }
    for (uint32_t i = 0; i < 4; ++i) {
        putWidget(f, 7, 8102 + i, 100, 200 + 30 * i, 287, 19, "none");
        put16(f, 0);
        for (const uint32_t v : {127U, 41U, 287U, 0U})
            put32(f, v);
        for (int n = 0; n < 4; ++n)
            putName(f, "none");
        for (int n = 0; n < 3; ++n)
            put32(f, 0);
    }
    putWidget(f, 14, 8122, 226, 153, 160, 29, "none");
    for (const uint32_t v : {0U, 0U, 160U, 29U})
        put32(f, v);
    putFont(f, 145, 200, 145, "human28.fnt");
    MemoryArchive archive;
    archive.files["options.wdf"] = f;
    archive.files["opening.wdf"] = emptyLayout(138);
    Assets assets(archive);
    Lexicon lexicon;
    OptionsScreen screen(assets, lexicon);
    CHECK(!screen.load());
    auto mouse = [&](EventType t, int x, int y) {
        Event e;
        e.type = t;
        e.button = t == EventType::kMouseMove ? MouseButton::kNone : MouseButton::kLeft;
        e.mouseX = x;
        e.mouseY = y;
        return screen.handle(e);
    };
    auto click = [&](size_t i) {
        mouse(EventType::kMouseDown, int(i * 20) + 5, 505);
        return mouse(EventType::kMouseUp, int(i * 20) + 5, 505);
    };
    // The manual's defaults.
    const GameOptions defaults;
    CHECK(defaults.manualCombat && defaults.randomEvents && defaults.autosave && defaults.strategicTimer == 0);
    // Each bar changes its setting; the timers' arrows have only "None".
    for (size_t i = 2; i < ids.size(); ++i)
        CHECK(click(i) == OptionsScreen::Result::kOpen);
    const GameOptions &o = screen.options();
    CHECK(!o.manualCombat && !o.toolTips && !o.randomEvents && !o.autosave);
    CHECK(o.strategicTimer == 0 && o.tacticalTimer == 0);
    // A click on a volume's bar lights the cell under it and those left of
    // it; a drag follows the mouse, past either end.
    mouse(EventType::kMouseDown, 100, 205);
    CHECK(o.volume[0] == 3); // one cell of 41, of 127
    mouse(EventType::kMouseMove, 100 + 20 * 7 + 3, 300);
    CHECK(o.volume[0] == 65);
    mouse(EventType::kMouseMove, 50, 300);
    CHECK(o.volume[0] == 0);
    mouse(EventType::kMouseMove, 500, 300);
    mouse(EventType::kMouseUp, 500, 300);
    CHECK(o.volume[0] == GameOptions::kMaxVolume);
    // Once the mouse is up, moving it changes nothing.
    mouse(EventType::kMouseMove, 100, 300);
    CHECK(o.volume[0] == GameOptions::kMaxVolume && o.volume[3] == GameOptions{}.volume[3]);
    mouse(EventType::kMouseDown, 100, 295);
    mouse(EventType::kMouseMove, 60, 295);
    mouse(EventType::kMouseUp, 60, 295);
    CHECK(o.volume[3] == 0);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);
    CHECK(click(0) == OptionsScreen::Result::kAccepted && click(1) == OptionsScreen::Result::kCancelled);
    // Opening it again shows the options it's given.
    screen.open(defaults);
    CHECK(screen.options().autosave && screen.options().volume[3] == defaults.volume[3]);
}

void testLoadScreen() {
    // Accept and Cancel, then the twelve slots, 600x20 each, one under
    // another from y 100, with their name and description boxes.
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 0, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, 14}, {14, 24}});
    auto button = [&](uint32_t id, uint32_t y, uint32_t w) {
        putWidget(f, 2, id, 10, y, w, 20, "none");
        put32(f, 0xffffffff);
        f.push_back(0);
        put32(f, 0);
        put32(f, 0);
        putName(f, "none");
        putName(f, "none");
    };
    button(9503, 550, 100);
    button(9504, 570, 100);
    for (uint32_t i = 0; i < 12; ++i)
        button(9507 + 2 * i, 100 + 25 * i, 600);
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t id = i < 12 ? 9506 + 2 * i : 9530 + 2 * (i - 12);
        putWidget(f, 14, id, i < 12 ? 50 : 250, 100 + 25 * (i % 12), 150, 17, "none");
        for (const uint32_t v : {0U, 0U, 150U, 17U})
            put32(f, v);
        putFont(f, 1, 0, 0, "human15.fnt");
    }
    MemoryArchive archive;
    archive.files["loadsave.wdf"] = f;
    archive.files["opening.wdf"] = emptyLayout(138);
    archive.files["human15.fnt"] = makeFont(16);
    Assets assets(archive);
    Lexicon lexicon;
    LoadScreen screen(assets, lexicon);
    CHECK(!screen.load());
    LoadScreen::Slots slots;
    slots[0] = SaveSummary{"Tutorial #1", "Irregular", 0, false};
    slots[3] = SaveSummary{"Mine", "Spiral", 120, false};
    screen.open(slots);
    auto click = [&](int x, int y) {
        LoadScreen::Result r = LoadScreen::Result::kOpen;
        for (EventType t : {EventType::kMouseDown, EventType::kMouseUp}) {
            Event e;
            e.type = t;
            e.button = MouseButton::kLeft;
            e.mouseX = x;
            e.mouseY = y;
            r = screen.handle(e);
        }
        return r;
    };
    auto slot = [&](int i) { return click(300, 105 + 25 * i); };
    // Nothing chosen: Accept does nothing; an empty slot can't be chosen.
    CHECK(screen.chosen() == -1 && click(20, 555) == LoadScreen::Result::kOpen);
    CHECK(slot(1) == LoadScreen::Result::kOpen && screen.chosen() == -1);
    CHECK(slot(3) == LoadScreen::Result::kOpen && screen.chosen() == 3);
    CHECK(slot(0) == LoadScreen::Result::kOpen && screen.chosen() == 0);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);
    CHECK(click(20, 555) == LoadScreen::Result::kAccepted && screen.chosen() == 0);
    CHECK(click(20, 575) == LoadScreen::Result::kCancelled);
    // Opening it again chooses nothing.
    screen.open(slots);
    CHECK(screen.chosen() == -1);
}

void testFromIni() {
    // Stbof.ini as the user's install had it after a game: every setting
    // the two screens start from.
    const auto ini = parseStbofIni("TACTICAL=ABSTRACT\r\nAUTOSAVE=ON\r\nTOOLTIPS=OFF\r\nRANDOMEVENTS=OFF\r\n"
                                   "SHAPE=1\r\nSIZE=0\r\nRACENUM=0\r\nDIFFICULTY=1\r\nCIVLEVEL1=0\r\nCIVLEVEL5=4\r\n"
                                   "MUSICVOLUME=90\r\nFXVOLUME=200\r\nVIDEOVOLUME=127\r\nVOICEVOLUME=0\r\n");
    const GameOptions o = optionsFromIni(ini);
    CHECK(!o.manualCombat && o.autosave && !o.toolTips && !o.randomEvents);
    CHECK(o.volume[0] == 0 && o.volume[1] == 127 && o.volume[2] == 90 && o.volume[3] == GameOptions::kMaxVolume);
    const GameSetup s = setupFromIni(ini);
    CHECK(s.galaxyShape == 1 && s.galaxySize == 0 && s.minorRaces == 0 && s.difficulty == 0);
    CHECK(s.civilization[0] == 0 && s.civilization[1] == 1 && s.civilization[4] == 4);
    // Without the file, or with values out of range, the manual's defaults.
    const GameOptions none = optionsFromIni({});
    CHECK(none.manualCombat && none.toolTips && none.volume[2] == 100 && none.volume[1] == 127);
    const GameSetup odd = setupFromIni(parseStbofIni("SHAPE=9\nDIFFICULTY=0\n"));
    CHECK(odd.galaxyShape == 0 && odd.difficulty == 2);
}

void testFameScreen() {
    // Return, the ten rows' places 40 pixels apart, and a row's three text
    // boxes; a row frame for the Klingons.
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 0, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, 1}, {13, 10}});
    putWidget(f, 2, 9950, 600, 550, 100, 30, "none");
    put32(f, 0xffffffff);
    f.push_back(0);
    put32(f, 0);
    put32(f, 0);
    putName(f, "none");
    putName(f, "none");
    for (uint32_t i = 0; i < 10; ++i) {
        putWidget(f, 13, 9951 + i, 17, 100 + 40 * i, 768, 36, "none");
        put32(f, 0xffffffff);
    }
    Bytes item;
    put32(item, 2);
    putWidget(item, 1, 0, 0, 0, 768, 36, "none");
    putWindowCounts(item, {{14, 3}});
    for (const auto &[id, x] : {std::pair{9962U, 52U}, {9963U, 296U}, {9964U, 598U}}) {
        putWidget(item, 14, id, x, 10, 140, 17, "none");
        for (const uint32_t v : {0U, 0U, 140U, 17U})
            put32(item, v);
        putFont(item, 0, 0, 0, "human15.fnt");
    }
    MemoryArchive archive;
    archive.files["fame.wdf"] = f;
    archive.files["fameitem.wdf"] = item;
    archive.files["human15.fnt"] = makeFont(16);
    image::Image frame;
    frame.w = 768;
    frame.h = 36;
    frame.rgba.assign(size_t(768) * 36 * 4, 200);
    archive.files["hk_fm1.tga"] = image::encodeTga(frame);
    Assets assets(archive);
    Lexicon lexicon;
    FameScreen screen(assets, lexicon);
    CHECK(!screen.load());
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    // Empty, nothing is drawn in the rows; with a Klingon's game, its row
    // has the Klingons' frame.
    screen.draw(surface);
    CHECK(surface.pixels[size_t(105 * 800 + 400) * 4] == 0);
    screen.open({{"Yobo", 3, "Klingons 381", 12865}});
    screen.draw(surface);
    CHECK(surface.pixels[size_t(105 * 800 + 400) * 4] == 200);
    CHECK(surface.pixels[size_t(145 * 800 + 400) * 4] == 0);
    auto click = [&](int x, int y) {
        bool left = false;
        for (EventType t : {EventType::kMouseDown, EventType::kMouseUp}) {
            Event e;
            e.type = t;
            e.button = MouseButton::kLeft;
            e.mouseX = x;
            e.mouseY = y;
            left = screen.handle(e);
        }
        return left;
    };
    CHECK(!click(300, 300) && click(650, 560));
    Event esc;
    esc.type = EventType::kKeyDown;
    esc.key = key::kEscape;
    CHECK(screen.handle(esc));
}

void testEmpireScreen() {
    // The empires' name buttons 9105-9109 in a column, 100x20 each, Accept
    // and Cancel, a hidden button, and the description's text box.
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 9000, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, 8}, {14, 1}});
    auto button = [&](uint32_t id, uint32_t x, uint32_t y) {
        putWidget(f, 2, id, x, y, 100, 20, "none");
        put32(f, 0xffffffff);
        f.push_back(2);
        put32(f, 0);
        put32(f, 0);
        putName(f, "none");
        putName(f, "none");
    };
    for (uint32_t i = 0; i < 5; ++i)
        button(9105 + i, 0, 100 + i * 50);
    button(9110, 400, 500);
    button(9111, 0, 500);
    button(9124, 200, 100);
    putWidget(f, 14, 9112, 300, 100, 240, 340, "none");
    for (uint32_t v : {0u, 0u, 240u, 340u})
        put32(f, v);
    putFont(f, 0, 0, 0, "human20.fnt");
    Bytes menu;
    put32(menu, 2);
    putWidget(menu, 1, 138, 0, 0, 800, 600, "none");
    putWindowCounts(menu, {});
    MemoryArchive archive;
    archive.files["emppick.wdf"] = f;
    archive.files["opening.wdf"] = menu;
    Assets assets(archive);
    Lexicon lexicon;
    EmpireScreen screen(assets, lexicon);
    CHECK(!screen.load());

    auto mouse = [&](EventType t, int x, int y) {
        Event e;
        e.type = t;
        e.button = t == EventType::kMouseMove ? MouseButton::kNone : MouseButton::kLeft;
        e.mouseX = x;
        e.mouseY = y;
        return screen.handle(e);
    };
    auto click = [&](int x, int y) {
        mouse(EventType::kMouseDown, x, y);
        return mouse(EventType::kMouseUp, x, y);
    };
    Event ret;
    ret.type = EventType::kKeyDown;
    ret.key = key::kReturn;
    // Nothing chosen: Accept and Return do nothing.
    CHECK(screen.chosen() == -1);
    CHECK(click(450, 510) == EmpireScreen::Result::kOpen && screen.handle(ret) == EmpireScreen::Result::kOpen);
    // Clicking a name chooses that empire (race.rst's order); pointing doesn't.
    mouse(EventType::kMouseMove, 50, 210);
    CHECK(screen.chosen() == -1);
    click(50, 160);
    CHECK(screen.chosen() == 1);
    // The hidden button isn't there.
    click(250, 110);
    CHECK(screen.chosen() == 1);
    CHECK(click(450, 510) == EmpireScreen::Result::kAccepted);
    CHECK(click(50, 510) == EmpireScreen::Result::kCancelled);
    screen.reset();
    CHECK(screen.chosen() == -1);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);
}

} // namespace

static const reluva::test::Suite
    kSuite("botf.unit.screens", {
                                    {"main menu", [](const std::filesystem::path &) { testMainMenu(); }},
                                    {"options", [](const std::filesystem::path &) { testOptionsScreen(); }},
                                    {"load game", [](const std::filesystem::path &) { testLoadScreen(); }},
                                    {"from Stbof.ini", [](const std::filesystem::path &) { testFromIni(); }},
                                    {"hall of fame", [](const std::filesystem::path &) { testFameScreen(); }},
                                    {"game setup", [](const std::filesystem::path &) { testSetupScreen(); }},
                                    {"empire selection", [](const std::filesystem::path &) { testEmpireScreen(); }},
                                });
