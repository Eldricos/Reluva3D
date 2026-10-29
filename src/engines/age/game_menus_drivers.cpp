// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The main menu's driver dialogs: Create a New Driver (jpg/newp_dlg), the
// duplicate name's warning (dupp_dlg), deleting one (delp_dlg, or
// lstp_dlg for the only one), the Driver's Stats (drec_dlg) and the Race
// Records (hoff_dlg), at tune/widget.csv's places in tune/menu.csv's.
// See docs/engines/age-menus.md.

#include "engines/age/game_menus.h"

#include "common/font.h"
#include "engines/age/race.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

// The tables' box, inside the stats' and records' dialogs (measured on
// jpg/drec_dlg and hoff_dlg), and a line's height.
constexpr int kTableX = 26, kTableY = 64, kTableRows = 16, kTableRow = 14;
// Where those two go: tune/menu.csv puts them at (50, 37), but they're 460
// high; up to fit.
constexpr int kBigX = 50, kBigY = 10;

const RaceKind kKinds[3] = {RaceKind::Blitz, RaceKind::Circuit, RaceKind::Checkpoint};

std::string timeText(float seconds) {
    if (seconds < 0)
        return "-";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.0f));
    return buf;
}

std::string placeText(int place) {
    static const char* const kPlaces[] = {"-", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
    return kPlaces[std::clamp(place, 0, 8)];
}

} // namespace

std::string GameMenus::driverMenuName() const {
    switch (screen_) {
    case Screen::NewDriver:
        return "Create a New Driver";
    case Screen::DuplicateDriver:
        return "dupp_dlg";
    case Screen::DeleteDriver:
        return "delp_dlg";
    case Screen::LastDriver:
        return "lastp_dlg";
    case Screen::DriverStats:
        return "Driver Record";
    case Screen::RaceRecords:
        return "Race Statistics";
    default:
        return {};
    }
}

void GameMenus::driverOrigin(int& x, int& y) const {
    switch (screen_) {
    case Screen::NewDriver:
        x = 120, y = 75;
        break;
    case Screen::DuplicateDriver:
    case Screen::DeleteDriver:
        x = 170, y = 127;
        break;
    case Screen::LastDriver:
        x = 120, y = 202;
        break;
    default:
        x = kBigX, y = kBigY;
        break;
    }
}

const char* GameMenus::driverPicture(Screen s) const {
    switch (s) {
    case Screen::NewDriver:
        return "newp_dlg";
    case Screen::DuplicateDriver:
        return "dupp_dlg";
    case Screen::DeleteDriver:
        return "delp_dlg";
    case Screen::LastDriver:
        return "lstp_dlg";
    case Screen::DriverStats:
        return "drec_dlg";
    case Screen::RaceRecords:
        return "hoff_dlg";
    default:
        return "";
    }
}

std::vector<GameMenus::Item> GameMenus::driverItems() const {
    auto button = [](const std::string& w, const std::string& pic) { return Item{Kind::Button, w, pic, 4, true}; };
    auto box = [](const std::string& w, bool enabled = true) { return Item{Kind::Check, w, "dlg_chkb", 5, enabled}; };
    switch (screen_) {
    case Screen::NewDriver:
        // The name, Amateur or Professional (texture/Checkbox), Cancel, Done.
        return {Item{Kind::Text, "Enter New Driver Name", {}, 0, true},
                Item{Kind::Check, "checkbox", "Checkbox", 5, true},
                Item{Kind::Check, "checkbox#2", "Checkbox", 5, true}, button("popup_cancel", "dlg_can"),
                button("popup_done", "dlg_done")};
    case Screen::DuplicateDriver:
        return {button("popup_ok", "dlg_ok")};
    case Screen::DeleteDriver:
        return {button("dlg_done", "dlg_yes"), button("dlg_cancel", "dlg_no")};
    case Screen::LastDriver:
        return {button("dlg_done", "dlg_ok")};
    case Screen::DriverStats:
        // Blitz, Circuit, Checkpoint; San Francisco, London.
        return {Item{Kind::List, "table", {}, 0, true},
                box("popup_checkbox"),
                box("popup_checkbox#2"),
                box("popup_checkbox#3"),
                box("popup_checkbox#4"),
                box("popup_checkbox#5"),
                button("popup_done", "dlg_done")};
    case Screen::RaceRecords:
        // Amateur times, professional times, professional points (the game
        // scores; Reluva doesn't); the three events; the two cities.
        return {Item{Kind::List, "table", {}, 0, true},
                box("popup_checkbox"),
                box("popup_checkbox#2"),
                box("popup_checkbox#3", false),
                box("popup_checkbox#4"),
                box("popup_checkbox#5"),
                box("popup_checkbox#6"),
                box("popup_checkbox#7"),
                box("popup_checkbox#8"),
                button("popup_done", "dlg_done")};
    default:
        return {};
    }
}

bool GameMenus::driverChecked(const std::string& w) const {
    if (screen_ == Screen::NewDriver)
        return w == "checkbox#2" ? newPro_ : !newPro_;
    const size_t hash = w.find('#');
    const size_t n = hash == std::string::npos ? 0 : size_t(std::atoi(w.c_str() + hash + 1)) - 1;
    // The stats' five are the records' last five (events, cities).
    const size_t i = screen_ == Screen::DriverStats ? n + 3 : n;
    return i < filters_.size() && filters_[i];
}

