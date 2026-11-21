// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: the main menu, the options, Load
// Game, Save Game, Hall of Fame and Multiplayer Setup screens, the menu's
// music, and the game setup and empire selection
// screens.

#include "common/events.h"
#include "engines/botf/formats/font.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/game_files.h"
#include "engines/botf/menu_audio.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/empire_screen.h"
#include "engines/botf/ui/fame_screen.h"
#include "engines/botf/ui/galaxy_screen.h"
#include "engines/botf/ui/load_save_screen.h"
#include "engines/botf/ui/main_menu.h"
#include "engines/botf/ui/multiplayer_screen.h"
#include "engines/botf/ui/options_screen.h"
#include "engines/botf/ui/setup_screen.h"
#include "engines/botf/ui/timers.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <random>
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
    // The timers' arrows step through their choices, stopping at the ends.
    click(9212);
    click(9212);
    click(9211);
    CHECK(s.strategicTimer == 1);
    click(9214);
    CHECK(s.tacticalTimer == 0);
    for (int i = 0; i < 40; ++i)
        click(9215);
    CHECK(s.tacticalTimer == int(tacticalTimerChoices().size()) - 1);
    // Held, an arrow steps at once, then every quarter of a second until
    // let go.
    auto mouseAt = [&](EventType t, int32_t id) {
        const size_t i = size_t(std::find(ids.begin(), ids.end(), id) - ids.begin());
        Event e;
        e.type = t;
        e.button = MouseButton::kLeft;
        e.mouseX = int(i) * 20 + 5;
        e.mouseY = 105;
        screen.handle(e);
    };
    CHECK(!screen.tick(1000));
    mouseAt(EventType::kMouseDown, 9214);
    CHECK(s.tacticalTimer == int(tacticalTimerChoices().size()) - 2 && screen.repeating());
    CHECK(!screen.tick(1249) && screen.tick(1250) && s.tacticalTimer == int(tacticalTimerChoices().size()) - 3);
    CHECK(screen.tick(2000) && s.tacticalTimer == int(tacticalTimerChoices().size()) - 6);
    mouseAt(EventType::kMouseUp, 9214);
    CHECK(!screen.repeating() && !screen.tick(3000) && s.tacticalTimer == int(tacticalTimerChoices().size()) - 6);
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
    // With an autosave, Continue can be used, to go on with it.
    menu.setGameInProgress(false, true);
    CHECK(click(0) == MainMenu::Choice::kContinue && click(4) == MainMenu::Choice::kNone);
    // With a game, all of them.
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
    // box, four volume sliders of 41 cells, 7 pixels each, one under
    // another from x 100, and the settings' lists' places at x 600, 100
    // apart (Tool Tips', Tactical Combat's, Random Events', Autosave's); an
    // empty main menu.
    const std::vector<uint32_t> ids = {8100, 8101, 8106, 8107, 8108, 8109, 8114, 8115, 8118, 8119};
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 43, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, uint32_t(ids.size())}, {7, 4}, {13, 4}, {14, 1}});
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
    for (const auto &[id, y] : {std::pair{8111U, 0U}, {8110U, 100U}, {8112U, 200U}, {8113U, 300U}}) {
        putWidget(f, 13, id, 600, y, 144, 96, "none");
        put32(f, 0xffffffff);
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
    // A setting opens its list, whose choices (yes, no) are 32 apart from
    // 20, 36 in it: choosing one sets it and closes the list.
    const GameOptions &o = screen.options();
    auto choose = [&](int list, int choice) {
        const int x = 600 + 25;
        const int y = 100 * list + 36 + 32 * choice + 5;
        mouse(EventType::kMouseDown, x, y);
        mouse(EventType::kMouseUp, x, y);
    };
    CHECK(click(3) == OptionsScreen::Result::kOpen && o.toolTips); // Tool Tips: only opened
    choose(0, 1);
    CHECK(!o.toolTips);
    choose(0, 0); // closed: a click there does nothing
    CHECK(!o.toolTips);
    // A click elsewhere only closes it, unchanged; its bar again too, and
    // another setting's bar doesn't open that one's.
    click(2);
    mouse(EventType::kMouseDown, 750, 590);
    mouse(EventType::kMouseUp, 750, 590);
    choose(1, 0);
    CHECK(o.manualCombat);
    click(2);
    click(2);
    choose(1, 0);
    CHECK(o.manualCombat);
    click(2);
    click(4);
    choose(2, 1);
    CHECK(o.manualCombat && o.randomEvents);
    // Tactical combat's list has Automatic first.
    click(2);
    choose(1, 0);
    CHECK(!o.manualCombat);
    click(4);
    choose(2, 1);
    click(5);
    choose(3, 1);
    // The timers' arrows (more, less) step through their choices, stopping
    // at the ends.
    for (size_t i : {6, 6, 6, 7, 8, 9, 9})
        CHECK(click(i) == OptionsScreen::Result::kOpen);
    CHECK(!o.manualCombat && !o.toolTips && !o.randomEvents && !o.autosave);
    CHECK(o.strategicTimer == 2 && o.tacticalTimer == 0);
    CHECK(optionsIniValues(o).at("TIMELIMIT") == "2" && optionsIniValues(o).at("TACTIMELIMIT") == "0");
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

void testLoadSaveScreen() {
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
    LoadSaveScreen screen(assets, lexicon);
    CHECK(!screen.load());
    LoadSaveScreen::Slots slots;
    slots[0] = SaveSummary{"Tutorial #1", "Irregular", 0, 4, false};
    slots[3] = SaveSummary{"Mine", "Spiral", 120, 3, false};
    screen.open(slots, LoadSaveScreen::Mode::kLoad);
    auto click = [&](int x, int y) {
        LoadSaveScreen::Result r = LoadSaveScreen::Result::kOpen;
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
    CHECK(screen.chosen() == -1 && click(20, 555) == LoadSaveScreen::Result::kOpen);
    CHECK(slot(1) == LoadSaveScreen::Result::kOpen && screen.chosen() == -1);
    CHECK(slot(3) == LoadSaveScreen::Result::kOpen && screen.chosen() == 3);
    CHECK(slot(0) == LoadSaveScreen::Result::kOpen && screen.chosen() == 0);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);
    CHECK(click(20, 555) == LoadSaveScreen::Result::kAccepted && screen.chosen() == 0);
    CHECK(click(20, 575) == LoadSaveScreen::Result::kCancelled);
    // Opening it again chooses nothing.
    screen.open(slots, LoadSaveScreen::Mode::kLoad);
    CHECK(screen.chosen() == -1);

    // Saving: an empty slot can be chosen, and its description typed over,
    // as far as the field and the font allow.
    screen.open(slots, LoadSaveScreen::Mode::kSave, "Ring");
    auto key = [&](uint32_t k) {
        Event e;
        e.type = EventType::kKeyDown;
        e.key = k;
        return screen.handle(e);
    };
    auto typed = [&](const char *t) {
        Event e;
        e.type = EventType::kTextInput;
        std::strncpy(e.text, t, sizeof e.text - 1);
        return screen.handle(e);
    };
    typed("x"); // nothing chosen: not typed
    CHECK(screen.description().empty() && key(key::kReturn) == LoadSaveScreen::Result::kOpen);
    // A saved game's slot keeps its description; an empty one takes the
    // new game's.
    CHECK(slot(3) == LoadSaveScreen::Result::kOpen && screen.description() == "Spiral");
    CHECK(slot(1) == LoadSaveScreen::Result::kOpen && screen.chosen() == 1 && screen.description() == "Ring");
    key(key::kBackspace);
    typed("s \xc3\xa9\xe2\x82\xac"); // 's', a space, 'é'; the font has no euro sign
    CHECK(screen.description() == "Rins \xc3\xa9");
    key(key::kBackspace);
    CHECK(screen.description() == "Rins ");
    screen.draw(surface);
    // The field is 150 pixels, here 3 to 6 a character: it fills up.
    for (int i = 0; i < 60; ++i)
        typed("1");
    Font font15;
    CHECK(!font15.load(makeFont(16)));
    CHECK(font15.measure(screen.description()) <= 150 && screen.description().size() < 60);
    // The cursor: a white block after the description, from its glyphs' top
    // (box at 250, 125, 17 high; the font's lines 16), shown or not.
    const int cursorX = 250 + font15.measure(screen.description());
    auto white = [&](int x, int y) {
        const uint8_t *p = surface.row(y) + size_t(x) * 4;
        return p[0] == 255 && p[1] == 255 && p[2] == 255;
    };
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t(0));
    screen.draw(surface);
    CHECK(white(cursorX, 126) && white(cursorX + 7, 126 + 14) && !white(cursorX + 8, 126) && !white(cursorX, 125));
    screen.setCursorShown(false);
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t(0));
    screen.draw(surface);
    CHECK(!white(cursorX, 126));
    // A saved game's slot is its empire's colour, the Klingons' red (255,
    // 24, 33) in slot 3, the Romulans' teal in slot 0; the chosen one the
    // lighter colour, here the Romulans' pink (239, 182, 198), the game to
    // save's empire not being given; the font's shading aside.
    auto any = [&](int top, auto test) {
        for (int y = top; y < top + 25; ++y)
            for (int x = 0; x < 800; ++x)
                if (const uint8_t *p = surface.row(y) + size_t(x) * 4; test(p[0], p[1], p[2]))
                    return true;
        return false;
    };
    auto red = [](int r, int g, int) { return r > 0 && g * 4 < r; };
    auto pink = [](int r, int g, int b) { return r > 0 && g * 2 > r && b > g; };
    auto lavender = [](int r, int, int b) { return r > 0 && b > r; };
    CHECK(any(100 + 25 * 3, red) && !any(100 + 25 * 3, pink));
    CHECK(any(100 + 25, pink) && !any(100 + 25, red) && !any(100, red));
    // The Klingons' chosen colour is lavender (214, 178, 255).
    slot(3);
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t(0));
    screen.draw(surface);
    CHECK(any(100 + 25 * 3, lavender) && !any(100 + 25 * 3, red));
    slot(1);
    CHECK(click(20, 555) == LoadSaveScreen::Result::kAccepted && screen.chosen() == 1);
}

