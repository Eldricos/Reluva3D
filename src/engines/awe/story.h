// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/conditions.h"
#include "engines/awe/script.h"
#include "engines/awe/tables.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The game's acts (motoe.exe's game object, 0x422580..0x423070): the
// rows of Text/acts.txt, and how an act starts: its PreLoad script, its
// location loaded once that has run, its Post Load script (which shows
// the first scene); its time of day, weather and the train's moving,
// taking effect when a scene is left; its Completion Conditions, checked
// whenever the main runner's queue runs dry, and its Event then. Timers
// tick here too. See docs/formats/awe/acts.md.

namespace reluva::awe {

struct ActRow {
    std::string act; // "01a"
    std::string preLoad, location, postLoad, completion, event, music;
    int time = 0;    // 1: night
    int weather = 0; // 1: snow, 2: blizzard
    int moving = 0;  // 1: the train is moving
};

// An act's name, as "act" takes it ("06a", "6A"): its number and letter
// (A = 1), from the name without its last character and that character
// (0x43e0a0, 0x43e0f0). False if it's shorter than two characters.
bool parseActName(std::string_view s, int& major, int& minor);
// "%02d%c", lower case: "06a".
std::string actName(int major, int minor);

// acts.txt's rows (0x423070): the cells' quotes taken off, Time of Day,
// Weather and Train Moving read as the game reads them ("night", "snow"
// or "blizzard", "yes", anywhere in the cell, ignoring case).
std::vector<ActRow> readActs(const Table& table);

class Story {
public:
    // `main` is the main runner: the act's scripts go there.
    Story(GameState& state, ScriptRunner& main);

    void setActs(std::vector<ActRow> acts) {
        acts_ = std::move(acts);
        row_.reset();
    }
    const ActRow* row() const { return row_ ? &*row_ : nullptr; }
    // A saved game's act (0x422470): its row as saved (acts.txt isn't read
    // again), its location loaded through onLoadLocation, but neither
    // PreLoad nor Post Load run; done() once loaded.
    void restore(const ActRow& row, const std::string& location);

    // "act [<act>]" (0x409eaa -> 0x422580): the act changes (or, with no
    // argument, starts again), its dialogue tables are loaded and, if
    // acts.txt has a row for it, its PreLoad is queued and its location
    // is loaded when the main runner is next idle. False if there's no
    // row (01A's Event, "act 01b": only the act changes, the last
    // row stays).
    bool startAct(std::string_view arg);
    // Moves time on: timers, and the act's loading.
    void update(float seconds);
    // A scene is being left (0x423390): the act's time of day, weather
    // and moving take effect.
    void leavingScene();
    // Whether an act's location is still to be loaded.
    bool loading() const { return loading_; }

    // The act's dialogue tables are to be loaded (on every act change).
    std::function<void(const std::string& act)> onActTables;
    // The act's location is to be loaded (the scene left first:
    // leavingScene() has been called); `flush` when the scenes kept so far
    // are dropped (a new place: before act 04A and from 08A on). Post Load
    // runs once locationLoaded() is called (at once without a handler).
    std::function<void(const std::string& location, bool flush)> onLoadLocation;
    void locationLoaded();
    const std::string& location() const { return location_; }

private:
    void runnerEmpty();

    GameState& state_;
    ScriptRunner& main_;
    std::vector<ActRow> acts_;
    std::optional<ActRow> row_; // the act's row, as it was read (or saved)
    bool restoring_ = false;
    std::string location_, previousLocation_;
    bool loading_ = false, waitingLoad_ = false;
    float second_ = 0;
};

} // namespace reluva::awe
