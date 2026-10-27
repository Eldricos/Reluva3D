// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/story.h"

#include "common/fs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace reluva::awe {

bool parseActName(std::string_view s, int& major, int& minor) {
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

bool contains(const std::string& cell, std::string_view word) {
    return fs::toLower(cell).find(word) != std::string::npos;
}

// A cell of "0" is empty.
bool given(const std::string& s) { return !s.empty() && s != "0"; }

} // namespace

std::vector<ActRow> readActs(const Table& table) {
    std::vector<ActRow> out;
    for (size_t i = 0; i < table.rows.size(); i++) {
        auto cell = [&](int column) { return unquote(table.cell(i, column)); };
        int major, minor;
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

Story::Story(GameState& state, ScriptRunner& main) : state_(state), main_(main) {
    main_.onEmpty = [this] { runnerEmpty(); };
}

bool Story::startAct(std::string_view arg) {
    // A bare "act" (the main menu's Play) starts the act there is.
    int major, minor;
    if (parseActName(arg, major, minor))
        state_.act = actName(major, minor);
    if (onActTables)
        onActTables(state_.act);
    // Without a row the last act's stays (0x423070 clears it only when it
    // finds one).
    const ActRow* found = nullptr;
    for (const ActRow& r : acts_)
        if (r.act == state_.act)
            found = &r;
    if (!found)
        return false;
    row_ = *found;
    previousLocation_ = location_;
    location_ = row_->location;
    loading_ = true;
    waitingLoad_ = false;
    if (given(row_->preLoad))
        main_.run(row_->preLoad);
    return true;
}

void Story::update(float seconds) {
    // Every timer goes on by a second each second (0x412580).
    second_ += seconds;
    for (; second_ >= 1; second_ -= 1)
        addTime(state_, 1);
    // The load state machine (0x422a90): once PreLoad (and whatever was
    // queued before it) has run, the location; then, once it's loaded,
    // Post Load.
    if (loading_ && !waitingLoad_ && main_.idle()) {
        waitingLoad_ = true;
        leavingScene();
        // A new place before act 04A and from 08A on drops what was kept
        // (0x423430); inside the train only the on-demand scenes go.
        const bool flush =
            !previousLocation_.empty() && previousLocation_ != location_ && (state_.act < "04a" || state_.act >= "08a");
        if (onLoadLocation)
            onLoadLocation(location_, flush);
        else
            locationLoaded();
    }
}

void Story::restore(const ActRow& row, const std::string& location) {
    row_ = row;
    previousLocation_ = location_;
    location_ = location;
    restoring_ = true;
    // At once: the load's own fade out has been waited for by the caller.
    loading_ = waitingLoad_ = true;
    leavingScene();
    const bool flush =
        !previousLocation_.empty() && previousLocation_ != location_ && (state_.act < "04a" || state_.act >= "08a");
    if (onLoadLocation)
        onLoadLocation(location_, flush);
    else
        locationLoaded();
}

void Story::locationLoaded() {
    if (!loading_ || !waitingLoad_)
        return;
    loading_ = waitingLoad_ = false;
    if (restoring_) {
        restoring_ = false;
        return;
    }
    if (row_ && given(row_->postLoad))
        main_.run(row_->postLoad);
}

void Story::leavingScene() {
    if (!row_)
        return;
    state_.values["time"] = row_->time;
    state_.values["weather"] = row_->weather;
    state_.values["moving"] = row_->moving;
}

void Story::runnerEmpty() {
    // The act's Completion Conditions, each time the queue runs dry
    // (0x409a80); "0" never holds.
    if (row_ && given(row_->completion) && testCondition(row_->completion, state_))
        main_.run(row_->event);
}

} // namespace reluva::awe
