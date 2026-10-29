// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The rest of the game's menus, redrawn as the others are (front_end.h):
// the Options (audio, graphics, controls and their keys, about), and the
// dialogs the main menu opens. Each screen is tune/widget.csv's controls
// on its jpg/ background. See docs/engines/age-menus.md.

#include "common/system.h"
#include "engines/age/front_end.h"
#include "engines/age/options.h"
#include "engines/age/record.h"

#include "engines/age/replay.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace reluva::age {

class GameMenus {
public:
    enum class Screen {
        None,
        Options,
        Audio,
        Graphics,
        Control,
        Customize,
        About,
        Defaults,        // odef_dlg: restore the defaults?
        DuplicateKey,    // ctrl_dlg
        Quit,            // quit_dlg
        ReplayList,      // rpl_bk: choose a replay file
        ReplayName,      // REDIT_BK: save as, or rename
        ReplayAsk,       // "Do you want to save this replay?"
        NewDriver,       // newp_dlg: Create a New Driver
        DuplicateDriver, // dupp_dlg
        DeleteDriver,    // delp_dlg: are you sure?
        LastDriver,      // lstp_dlg: you cannot delete your only driver
        DriverStats,     // drec_dlg: Driver's Stats
        RaceRecords,     // hoff_dlg: Race Records
    };
    enum class Action {
        None,
        Closed,        // back to whatever opened it
        Quit,          // leave the game
        LoadReplay,    // play replayFile()
        SaveReplay,    // save the replay played as replayName()
        DriverChanged, // a driver added or deleted: the current one's record to read
    };

    GameMenus(System& sys, const SearchSet& files, const GameStrings& strings, audio::Mixer* mixer,
              std::filesystem::path savePath);
    bool loaded() const { return ok_; }

    void open(Screen s);
    Screen screen() const { return screen_; }
    bool isOpen() const { return screen_ != Screen::None; }
    Action handle(const Event& ev);
    void draw(Surface& overlay, uint64_t nowMs);
    bool typing() const;
    // After a replay: ask whether to save it, under this name.
    void askToSaveReplay(const std::string& name);
    // Save the replay played as (the replay's own Save).
    void nameReplay(const std::string& name);
    // The drivers and their records, for the driver dialogs (and the
    // cities, for the records' tables).
    void setDrivers(DriverRoster* roster, PlayerRecord* record, const std::vector<CityInfo>* cities) {
        roster_ = roster;
        record_ = record;
        cities_ = cities;
    }
    const std::filesystem::path& replayFile() const { return replayFile_; }
    const std::string& replayName() const { return name_; }

private:
    enum class Kind { Button, Check, Bar, Choice, List, Label, Text };
    struct Item {
        Kind kind = Kind::Button;
        std::string widget;  // widget.csv's name
        std::string picture; // a button's or check's texture
        int states = 4;
        bool enabled = true;
    };
    std::vector<Item> items() const;
    std::string menuName() const;
    int originX() const;
    int originY() const;
    const Widget* widget(const std::string& name) const;
    void go(Screen s);
    Action activate(const Item& item);
    void step(const Item& item, int dir); // Left/Right
    bool checked(const std::string& w) const;
    void toggle(const std::string& w);
    float share(const std::string& w) const;
    void setShare(const std::string& w, float v);
    std::string choiceText(const std::string& w) const;
    std::string description(const std::string& w) const; // its jpg/ picture
    bool hit(const Item& item, int x, int y, int& w, int& h) const;
    void drawItems(Surface& o, const std::vector<Item>& list, int focus) const;
    void drawScreenWords(Surface& o, uint64_t nowMs) const;
    void text(Surface& o, const std::string& widget, const std::string& s, Color c = {255, 230, 140}) const;
    Action done(); // the options kept, saved and put in force
    Action cancel();

    System& sys_;
    const SearchSet& files_;
    const GameStrings& strings_;
    audio::Mixer* mixer_;
    std::filesystem::path savePath_;
    mutable MenuPictures pictures_;
    std::vector<Widget> widgets_;
    bool ok_ = false;

    Screen screen_ = Screen::None;
    Screen under_ = Screen::None; // the screen a dialog is over
    int focus_ = 0;
    GameOptions edit_;         // as changed on the screens
    GameOptions entered_;      // as they were when the Options opened
    int row_ = 0, column_ = 0; // Customize Controls: the control and which of its two keys
    bool waitingForKey_ = false;
    uint64_t openedMs_ = 0, nowMs_ = 0;
    std::vector<ReplayEntry> replays_;
    size_t replayRow_ = 0;
    std::filesystem::path replayFile_;
    std::string name_;            // the replay's name being typed
    bool overlay(Screen s) const; // a dialog over whatever's beneath (drawn already)
    void drawReplayList(Surface& o) const;

    // The driver dialogs (game_menus_drivers.cpp).
    DriverRoster* roster_ = nullptr;
    PlayerRecord* record_ = nullptr;
    const std::vector<CityInfo>* cities_ = nullptr;
    std::string newName_;
    bool newPro_ = false;
    std::array<bool, 8> filters_{true, true, false, true, true, true, true, true};
    std::vector<std::string> table_; // the stats' or records' lines
    size_t tableRow_ = 0;
    bool driverScreen(Screen s) const { return s >= Screen::NewDriver && s <= Screen::RaceRecords; }
    std::string driverMenuName() const;
    void driverOrigin(int& x, int& y) const;
    std::vector<Item> driverItems() const;
    Action driverActivate(const Item& item);
    bool driverChecked(const std::string& w) const;
    void driverToggle(const std::string& w);
    void buildTable();
    void drawTable(Surface& o) const;
    const char* driverPicture(Screen s) const;
};

} // namespace reluva::age
