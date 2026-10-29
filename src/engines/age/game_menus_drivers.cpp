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
    case Screen::RaceOptions:
        return "Race Environment";
    case Screen::CitySelection:
        return "City Selection";
    case Screen::Locked:
        return "locked_dlg";
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
    case Screen::Locked:
        x = 120, y = 202;
        break;
    case Screen::RaceOptions: // tune/menu.csv's
        x = 120, y = 52;
        break;
    case Screen::CitySelection:
        x = 120, y = 75;
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
    case Screen::RaceOptions:
        return "Renv_dlg";
    case Screen::CitySelection:
        return "cenv_dlg";
    case Screen::Locked:
        return "lock_dlg";
    default:
        return "";
    }
}

std::vector<GameMenus::Item> GameMenus::driverItems() const {
    if (screen_ == Screen::RaceOptions) {
        // Its time and weather icons (texture/RENV_*: five states), the
        // three densities on its grid, Hide Pedestrians (RENV_HIDE), and its
        // two buttons as widget.csv has them: DONE left, CANCEL right.
        std::vector<Item> list = {Item{Kind::Choice, "TIME", {}, 0, true}, Item{Kind::Choice, "WEATHER", {}, 0, true}};
        for (const char* bar : {"TRAFFIC DENSITY", "PEDESTRIAN DENSITY", "COP DENSITY"})
            list.push_back(Item{Kind::Bar, bar, {}, 0, true});
        list.push_back(Item{Kind::Check, "dlg_chkb", "RENV_HIDE", 5, true});
        list.push_back(Item{Kind::Button, "dlg_done", "dlg_done", 4, true});
        list.push_back(Item{Kind::Button, "dlg_cancel", "dlg_can", 4, true});
        return list;
    }
    if (screen_ == Screen::CitySelection)
        return {Item{Kind::List, "CITY", {}, 0, true}, Item{Kind::Button, "dlg_cancel", "dlg_can", 4, true},
                Item{Kind::Button, "dlg_done", "dlg_done", 4, true}};
    if (screen_ == Screen::Locked)
        return {Item{Kind::Button, "dlg_done", "dlg_ok", 4, true}};
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

namespace {
// The icons (texture/RENV_*, five states 31 high): the times morning,
// noon, sunset, night; the weathers as their pictures show them, in the
// lightings' order (clear, cloudy, foggy, rainy): a sun, a cloud, a sun
// behind a cloud, rain. Their names say sun, fog, snow and rain.
const char* const kTimeIcons[4] = {"RENV_IMORN", "RENV_INOON", "RENV_ISUNS", "RENV_INITE"};
const char* const kWeatherIcons[4] = {"RENV_ISUN", "RENV_IFOG", "RENV_ISNOW", "RENV_IRAIN"};
constexpr int kIconW = 28, kIconH = 21, kIconStep = 31;
} // namespace

void GameMenus::drawConditionIcons(Surface& o, const std::string& w, bool focused) const {
    // Four icons, smaller, in the picture's box for the row: the chosen
    // one lit (the pictures' fourth state), the others dimmed (their
    // fifth), or plain (their first) in the row pointed at.
    const Widget* wd = widget(w);
    if (!wd)
        return;
    const bool time = w == "TIME";
    const int chosen = time ? conditions_.timeOfDay : conditions_.weather;
    for (int i = 0; i < 4; ++i) {
        const image::Image* im = pictures_.texture(time ? kTimeIcons[i] : kWeatherIcons[i]);
        if (!im)
            continue;
        const int states = 5, h = im->h / states, state = i == chosen ? 3 : focused ? 0 : 4;
        image::Image band;
        band.w = im->w;
        band.h = h;
        const size_t row = size_t(im->w) * 4;
        band.rgba.assign(im->rgba.begin() + std::ptrdiff_t(size_t(state * h) * row),
                         im->rgba.begin() + std::ptrdiff_t(size_t((state + 1) * h) * row));
        drawPicture(o, band, originX() + wd->x + 4 + i * kIconStep, originY() + wd->y + 3, kIconW, kIconH);
    }
}

void GameMenus::stepCondition(const std::string& w, int dir) {
    int& v = w == "TIME" ? conditions_.timeOfDay : conditions_.weather;
    v = (v + dir + 4) % 4;
}

float GameMenus::raceShare(const std::string& w) const {
    if (w == "TRAFFIC DENSITY")
        return conditions_.traffic;
    if (w == "PEDESTRIAN DENSITY")
        return conditions_.peds;
    if (w == "COP DENSITY")
        return std::min(1.0f, float(conditions_.cops) / 4);
    return 0;
}

void GameMenus::setRaceShare(const std::string& w, float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (w == "TRAFFIC DENSITY")
        conditions_.traffic = std::round(v * 10) / 10;
    else if (w == "PEDESTRIAN DENSITY")
        conditions_.peds = std::round(v * 10) / 10;
    else if (w == "COP DENSITY")
        conditions_.cops = int(std::lround(v * 4)); // 0-4 police
}

bool GameMenus::driverChecked(const std::string& w) const {
    if (screen_ == Screen::RaceOptions)
        return w == "dlg_chkb" && conditions_.hidePeds;
    if (screen_ == Screen::NewDriver)
        return w == "checkbox#2" ? newPro_ : !newPro_;
    const size_t hash = w.find('#');
    const size_t n = hash == std::string::npos ? 0 : size_t(std::atoi(w.c_str() + hash + 1)) - 1;
    // The stats' five are the records' last five (events, cities).
    const size_t i = screen_ == Screen::DriverStats ? n + 3 : n;
    return i < filters_.size() && filters_[i];
}

void GameMenus::driverToggle(const std::string& w) {
    if (screen_ == Screen::RaceOptions) {
        if (w == "dlg_chkb")
            conditions_.hidePeds = !conditions_.hidePeds;
        return;
    }
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
    if (screen_ == Screen::RaceOptions || screen_ == Screen::CitySelection) {
        if (item.kind == Kind::List)
            return driverActivate(Item{Kind::Button, "dlg_done", {}, 4, true});
        if (w == "dlg_done" && front_) {
            if (screen_ == Screen::RaceOptions) {
                front_->choice().conditions = conditions_;
                front_->conditionsChanged();
            } else if (cities_ && cityRow_ < cities_->size()) {
                front_->choice().city = int(cityRow_);
                front_->refreshChoice();
            }
        }
        close();
        return Action::Closed;
    }
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
