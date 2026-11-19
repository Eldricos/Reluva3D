// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The rest of the game's menus, redrawn as the others are (front_end.h):
// the Options (audio, graphics, controls and their keys, about), and the
// dialogs the main menu opens. Each screen is tune/widget.csv's controls
// on its jpg/ background. See docs/engines/age/games/mm2/systems/menus.md.

#include "common/system.h"
#include "engines/age/ui/front_end.h"
#include "engines/age/game/options.h"
#include "engines/age/game/record.h"

#include "engines/age/formats/replay.h"

#include <filesystem>
#include <functional>
#include <utility>
#include <string>
#include <vector>

namespace reluva::age {

/** The Options and the main menu's dialogs, opened over the front end or a race. */
class GameMenus {
public:
    /** The screen or dialog shown; kNone while the menus are closed. */
    enum class Screen {
        kNone,
        kOptions,
        kAudio,
        kGraphics,
        kControl,
        kCustomize,
        kAbout,
        kDefaults,        // odef_dlg: restore the defaults?
        kDuplicateKey,    // ctrl_dlg
        kControlWarning,  // Control Warning: no game controller connected (Reluva's words)
        kQuit,            // quit_dlg
        kReplayList,      // rpl_bk: choose a replay file
        kReplayName,      // REDIT_BK: save as, or rename
        kReplayAsk,       // "Do you want to save this replay?"
        kNewDriver,       // newp_dlg: Create a New Driver
        kDuplicateDriver, // dupp_dlg
        kDeleteDriver,    // delp_dlg: are you sure?
        kLastDriver,      // lstp_dlg: you cannot delete your only driver
        kDriverStats,     // drec_dlg: Driver's Stats
        kRaceRecords,     // hoff_dlg: Race Records
        kRaceOptions,     // Renv_dlg: Change Race Options
        kCitySelection,   // cenv_dlg: Race Locales
        kLocked,          // lock_dlg: you cannot pick a locked vehicle
    };
    /** What the menus want their caller to do next. */
    enum class Action {
        kNone,
        kClosed,        // back to whatever opened it
        kQuit,          // leave the game
        kLoadReplay,    // play replayFile()
        kSaveReplay,    // save the replay played as replayName()
        kDriverChanged, // a driver added or deleted: the current one's record to read
    };

    /**
     * Reads the menus' layout and pictures; loaded() says whether they came.
     * @param mixer The sound output the Audio Options set; nullptr with none.
     * @param savePath Where the options are saved and the replays found.
     */
    GameMenus(System &sys, const SearchSet &files, const GameStrings &strings, audio::Mixer *mixer,
              std::filesystem::path savePath);
    /** Whether the menus' layout (tune/widget.csv) and the Options' background were found. */
    bool loaded() const { return _ok; }