void testSaveDescription() {
    // As the game fills an empty slot: "Ferengi 0 Medium Ring Galaxy".
    const Lexicon none;
    CHECK(saveDescription(none, 2, 0, 1, 2) == "Ferengi 0 Medium Ring Galaxy");
    CHECK(saveDescription(none, 0, 12, 0, 3) == "Cardassians 12 Small Spiral Galaxy");
    CHECK(saveDescription(none, 1, 5, 2, 0) == "Federation 5 Large Irregular Galaxy");
}

void testTimers() {
    // The user's account of the game's choices.
    const std::vector<int> &strategic = strategicTimerChoices();
    const std::vector<int> &tactical = tacticalTimerChoices();
    CHECK(strategic.size() == 25 && tactical.size() == 30);
    CHECK(strategic[0] == 0 && strategic[1] == 60 && strategic[15] == 15 * 60 && strategic[16] == 20 * 60);
    CHECK(strategic[17] == 25 * 60 && strategic[18] == 30 * 60 && strategic.back() == 90 * 60);
    CHECK(tactical[1] == 15 && tactical[20] == 5 * 60 && tactical[21] == 6 * 60 && tactical[25] == 10 * 60);
    CHECK(tactical[26] == 15 * 60 && tactical.back() == 30 * 60);
    CHECK(timerText(15) == "00:15" && timerText(90 * 60) == "90:00" && timerText(65) == "01:05");
    // Stbof.ini: the strategic timer in minutes, the tactical in seconds, as
    // the user's file with 03:00 and 01:30 has them.
    const GameOptions o = optionsFromIni(parseStbofIni("TIMELIMIT=3\r\nTACTIMELIMIT=90\r\n"));
    CHECK(strategic[size_t(o.strategicTimer)] == 3 * 60 && tactical[size_t(o.tacticalTimer)] == 90);
    CHECK(optionsIniValues(o).at("TIMELIMIT") == "3" && optionsIniValues(o).at("TACTIMELIMIT") == "90");
    // A time that isn't a choice is none.
    CHECK(optionsFromIni(parseStbofIni("TIMELIMIT=7x\r\nTACTIMELIMIT=17\r\n")).tacticalTimer == 0);
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
    // Written back as the game writes them: manual combat is DETAILED.
    CHECK(optionsIniValues(o).at("TACTICAL") == "ABSTRACT" && optionsIniValues(o).at("TOOLTIPS") == "OFF");
    GameOptions manual = o;
    manual.manualCombat = true;
    CHECK(optionsIniValues(manual).at("TACTICAL") == "DETAILED");
    CHECK(optionsFromIni(parseStbofIni("TACTICAL=DETAILED\r\n")).manualCombat);
    CHECK(setupIniValues(s).at("DIFFICULTY") == "1" && setupIniValues(s).at("SHAPE") == "1");
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

// A layout of buttons, empty pictures and text boxes (each `{type, id, x,
// y, w, h}`), in a window `w` by `h`.
Bytes smallLayout(uint32_t w, uint32_t h, const std::vector<std::array<uint32_t, 6>> &widgets) {
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 0, 0, 0, w, h, "none");
    std::vector<std::pair<uint32_t, uint32_t>> counts;
    for (const uint32_t type : {2U, 13U, 14U}) {
        uint32_t n = 0;
        for (const auto &wd : widgets)
            n += wd[0] == type ? 1 : 0;
        if (n)
            counts.emplace_back(type, n);
    }
    putWindowCounts(f, counts);
    for (const uint32_t type : {2U, 13U, 14U})
        for (const auto &[t, id, x, y, ww, hh] : widgets) {
            if (t != type)
                continue;
            putWidget(f, t, id, x, y, ww, hh, "none");
            if (t == 14) {
                for (const uint32_t v : {0U, 0U, ww, hh})
                    put32(f, v);
                putFont(f, 0, 0, 0, "human15.fnt");
                continue;
            }
            put32(f, 0xffffffff);
            if (t == 2) {
                f.push_back(0);
                put32(f, 0);
                put32(f, 0);
                putName(f, "none");
                putName(f, "none");
            }
        }
    return f;
}