void GameMenus::driverToggle(const std::string& w) {
    if (screen_ == Screen::NewDriver) {
        newPro_ = w == "checkbox#2";
        return;
    }
    const size_t hash = w.find('#');
    const size_t n = hash == std::string::npos ? 0 : size_t(std::atoi(w.c_str() + hash + 1)) - 1;
    const size_t i = screen_ == Screen::DriverStats ? n + 3 : n;
    if (i < filters_.size())
        filters_[i] = !filters_[i];
    tableRow_ = 0;
    buildTable();
}

GameMenus::Action GameMenus::driverActivate(const Item& item) {
    const std::string& w = item.widget;
    if (item.kind == Kind::Check) {
        driverToggle(w);
        return Action::None;
    }
    if (item.kind == Kind::Text) // Enter in the name: done
        return driverActivate(Item{Kind::Button, "popup_done", {}, 4, true});
    auto close = [&] {
        screen_ = Screen::None;
        sys_.setTextInput(false);
    };
    if (screen_ == Screen::NewDriver && w == "popup_done") {
        if (newName_.empty() || !roster_)
            return Action::None;
        if (roster_->has(newName_)) {
            go(Screen::DuplicateDriver); // "The driver name you entered already exists"
            return Action::None;
        }
        roster_->add(newName_, newPro_);
        roster_->save();
        close();
        return Action::DriverChanged;
    }
    if (screen_ == Screen::DuplicateDriver) {
        go(Screen::NewDriver);
        return Action::None;
    }
    if (screen_ == Screen::DeleteDriver && w == "dlg_done" && roster_) {
        roster_->remove(roster_->current());
        roster_->save();
        close();
        return Action::DriverChanged;
    }
    close();
    return Action::Closed;
}

void GameMenus::buildTable() {
    // Each race of the events and cities ticked: the driver's best place
    // and time at each level (the stats); the best time at each level
    // ticked and whose it is, of every driver (the records).
    table_.clear();
    if (!cities_ || !roster_)
        return;
    const bool stats = screen_ == Screen::DriverStats;
    std::vector<std::pair<std::string, PlayerRecord>> records;
    if (!stats)
        for (const DriverEntry& d : roster_->drivers()) {
            PlayerRecord r;
            r.load(roster_->folder(), d.file);
            records.push_back({d.name, std::move(r)});
        }
    char line[160];
    table_.push_back(stats ? "Race                       Amateur          Professional"
                           : "Race                       Amateur              Professional");
    for (const CityInfo& city : *cities_) {
        const bool london = city.map == "london";
        if (!filters_[london ? 7 : 6])
            continue;
        for (int k = 0; k < 3; ++k) {
            if (!filters_[size_t(3 + k)])
                continue;
            const std::vector<std::string> names = raceNames(files_, city.map, kKinds[k]);
            for (size_t i = 0; i < names.size(); ++i) {
                const std::string name = names[i].substr(0, 24);
                if (stats && record_) {
                    std::snprintf(line, sizeof line, "%-26s %-4s %-10s  %-4s %s", name.c_str(),
                                  placeText(record_->bestPlace(city.map, kKinds[k], int(i), false)).c_str(),
                                  timeText(record_->bestTime(city.map, kKinds[k], int(i), false)).c_str(),
                                  placeText(record_->bestPlace(city.map, kKinds[k], int(i), true)).c_str(),
                                  timeText(record_->bestTime(city.map, kKinds[k], int(i), true)).c_str());
                } else {
                    std::string cells[2];
                    for (int pro = 0; pro < 2; ++pro) {
                        if (!filters_[size_t(pro)]) {
                            cells[pro] = "";
                            continue;
                        }
                        float best = -1;
                        std::string who;
                        for (const auto& [driver, r] : records) {
                            const float t = r.bestTime(city.map, kKinds[k], int(i), pro == 1);
                            if (t > 0 && (best < 0 || t < best)) {
                                best = t;
                                who = driver;
                            }
                        }
                        cells[pro] = timeText(best) + (who.empty() ? "" : " " + who.substr(0, 12));
                    }
                    std::snprintf(line, sizeof line, "%-26s %-20s %s", name.c_str(), cells[0].c_str(),
                                  cells[1].c_str());
                }
                table_.push_back(line);
            }
        }
    }
}

void GameMenus::drawTable(Surface& o) const {
    const int x = originX() + kTableX, y = originY() + kTableY;
    if (screen_ == Screen::DriverStats && roster_)
        font::drawText(o, x, y - 30,
                       roster_->driver().name + (roster_->driver().professional ? " (Professional)" : " (Amateur)"),
                       {255, 230, 140});
    if (table_.empty())
        return;
    font::drawText(o, x, y, table_[0], {150, 200, 255});
    const size_t first = tableRow_ >= size_t(kTableRows - 1) ? tableRow_ - size_t(kTableRows - 2) : 0;
    for (size_t i = 1 + first; i < table_.size() && i < 1 + first + size_t(kTableRows - 1); ++i) {
        const int row = int(i - first);
        const bool chosen = i - 1 == tableRow_;
        font::drawText(o, x, y + row * kTableRow, table_[i], chosen ? Color{255, 255, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::age