    /** Opens a screen, starting from the options in force. */
    void open(Screen s);
    /** The screen or dialog shown. */
    Screen screen() const { return _screen; }
    /** Whether a screen or dialog is shown. */
    bool isOpen() const { return _screen != Screen::kNone; }
    /** A dialog over the menu that opened it (drawn first). */
    bool isDialog() const { return isOpen() && overlay(_screen); }
    /** Takes a key, click or typing; kNone while the menus are closed. */
    Action handle(const Event &ev);
    /** Draws the screen shown into `overlay`; nothing while the menus are closed. */
    void draw(Surface &overlay, uint64_t nowMs);
    /** Whether keys go to a field typed in, or to a control being given a key. */
    bool typing() const;
    /** After a replay: ask whether to save it, under this name (cut at 40 characters). */
    void askToSaveReplay(const std::string &name);
    /** Save the replay played as (the replay's own Save): opens the name to change, cut at 40 characters. */
    void nameReplay(const std::string &name);
    /**
     * The drivers and their records, for the driver dialogs (and the
     * cities, for the records' tables).
     */
    void setDrivers(DriverRoster *roster, PlayerRecord *record, const std::vector<CityInfo> *cities) {
        _roster = roster;
        _record = record;
        _cities = cities;
    }
    /** The race menu's choice, for Change Race Options and Race Locales. */
    void setFrontEnd(FrontEnd *front) { _front = front; }
    /** The replay file chosen to play (Action::kLoadReplay). */
    const std::filesystem::path &replayFile() const { return _replayFile; }
    /** A recording to say now (the time or weather chosen, an alert), once. */
    std::string takeVoice() { return std::exchange(_voice, std::string()); }
    /** The name to save the replay under (Action::kSaveReplay). */
    const std::string &replayName() const { return _name; }

private:
    enum class Kind { kButton, kCheck, kBar, kChoice, kList, kLabel, kText };
    struct Item {
        Kind kind = Kind::kButton;
        std::string widget;  // widget.csv's name
        std::string picture; // a button's or check's texture
        int states = 4;
        bool enabled = true;
    };
    std::vector<Item> items() const;
    std::string menuName() const;
    int originX() const;
    int originY() const;
    const Widget *widget(const std::string &name) const;
    void go(Screen s);
    Action activate(const Item &item);
    void step(const Item &item, int dir); // Left/Right
    bool checked(const std::string &w) const;
    void toggle(const std::string &w);
    float share(const std::string &w) const;
    void setShare(const std::string &w, float v);
    std::string choiceText(const std::string &w) const;
    std::string description(const std::string &w) const; // its jpg/ picture
    bool hit(const Item &item, int x, int y, int &w, int &h) const;
    void drawItems(Surface &o, const std::vector<Item> &list, int focus) const;
    void drawScreenWords(Surface &o, uint64_t nowMs) const;
    void drawControlWarning(Surface &o) const;
    void text(Surface &o, const std::string &widget, const std::string &s, Color c = {255, 230, 140}) const;
    Action done(); // the options kept, saved and put in force
    Action cancel();

    System &_sys;
    const SearchSet &_files;
    const GameStrings &_strings;
    audio::Mixer *_mixer;
    std::filesystem::path _savePath;
    mutable MenuPictures _pictures;
    std::vector<Widget> _widgets;
    bool _ok = false;

    Screen _screen = Screen::kNone;
    Screen _under = Screen::kNone; // the screen a dialog is over
    int _controllerBefore = 0;     // CONTROLLERS before Control Warning asked
    int _focus = 0;
    GameOptions _edit;    // as changed on the screens
    GameOptions _entered; // as they were when the Options opened
    // Customize Controls: the control and which of its two keys
    int _row = 0;
    int _column = 0;
    int _listTop = 0; // and the first shown
    bool _waitingForKey = false;
    uint64_t _openedMs = 0;
    uint64_t _nowMs = 0;
    std::vector<ReplayEntry> _replays;
    size_t _replayRow = 0;
    std::filesystem::path _replayFile;
    std::string _name;            // the replay's name being typed
    bool overlay(Screen s) const; // a dialog over whatever's beneath (drawn already)
    void drawReplayList(Surface &o) const;

    // The driver dialogs (game_menus_drivers.cpp).
    DriverRoster *_roster = nullptr;
    PlayerRecord *_record = nullptr;
    const std::vector<CityInfo> *_cities = nullptr;
    std::string _newName;
    bool _newPro = false;
    std::array<bool, 8> _filters{true, true, false, true, true, true, true, true};
    std::vector<std::string> _table; // the stats' or records' lines
    size_t _tableRow = 0;
    // The dialogs over a menu kept in game_menus_drivers.cpp: the drivers',
    // and the race menu's.
    bool driverScreen(Screen s) const { return s >= Screen::kNewDriver && s <= Screen::kLocked; }
    FrontEnd *_front = nullptr;
    FrontEnd::Choice::Conditions _conditions; // as Change Race Options changes them
    size_t _cityRow = 0;
    int barInset() const { return _screen == Screen::kRaceOptions ? 20 : 23; }
    void drawConditionIcons(Surface &o, const std::string &w, bool focused) const;
    void stepCondition(const std::string &w, int dir);
    void sayCondition(bool time);
    std::string _voice;
    float raceShare(const std::string &w) const;
    void setRaceShare(const std::string &w, float v);
    std::string driverMenuName() const;
    void driverOrigin(int &x, int &y) const;
    std::vector<Item> driverItems() const;
    Action driverActivate(const Item &item);
    bool driverChecked(const std::string &w) const;
    void driverToggle(const std::string &w);
    void buildTable();
    void drawTable(Surface &o) const;
    const char *driverPicture(Screen s) const;
};

} // namespace reluva::age