void testMultiplayerScreen() {
    // Host, Join and Cancel down the left, the three fields under them, and
    // the panels' places; each panel's buttons in a row.
    MemoryArchive archive;
    archive.files["opening.wdf"] = emptyLayout(138);
    archive.files["human15.fnt"] = makeFont(16);
    archive.files["mpsetup.wdf"] = smallLayout(800, 600,
                                               {{2, 108, 16, 195, 160, 38},
                                                {2, 109, 16, 242, 160, 38},
                                                {2, 114, 16, 289, 160, 38},
                                                {13, 702, 16, 337, 294, 36},
                                                {13, 703, 16, 384, 294, 36},
                                                {13, 701, 16, 430, 294, 36},
                                                {13, 399, 186, 195, 160, 131},
                                                {13, 200, 317, 192, 303, 408},
                                                {14, 112, 21, 353, 284, 17},
                                                {14, 113, 21, 400, 284, 17},
                                                {14, 700, 21, 446, 284, 17}});
    archive.files["mpconn.wdf"] =
        smallLayout(160, 134, {{2, 193, 0, 0, 160, 38}, {2, 194, 0, 47, 160, 38}, {2, 196, 0, 94, 160, 38}});
    archive.files["mpcnet.wdf"] = smallLayout(303, 408,
                                              {{2, 202, 35, 69, 129, 19},
                                               {2, 203, 168, 69, 127, 19},
                                               {2, 207, 35, 94, 261, 36},
                                               {2, 210, 135, 288, 158, 36},
                                               {14, 714, 40, 113, 252, 17}});
    archive.files["mpcmod.wdf"] =
        smallLayout(303, 408, {{2, 491, 135, 288, 158, 36}, {2, 486, 34, 98, 262, 25}, {14, 492, 39, 102, 253, 17}});
    archive.files["mpcser.wdf"] = smallLayout(303, 408, {{2, 451, 135, 288, 158, 36}, {2, 442, 36, 147, 80, 19}});
    Assets assets(archive);
    Lexicon lexicon;
    MultiplayerScreen screen(assets, lexicon);
    CHECK(!screen.load());
    screen.open();
    auto mouse = [&](int x, int y) {
        MultiplayerScreen::Result r = MultiplayerScreen::Result::kOpen;
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
    auto typed = [&](const char *t) {
        Event e;
        e.type = EventType::kTextInput;
        std::strncpy(e.text, t, sizeof e.text - 1);
        screen.handle(e);
    };
    auto key = [&](uint32_t k) {
        Event e;
        e.type = EventType::kKeyDown;
        e.key = k;
        return screen.handle(e);
    };
    // At first the panels are hidden: Network's place doesn't answer.
    mouse(200, 200);
    CHECK(screen.connection() == MultiplayerScreen::Connection::kNone);
    // Host shows the connections; Network its panel.
    mouse(50, 210);
    CHECK(screen.role() == MultiplayerScreen::Role::kHost);
    mouse(200, 200);
    CHECK(screen.connection() == MultiplayerScreen::Connection::kNetwork);
    mouse(200, 250);
    CHECK(screen.connection() == MultiplayerScreen::Connection::kModem);
    // The modem's address book: its first row typed in once clicked.
    mouse(400, 300);
    typed("555-1234");
    CHECK(screen.field() == MultiplayerScreen::Field::kAddress && screen.addressRow() == 0 &&
          screen.text(MultiplayerScreen::Field::kAddress, 0) == "555-1234");
    // Typing goes nowhere until a field is clicked; Tab goes round them.
    typed("x");
    CHECK(screen.text(MultiplayerScreen::Field::kGameName).empty());
    mouse(100, 350);
    typed("Game");
    CHECK(screen.field() == MultiplayerScreen::Field::kGameName &&
          screen.text(MultiplayerScreen::Field::kGameName) == "Game");
    key(key::kTab);
    key(key::kTab);
    CHECK(screen.field() == MultiplayerScreen::Field::kPassword);
    typed("12345678901234567");
    CHECK(screen.text(MultiplayerScreen::Field::kPassword).size() == 13); // the manual's limit
    key(key::kBackspace);
    CHECK(screen.text(MultiplayerScreen::Field::kPassword).size() == 12);
    key(key::kTab);
    CHECK(screen.field() == MultiplayerScreen::Field::kGameName);
    Surface surface;
    surface.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(surface);
    // Opening it again keeps the fields but nothing chosen.
    screen.open();
    CHECK(screen.role() == MultiplayerScreen::Role::kNone &&
          screen.text(MultiplayerScreen::Field::kGameName) == "Game");
    CHECK(mouse(50, 300) == MultiplayerScreen::Result::kCancelled &&
          key(key::kEscape) == MultiplayerScreen::Result::kCancelled);
}

void testMenuMusic() {
    // The menu music's segments: 0 or 1 first, then 2, 3 and 4, never the
    // one just played, each in time.
    std::mt19937 random(7);
    std::array<int, 5> seen{};
    for (int start = 0; start < 20; ++start) {
        const int first = nextSegment(-1, random);
        CHECK(first == 0 || first == 1);
        ++seen[size_t(first)];
    }
    int last = 0;
    for (int i = 0; i < 200; ++i) {
        const int next = nextSegment(last, random);
        CHECK(next >= 2 && next <= 4 && next != last);
        ++seen[size_t(next)];
        last = next;
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](int n) { return n > 0; }));
}

