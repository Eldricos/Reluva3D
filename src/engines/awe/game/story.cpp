// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/story.h"

#include "common/fs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace reluva::awe {

bool parseActName(std::string_view s, int &major, int &minor) {
    if (s.size() < 2)
        return false;
    major = std::atoi(std::string(s.substr(0, s.size() - 1)).c_str());
    minor = std::toupper(uint8_t(s.back())) - 0x40;
    return true;
}

std::string actName(int major, int minor) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%c", major, char(minor + 0x60));
    return buf;
}

namespace {

// 0x43e340: surrounding quotes off, doubled ones single.
std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        out += s[i];
        if (s[i] == '"' && i + 1 < s.size() && s[i + 1] == '"')
            i++;
    }
    return out;
}

bool contains(const std::string &cell, std::string_view word) {
    return fs::toLower(cell).find(word) != std::string::npos;
}

// A cell of "0" is empty.
bool given(const std::string &s) {
    return !s.empty() && s != "0";
}

} // namespace

std::vector<ActRow> readActs(const Table &table) {
    std::vector<ActRow> out;
    for (size_t i = 0; i < table.rows.size(); i++) {
        auto cell = [&](int column) { return unquote(table.cell(i, column)); };
        int major;
        int minor;
        if (!parseActName(cell(0), major, minor))
            continue;
        ActRow r;
        r.act = actName(major, minor);
        r.preLoad = cell(1);
        // Trailing spaces ("calais.hall " in 06A) go, as Windows drops them
        // from a folder's name.
        r.location = fs::toLower(cell(2));
        while (!r.location.empty() && r.location.back() == ' ')
            r.location.pop_back();
        r.postLoad = cell(3);
        r.time = contains(cell(4), "night") ? 1 : 0;
        r.weather = contains(cell(5), "blizzard") ? 2 : contains(cell(5), "snow") ? 1 : 0;
        r.moving = contains(cell(6), "yes") ? 1 : 0;
        r.completion = cell(7);
        r.event = cell(8);
        r.music = cell(9);
        out.push_back(std::move(r));
    }
    return out;
}

Story::Story(GameState &state, ScriptRunner &main) : _state(state), _main(main) {
    _main.onEmpty = [this] { runnerEmpty(); };
}

bool Story::startAct(std::string_view arg) {
    // A bare "act" (the main menu's Play) starts the act there is.
    int major;
    int minor;
    if (parseActName(arg, major, minor))
        _state.act = actName(major, minor);
    if (onActTables)
        onActTables(_state.act);
    // Without a row the last act's stays (0x423070 clears it only when it
    // finds one).
    const ActRow *found = nullptr;
    for (const ActRow &r : _acts)
        if (r.act == _state.act)
            found = &r;
    if (!found)
        return false;
    _row = *found;
    _previousLocation = _location;
    _location = _row->location;
    _loading = true;
    _waitingLoad = false;
    if (given(_row->preLoad))
        _main.run(_row->preLoad);
    return true;
}

void Story::update(float seconds) {
    // Every timer goes on by a second each second (0x412580).
    _second += seconds;
    for (; _second >= 1; _second -= 1)
        addTime(_state, 1);
    // The load state machine (0x422a90): once PreLoad (and whatever was
    // queued before it) has run, the location; then, once it's loaded,
    // Post Load.
    if (_loading && !_waitingLoad && _main.idle()) {
        _waitingLoad = true;
        leavingScene();
        // A new place before act 04A and from 08A on drops what was kept
        // (0x423430); inside the train only the on-demand scenes go.
        const bool flush =
            !_previousLocation.empty() && _previousLocation != _location && (_state.act < "04a" || _state.act >= "08a");
        if (onLoadLocation)
            onLoadLocation(_location, flush);
        else
            locationLoaded();
    }
}

bool Story::sceneLocation(const std::string &location) {
    _previousLocation = _location;
    _location = location;
    return !_previousLocation.empty() && _previousLocation != _location && (_state.act < "04a" || _state.act >= "08a");
}

void Story::restore(const ActRow &row, const std::string &location) {
    _row = row;
    _previousLocation = _location;
    _location = location;
    _restoring = true;
    // At once: the load's own fade out has been waited for by the caller.
    _loading = _waitingLoad = true;
    leavingScene();
    const bool flush =
        !_previousLocation.empty() && _previousLocation != _location && (_state.act < "04a" || _state.act >= "08a");
    if (onLoadLocation)
        onLoadLocation(_location, flush);
    else
        locationLoaded();
}

void Story::locationLoaded() {
    if (!_loading || !_waitingLoad)
        return;
    _loading = _waitingLoad = false;
    if (_restoring) {
        _restoring = false;
        return;
    }
    if (_row && given(_row->postLoad))
        _main.run(_row->postLoad);
}

void Story::leavingScene() {
    if (!_row)
        return;
    _state.values["time"] = _row->time;
    _state.values["weather"] = _row->weather;
    _state.values["moving"] = _row->moving;
}

void Story::runnerEmpty() {
    // The act's Completion Conditions, each time the queue runs dry
    // (0x409a80); "0" never holds.
    if (_row && given(_row->completion) && testCondition(_row->completion, _state))
        _main.run(_row->event);
}

} // namespace reluva::awe