void testGalaxyScreen() {
    // The Federation's layout: the military mode's button, the map's place
    // and the turn's text box, as hmaingal.wdf places them.
    Bytes f;
    put32(f, 2);
    putWidget(f, 1, 1000, 0, 0, 800, 600, "none");
    putWindowCounts(f, {{2, 2}, {13, 4}, {14, 2}});
    auto button = [](Bytes &l, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        putWidget(l, 2, id, x, y, w, h, "none");
        put32(l, 0xffffffff);
        l.push_back(0);
        put32(l, 0);
        put32(l, 0);
        putName(l, "none");
        putName(l, "none");
    };
    button(f, 1004, 558, 432, 87, 24);
    button(f, 1002, 650, 432, 88, 24);
    putWidget(f, 13, 1017, 249, 36, 526, 391, "none");
    put32(f, 0xffffffff);
    putWidget(f, 13, 1020, 28, 110, 196, 380, "none");
    put32(f, 0xffffffff);
    putWidget(f, 13, 1014, 7, 459, 786, 134, "none");
    put32(f, 0xffffffff);
    putWidget(f, 13, 1025, 564, 353, 175, 76, "none");
    put32(f, 0xffffffff);
    putWidget(f, 14, 1021, 63, 76, 100, 30, "none");
    for (const uint32_t v : {0U, 0U, 100U, 30U})
        put32(f, v);
    putFont(f, 1, 0, 0, "human28.fnt");
    // The line naming the sector under the pointer.
    putWidget(f, 14, 1081, 262, 8, 500, 22, "none");
    for (const uint32_t v : {0U, 0U, 500U, 22U})
        put32(f, v);
    putFont(f, 1, 0, 0, "human20.fnt");
    // A white star, 10 pixels square.
    image::Image star;
    star.w = star.h = 10;
    star.rgba.assign(400, 255);
    // The map's pictures leave out their top-left pixel's colour: black.
    image::Image keyed = star;
    keyed.rgba[0] = keyed.rgba[1] = keyed.rgba[2] = 0;
    // The empire's panel's parts: its information's text box, the
    // emblems' places (the Klingons' at 109, 2), the task forces' bar and a
    // task force's row.
    auto layout = [](uint32_t w, uint32_t h, const std::vector<std::pair<uint32_t, uint32_t>> &counts) {
        Bytes l;
        put32(l, 2);
        putWidget(l, 1, 0, 0, 0, w, h, "none");
        putWindowCounts(l, counts);
        return l;
    };
    auto textBox = [](Bytes &l, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        putWidget(l, 14, id, x, y, w, h, "none");
        for (const uint32_t v : {0U, 0U, w, h})
            put32(l, v);
        putFont(l, 1, 0, 0, "human15.fnt");
    };
    Bytes info = layout(200, 90, {{14, 1}});
    textBox(info, 1018, 17, 30, 159, 51);
    Bytes emblems = layout(200, 40, {{13, 1}});
    putWidget(emblems, 13, 1053, 109, 2, 27, 25, "none");
    put32(emblems, 0xffffffff);
    Bytes bar = layout(194, 17, {{2, 1}, {13, 1}});
    putWidget(bar, 2, 1028, 16, 0, 157, 17, "none");
    put32(bar, 0xffffffff);
    bar.push_back(0);
    put32(bar, 0);
    put32(bar, 0);
    putName(bar, "none");
    putName(bar, "none");
    putWidget(bar, 13, 1029, 179, 0, 15, 17, "none");
    put32(bar, 0xffffffff);
    Bytes row = layout(195, 40, {{13, 1}, {14, 1}});
    putWidget(row, 13, 1036, 15, 5, 30, 30, "none");
    put32(row, 0xffffffff);
    textBox(row, 1037, 66, 3, 125, 34);
    // The strips under the map: a system's, its planets' place 548 wide
    // and its text boxes, and a stellar object's, 300 wide.
    Bytes systemStrip = layout(786, 134, {{13, 1}, {14, 4}});
    putWidget(systemStrip, 13, 1020, 200, 18, 548, 100, "none");
    put32(systemStrip, 0xffffffff);
    textBox(systemStrip, 1100, 239, 0, 126, 17);
    textBox(systemStrip, 1101, 604, 0, 126, 17);
    textBox(systemStrip, 1102, 56, 52, 100, 68);
    textBox(systemStrip, 1103, 111, 52, 75, 68);
    Bytes stellarStrip = layout(786, 134, {{13, 1}, {14, 3}});
    putWidget(stellarStrip, 13, 1020, 219, 18, 300, 100, "none");
    put32(stellarStrip, 0xffffffff);
    textBox(stellarStrip, 1100, 239, 0, 126, 17);
    textBox(stellarStrip, 1101, 604, 0, 126, 17);
    textBox(stellarStrip, 1103, 525, 18, 220, 100);
    // The zoom's two buttons.
    Bytes zoom = layout(175, 76, {{2, 2}});
    button(zoom, 1022, 20, 0, 88, 18);
    button(zoom, 1023, 20, 22, 88, 18);
    MemoryArchive archive;
    archive.files["hmaingal.wdf"] = f;
    archive.files["h_zoom.wdf"] = zoom;
    archive.files["s-y-map.tga"] = image::encodeTga(keyed);
    archive.files["h_sysmap.wdf"] = systemStrip;
    archive.files["h_starmp.wdf"] = stellarStrip;
    // A white star 100 pixels square; a planet 20 square, white then
    // black; a neutron star 300 wide and 100 high, white.
    archive.files["s-y-.ani"] = makeAnimation(100, 100, {0x7FFF});
    archive.files["eart.ani"] = makeAnimation(20, 20, {0x7FFF, 0});
    archive.files["ns.ani"] = makeAnimation(300, 100, {0x7FFF});
    // The map's backdrop, grey, shown only in sectors the empire hasn't
    // seen; a neutron star's picture, another empire's task force's
    // emblem, and the marker for where a task force is ordered to.
    image::Image grey;
    grey.w = 526;
    grey.h = 391;
    grey.rgba.assign(size_t(grey.w) * size_t(grey.h) * 4, 128);
    archive.files["hm_glmap.tga"] = image::encodeTga(grey);
    archive.files["nsmaps.tga"] = image::encodeTga(keyed);
    archive.files["i_k13.tga"] = image::encodeTga(star);
    archive.files["movs.tga"] = image::encodeTga(star);
    // The strip's icons (a food bonus, dilithium: red) and the
    // Federation's emblem; and the sector line's font.
    image::Image red = keyed;
    for (size_t i = 4; i < red.rgba.size(); i += 4)
        red.rgba[i + 1] = red.rgba[i + 2] = 0;
    archive.files["bif1.tga"] = image::encodeTga(keyed);
    archive.files["bid.tga"] = image::encodeTga(red);
    archive.files["i_h46.tga"] = image::encodeTga(star);
    archive.files["human20.fnt"] = makeFont(21);
    archive.files["hinfobox.wdf"] = info;
    archive.files["h_eicon.wdf"] = emblems;
    archive.files["hinf_tf.wdf"] = bar;
    archive.files["h_tfinfo.wdf"] = row;
    archive.files["human15.fnt"] = makeFont(16);
    archive.files["i_k26.tga"] = image::encodeTga(star);
    archive.files["i_h26.tga"] = image::encodeTga(star);
    archive.files["i_hi18.tga"] = image::encodeTga(star);
    archive.files["s-y-maps.tga"] = image::encodeTga(keyed);
    archive.files["human9.fnt"] = makeFont(10);
    archive.files["human28.fnt"] = makeFont(29);
    Assets assets(archive);
    Lexicon lexicon;
    GalaxyScreen screen(assets, lexicon);
    CHECK(!screen.load(1));

    // A small galaxy's 13 by 10 sectors: Sol, the Federation's, explored;
    // Thelka next to it, not; another far away.
    SavedGalaxy g;
    g.width = 13;
    g.height = 10;
    g.sectors.resize(130);
    auto system = [&](const char *name, int x, int y, int mapX, int mapY, int race) {
        SavedSystem s;
        s.name = name;
        s.starAnimation = "s-y-";
        s.mapX = mapX;
        s.mapY = mapY;
        s.controllingRace = race;
        g.sectors[size_t(y * 13 + x)].system = int(g.systems.size());
        g.systems.push_back(s);
    };
    system("Sol", 10, 8, 52, 43, 1);
    system("Thelka", 9, 7, 47, 37, -1);
    system("Far", 2, 2, 12, 12, -1);
    SavedPlanet earth;
    earth.animation = "eart.ani";
    earth.type = 6;
    earth.foodBonus = 620;
    g.systems[0].planets = {earth};
    g.systems[0].population = 310;
    g.systems[0].dilithium = 5;
    // A neutron star next to Sol.
    SavedStellarObject neutron;
    neutron.type = 3;
    neutron.name = "Neutron Star K9";
    neutron.mapX = 52;
    neutron.mapY = 47;
    g.stellarObjects = {neutron};
    g.sectors[9 * 13 + 10].stellarObject = 0;
    g.sectors[8 * 13 + 10].explored = 1 << 1;
    // The Federation's scanners reach the sectors round Sol and round a
    // task force (all but one, hidden by a nebula); it has seen those.
    for (const auto &[cx, cy] : {std::pair{10, 8}, std::pair{5, 5}})
        for (int y = cy - 1; y <= std::min(cy + 1, 9); ++y)
            for (int x = cx - 1; x <= std::min(cx + 1, 12); ++x) {
                g.sectors[size_t(y * 13 + x)].scanStrength[1] = 5;
                g.sectors[size_t(y * 13 + x)].revealed = 1 << 1;
            }
    g.sectors[7 * 13 + 11].scanStrength[1] = -27;
    // The Federation claims Sol's sector.
    g.sectors[8 * 13 + 10].claims = 1 << 1;
    // A task force of the Federation's away from Sol scans round it too.
    SavedTaskForce t;
    t.owner = 1;
    t.x = 5;
    t.y = 5;
    SavedGame game;
    game.summary.empire = 1;
    game.galaxy = g;
    t.mission = 6;
    t.destinationX = 6;
    t.destinationY = 4;
    // A Klingon task force in the scanners' range, and one out of it.
    SavedTaskForce klingon;
    klingon.owner = 3;
    klingon.x = 9;
    klingon.y = 9;
    SavedTaskForce hidden = klingon;
    hidden.x = 0;
    hidden.y = 0;
    game.taskForces = {t, klingon, hidden};
    game.empires[1].credits = 2000;
    // The last turn's results: the Federation's two events (one economic,
    // one military), its new game's fleet (not an event), and a Klingon one.
    game.results = {{1, 0x100, 17, 3}, {1, 0x300, 4, -1}, {1, 0x300, 5, 0}, {3, 0x100, 17, 3}};
    screen.open(game);
    // The scanners reach where the save says, not where something hides.
    CHECK(screen.scanned(9, 7) && screen.scanned(11, 9) && !screen.scanned(8, 8) && !screen.scanned(11, 7));
    CHECK(screen.scanned(4, 4) && screen.scanned(6, 6) && !screen.scanned(2, 2));
    CHECK(screen.revealed(11, 7) && !screen.revealed(2, 2));
    Surface s;
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    auto white = [&](int x, int y) {
        const uint8_t *p = s.row(y) + size_t(x) * 4;
        return p[0] == 255 && p[1] == 255 && p[2] == 255;
    };
    auto any = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (s.row(y)[size_t(x) * 4 + 2] > 100 && s.row(y)[size_t(x) * 4] < 100) // not the grey backdrop
                    return true;
        return false;
    };
    // Sol's star where the game's is: the grid from (258, 36), 39 pixels a
    // sector, 7 a step of its small-map place, less 3.
    CHECK(!white(659, 366) && white(660, 366) && white(668, 375) && !white(658, 367) && !white(669, 375));
    // Thelka's is drawn, the far system's isn't; only Sol's name is.
    CHECK(white(258 + 9 * 39 + 12, 36 + 7 * 39 + 11) && !white(258 + 2 * 39 + 12, 36 + 2 * 39 + 11));
    CHECK(any(645, 376, 680, 386) && !any(605, 298, 640, 308));
    // The turn, in its box.
    CHECK(any(63, 76, 163, 106));
    // The panel: the information's lines, the Klingons' emblem in its place
    // (the panel from 28, 110; the emblems 90 down), the task force's row
    // 147 down with its emblem centred across its place and its mission.
    auto lit = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const uint8_t *p = s.row(y) + size_t(x) * 4;
                if (std::max({p[0], p[1], p[2]}) > 40)
                    return true;
            }
        return false;
    };
    CHECK(lit(45, 140, 120, 157) && lit(150, 140, 200, 157) && lit(45, 174, 120, 191));
    // "2 Events" in the figures' green; no dilithium, "0" in the words' orange.
    CHECK(screen.eventCount() == 2);
    auto greenish = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (const uint8_t *p = s.row(y) + size_t(x) * 4; p[1] > 150 && p[0] < 200)
                    return true;
        return false;
    };
    CHECK(greenish(150, 174, 200, 191) && !greenish(45, 174, 60, 191));
    CHECK(!lit(45, 192, 200, 199));
    CHECK(white(28 + 109 + 8, 110 + 90 + 2) && white(28 + 109 + 17, 110 + 90 + 11) && !white(28 + 109 + 7, 202));
    CHECK(white(28 + 15 + 10, 110 + 147 + 5) && !white(28 + 15 + 9, 110 + 147 + 5));
    CHECK(lit(94, 260, 190, 276));
    // The ranges round Sol: green a sector round it, yellow two (but not
    // two across and two down: the larger distance and half the smaller),
    // red five; the shorter drawn over the longer.
    auto colour = [&](int x, int y, uint8_t r, uint8_t gr, uint8_t b) {
        const uint8_t *p = s.row(y) + size_t(x) * 4;
        return p[0] == r && p[1] == gr && p[2] == b;
    };
    const int gx = 258;
    const int gy = 36;
    CHECK(colour(gx + 10 * 39 + 20, gy + 7 * 39, 0, 255, 0) && colour(gx + 9 * 39, gy + 8 * 39 + 20, 0, 255, 0));
    CHECK(colour(gx + 10 * 39 + 20, gy + 6 * 39, 255, 255, 0) && colour(gx + 8 * 39, gy + 7 * 39 + 20, 255, 255, 0));
    CHECK(colour(gx + 8 * 39 + 20, gy + 7 * 39, 255, 255, 0) && !colour(gx + 8 * 39 + 20, gy + 6 * 39, 255, 255, 0));
    CHECK(colour(gx + 5 * 39, gy + 8 * 39 + 20, 255, 0, 0) && colour(gx + 10 * 39 + 20, gy + 3 * 39, 255, 0, 0));
    // The grid's bottom edge closes them: green under Sol's column.
    CHECK(colour(gx + 10 * 39 + 20, gy + 10 * 39, 0, 255, 0));
    // Sol selected: an orange square just inside its sector's lines.
    CHECK(colour(gx + 10 * 39 + 1, gy + 8 * 39 + 20, 247, 125, 0) &&
          colour(gx + 10 * 39 + 20, gy + 9 * 39 - 1, 247, 125, 0));
    CHECK(!colour(gx + 10 * 39 + 2, gy + 8 * 39 + 20, 247, 125, 0));
    // The task force's emblem, a pixel right of its sector's corner and one
    // above.
    CHECK(white(gx + 5 * 39 + 1, gy + 5 * 39 - 1) && !white(gx + 5 * 39, gy + 5 * 39 - 1));
    // The backdrop only inside the sectors not seen: not in the far one's
    // lines, nor in those round Sol.
    CHECK(colour(gx + 2 * 39 + 5, gy + 3 * 39 + 5, 128, 128, 128) &&
          !colour(gx + 2 * 39, gy + 3 * 39 + 5, 128, 128, 128));
    CHECK(!colour(gx + 11 * 39 + 30, gy + 9 * 39 + 30, 128, 128, 128));
    // Sol's sector's claim: the Federation's dots, a pixel every 3 on every
    // other row, shifting.
    CHECK(colour(gx + 10 * 39 + 5, gy + 8 * 39 + 3, 16, 8, 115) &&
          colour(gx + 10 * 39 + 4, gy + 8 * 39 + 5, 16, 8, 115) &&
          !colour(gx + 10 * 39 + 5, gy + 8 * 39 + 5, 16, 8, 115) &&
          !colour(gx + 10 * 39 + 5, gy + 8 * 39 + 4, 16, 8, 115));
    // The lines round the scanners' sectors dotted from each sector's corner.
    CHECK(colour(gx + 4 * 39, gy + 4 * 39 + 2, 24, 73, 173) && !colour(gx + 4 * 39, gy + 4 * 39 + 1, 24, 73, 173));
    // The neutron star seen in the sector under Sol's, centred where a star
    // goes; the Klingons' task force in range, not the one out of it; the
    // marker where the Federation's is ordered to.
    CHECK(white(gx + 10 * 39 + 12, gy + 9 * 39 + 11) && white(gx + 10 * 39 + 20, gy + 9 * 39 + 20));
    CHECK(white(gx + 9 * 39 + 1, gy + 9 * 39 + 1) && !white(gx + 1, gy + 1));
    CHECK(white(gx + 6 * 39 + 14, gy + 4 * 39 + 14) && white(gx + 6 * 39 + 23, gy + 4 * 39 + 23) &&
          !white(gx + 6 * 39 + 13, gy + 4 * 39 + 14));

    // An animation's white, 5 bits a colour.
    auto shown = [&](int x, int y) {
        const uint8_t *p = s.row(y) + size_t(x) * 4;
        return p[0] == 248 && p[1] == 248 && p[2] == 248;
    };
    // Sol's strip, from (7, 459): its star at the right end of the planets'
    // place (207 to 755), the planet 16 pixels left of it, both in the
    // middle of the place's height; the planet's class letter under it, M
    // in green (Earth's needs no terraforming); the texts.
    CHECK(shown(655, 477) && shown(754, 576) && !shown(654, 527) && !shown(755, 527));
    CHECK(shown(619, 517) && shown(638, 536) && !shown(618, 527) && !shown(639, 527));
    CHECK(lit(620, 537, 638, 549) && !colour(629, 545, 255, 0, 0));
    CHECK(lit(246, 459, 290, 476) && lit(299, 459, 330, 476) && lit(611, 459, 670, 476));
    CHECK(lit(63, 511, 140, 528) && lit(160, 511, 193, 528) && !lit(150, 545, 193, 579));
    // Earth's food bonus on its frame's top from its left edge (619, 517);
    // Sol's dilithium at its star's corner; the Federation's emblem at the
    // planets' place's corner at 64% (255 to 163).
    CHECK(white(620, 507) && white(628, 516) && !white(620, 506));
    CHECK(colour(656, 477, 255, 0, 0) && colour(664, 486, 255, 0, 0));
    CHECK(colour(208, 478, 163, 163, 163) && colour(216, 486, 163, 163, 163));
    // The planet turns: its second frame, black, at its frame's time.
    screen.setTime(198);
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(!shown(629, 527) && shown(700, 527));
    // A click on the map's neutron star selects it: its strip, 300 wide in
    // the middle of its place, with its name and description.
    CHECK(screen.click(gx + 10 * 39 + 5, gy + 9 * 39 + 5) && (screen.selected() == std::pair{10, 9}));
    CHECK(!screen.click(10, 10) && (screen.selected() == std::pair{10, 9}));
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(shown(226, 477) && shown(525, 576) && !shown(225, 527) && !shown(526, 527));
    CHECK(lit(246, 459, 330, 476) && lit(611, 459, 700, 476));
    // An empty sector has none.
    screen.select(0, 0);
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(!lit(7, 459, 793, 476) && !shown(376, 527));

    // The pointer over Thelka's sector (9, 7), seen but not explored: its
    // name and the scan strength over the map; off the map, nothing.
    auto move = [&](int x, int y) {
        Event e;
        e.type = EventType::kMouseMove;
        e.mouseX = x;
        e.mouseY = y;
        return screen.handle(e);
    };
    CHECK(move(gx + 9 * 39 + 5, gy + 7 * 39 + 5) && (screen.pointed() == std::pair{9, 7}));
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(lit(262, 8, 400, 30) && lit(650, 8, 756, 30) && !lit(757, 8, 762, 30));
    CHECK(move(10, 10) && (screen.pointed() == std::pair{-1, -1}) && !move(12, 10));
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(!lit(262, 8, 762, 30));

    // Zoomed in on Sol: sectors 61 pixels square, Sol's corner at the
    // middle of the zoomed-out grid's square (258 to 765, 36 to 426), but
    // the grid kept over all of it: so from (-28, -184).
    screen.select(10, 8);
    screen.setZoomedIn(true);
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    const int zx = -28 + 10 * 61;
    const int zy = -184 + 8 * 61;
    CHECK(colour(zx + 1, zy + 30, 247, 125, 0) && colour(zx + 60, zy + 30, 247, 125, 0) &&
          !colour(zx + 2, zy + 30, 247, 125, 0));
    // Sol's star, 12 pixels a step of its small-map place, less 8; the task
    // force's larger emblem, a pixel right of and below its sector's corner.
    CHECK(white(zx + 17, zy + 28) && white(zx + 25, zy + 37) && !white(zx + 15, zy + 29));
    CHECK(white(-28 + 5 * 61 + 1, -184 + 5 * 61 + 1) && !white(-28 + 5 * 61, -184 + 5 * 61 + 1));
    // Nothing of the map left of the square.
    CHECK(!lit(249, 36, 258, 427));
    // A click selects the zoomed sector under it, and the map stays put.
    CHECK(screen.click(-28 + 9 * 61 + 5, -184 + 7 * 61 + 5) && (screen.selected() == std::pair{9, 7}));
    s.create(800, 600, PixelFormat::kRGBA32);
    screen.draw(s);
    CHECK(colour(-28 + 9 * 61 + 1, -184 + 7 * 61 + 30, 247, 125, 0));
    // The Map button opens the zoom's buttons (at 564, 353); Large Map
    // zooms out and closes them; closed, their place is the map's; Small
    // Map zooms in.
    auto press = [&](int x, int y) {
        Event e;
        e.type = EventType::kMouseDown;
        e.button = MouseButton::kLeft;
        e.mouseX = x;
        e.mouseY = y;
        return screen.handle(e);
    };
    CHECK(press(700, 440) && press(600, 380) && !screen.zoomedIn());
    CHECK(press(600, 360) && !screen.zoomedIn() && (screen.selected() == std::pair{8, 8}));
    CHECK(press(700, 440) && press(600, 360) && screen.zoomedIn());
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
    kSuite("botf.unit.screens",
           {
               {"main menu", [](const std::filesystem::path &) { testMainMenu(); }},
               {"options", [](const std::filesystem::path &) { testOptionsScreen(); }},
               {"load game", [](const std::filesystem::path &) { testLoadSaveScreen(); }},
               {"from Stbof.ini", [](const std::filesystem::path &) { testFromIni(); }},
               {"timers", [](const std::filesystem::path &) { testTimers(); }},
               {"save description", [](const std::filesystem::path &) { testSaveDescription(); }},
               {"menu music", [](const std::filesystem::path &) { testMenuMusic(); }},
               {"hall of fame", [](const std::filesystem::path &) { testFameScreen(); }},
               {"multiplayer setup", [](const std::filesystem::path &) { testMultiplayerScreen(); }},
               {"game setup", [](const std::filesystem::path &) { testSetupScreen(); }},
               {"empire selection", [](const std::filesystem::path &) { testEmpireScreen(); }},
               {"galaxy", [](const std::filesystem::path &) { testGalaxyScreen(); }},
           });
