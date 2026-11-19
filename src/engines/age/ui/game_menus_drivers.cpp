// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The main menu's driver dialogs: Create a New Driver (jpg/newp_dlg), the
// duplicate name's warning (dupp_dlg), deleting one (delp_dlg, or
// lstp_dlg for the only one), the Driver's Stats (drec_dlg) and the Race
// Records (hoff_dlg), at tune/widget.csv's places in tune/menu.csv's.
// See docs/engines/age-menus.md.

#include "engines/age/ui/game_menus.h"

#include "graphics/font.h"
#include "engines/age/formats/race.h"

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

const RaceKind kKinds[3] = {RaceKind::kBlitz, RaceKind::kCircuit, RaceKind::kCheckpoint};

std::string timeText(float seconds) {
    if (seconds < 0)
        return "-";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(seconds, 60.0f));
    return buf;
}

std::string placeText(int place) {
    static const char *const kPlaces[] = {"-", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
    return kPlaces[std::clamp(place, 0, 8)];
}

} // namespace

std::string GameMenus::driverMenuName() const {
    switch (_screen) {
    case Screen::kNewDriver:
        return "Create a New Driver";
    case Screen::kDuplicateDriver:
        return "dupp_dlg";
    case Screen::kDeleteDriver:
        return "delp_dlg";
    case Screen::kLastDriver:
        return "lastp_dlg";
    case Screen::kDriverStats:
        return "Driver Record";
    case Screen::kRaceRecords:
        return "Race Statistics";
    case Screen::kRaceOptions:
        return "Race Environment";
    case Screen::kCitySelection:
        return "City Selection";
    case Screen::kLocked:
        return "locked_dlg";
    default:
        return {};
    }
}

void GameMenus::driverOrigin(int &x, int &y) const {
    switch (_screen) {
    case Screen::kNewDriver:
        x = 120, y = 75;
        break;
    case Screen::kDuplicateDriver:
    case Screen::kDeleteDriver:
        x = 170, y = 127;
        break;
    case Screen::kLastDriver:
    case Screen::kLocked:
        x = 120, y = 202;
        break;
    case Screen::kRaceOptions: // tune/menu.csv's
        x = 120, y = 52;
        break;
    case Screen::kCitySelection:
        x = 120, y = 75;
        break;
    default:
        x = kBigX, y = kBigY;
        break;
    }
}

const char *GameMenus::driverPicture(Screen s) const {
    switch (s) {
    case Screen::kNewDriver:
        return "newp_dlg";
    case Screen::kDuplicateDriver:
        return "dupp_dlg";
    case Screen::kDeleteDriver:
        return "delp_dlg";
    case Screen::kLastDriver:
        return "lstp_dlg";
    case Screen::kDriverStats:
        return "drec_dlg";
    case Screen::kRaceRecords:
        return "hoff_dlg";
    case Screen::kRaceOptions:
        return "Renv_dlg";
    case Screen::kCitySelection:
        return "cenv_dlg";
    case Screen::kLocked:
        return "lock_dlg";
    default:
        return "";
    }
}

std::vector<GameMenus::Item> GameMenus::driverItems() const {
    if (_screen == Screen::kRaceOptions) {
        // Its time and weather icons (texture/RENV_*: five states), the
        // three densities on its grid, Hide Pedestrians (RENV_HIDE), and its
        // two buttons as widget.csv has them: DONE left, CANCEL right.
        std::vector<Item> list = {Item{Kind::kChoice, "TIME", {}, 0, true},
                                  Item{Kind::kChoice, "WEATHER", {}, 0, true}};
        // "There are no pedestrians in a Circuit Race" (the game's help).
        const bool peds = !_front || _front->choice().kind != RaceKind::kCircuit;
        for (const char *bar : {"TRAFFIC DENSITY", "PEDESTRIAN DENSITY", "COP DENSITY"})
            list.push_back(Item{Kind::kBar, bar, {}, 0, peds || std::string(bar) != "PEDESTRIAN DENSITY"});
        list.push_back(Item{Kind::kCheck, "dlg_chkb", "RENV_HIDE", 5, peds});
        list.push_back(Item{Kind::kButton, "dlg_done", "dlg_done", 4, true});
        list.push_back(Item{Kind::kButton, "dlg_cancel", "dlg_can", 4, true});
        return list;
    }
    if (_screen == Screen::kCitySelection)
        return {Item{Kind::kList, "CITY", {}, 0, true}, Item{Kind::kButton, "dlg_cancel", "dlg_can", 4, true},
                Item{Kind::kButton, "dlg_done", "dlg_done", 4, true}};
    if (_screen == Screen::kLocked)
        return {Item{Kind::kButton, "dlg_done", "dlg_ok", 4, true}};
    auto button = [](const std::string &w, const std::string &pic) { return Item{Kind::kButton, w, pic, 4, true}; };
    auto box = [](const std::string &w, bool enabled = true) { return Item{Kind::kCheck, w, "dlg_chkb", 5, enabled}; };
    switch (_screen) {
    case Screen::kNewDriver:
        // The name, Amateur or Professional (texture/Checkbox), Cancel, Done.
        return {Item{Kind::kText, "Enter New Driver Name", {}, 0, true},
                Item{Kind::kCheck, "checkbox", "Checkbox", 5, true},
                Item{Kind::kCheck, "checkbox#2", "Checkbox", 5, true}, button("popup_cancel", "dlg_can"),
                button("popup_done", "dlg_done")};
    case Screen::kDuplicateDriver:
        return {button("popup_ok", "dlg_ok")};
    case Screen::kDeleteDriver:
        return {button("dlg_done", "dlg_yes"), button("dlg_cancel", "dlg_no")};
    case Screen::kLastDriver:
        return {button("dlg_done", "dlg_ok")};
    case Screen::kDriverStats:
        // Blitz, Circuit, Checkpoint; San Francisco, London.
        return {Item{Kind::kList, "table", {}, 0, true},
                box("popup_checkbox"),
                box("popup_checkbox#2"),
                box("popup_checkbox#3"),
                box("popup_checkbox#4"),
                box("popup_checkbox#5"),
                button("popup_done", "dlg_done")};
    case Screen::kRaceRecords:
        // Amateur times, professional times, professional points (the game
        // scores; Reluva doesn't); the three events; the two cities.
        return {Item{Kind::kList, "table", {}, 0, true},
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
const char *const kTimeIcons[4] = {"RENV_IMORN", "RENV_INOON", "RENV_ISUNS", "RENV_INITE"};
const char *const kWeatherIcons[4] = {"RENV_ISUN", "RENV_IFOG", "RENV_ISNOW", "RENV_IRAIN"};
constexpr int kIconW = 28, kIconH = 21, kIconStep = 31;
} // namespace

void GameMenus::drawConditionIcons(Surface &o, const std::string &w, bool focused) const {
    // Four icons, smaller, in the picture's box for the row: the chosen
    // one lit (the pictures' fourth state), the others dimmed (their
    // fifth), or plain (their first) in the row pointed at.
    const Widget *wd = widget(w);
    if (!wd)
        return;
    const bool time = w == "TIME";
    const int chosen = time ? _conditions.timeOfDay : _conditions.weather;
    for (int i = 0; i < 4; ++i) {
        const image::Image *im = _pictures.texture(time ? kTimeIcons[i] : kWeatherIcons[i]);
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

void GameMenus::stepCondition(const std::string &w, int dir) {
    int &v = w == "TIME" ? _conditions.timeOfDay : _conditions.weather;
    v = (v + dir + 4) % 4;
    sayCondition(w == "TIME");
}

void GameMenus::sayCondition(bool time) {
    // The game's recordings of the times and weathers (aud/aud22/UI*.wav);
    // there's none for fog (and a UIsnow for a weather the game hasn't).
    static const char *const kTimes[] = {"UImorning", "UInoon", "UIsunset", "UInight"};
    static const char *const kWeathers[] = {"UIsunny", "UIcloudy", "", "UIrain"};
    _voice = time ? kTimes[_conditions.timeOfDay & 3] : kWeathers[_conditions.weather & 3];
}

float GameMenus::raceShare(const std::string &w) const {
    if (w == "TRAFFIC DENSITY")
        return _conditions.traffic;
    if (w == "PEDESTRIAN DENSITY")
        return _conditions.peds;
    if (w == "COP DENSITY")
        return std::min(1.0f, float(_conditions.cops) / 4);
    return 0;
}

void GameMenus::setRaceShare(const std::string &w, float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (w == "TRAFFIC DENSITY")
        _conditions.traffic = std::round(v * 10) / 10;
    else if (w == "PEDESTRIAN DENSITY")
        _conditions.peds = std::round(v * 10) / 10;
    else if (w == "COP DENSITY")
        _conditions.cops = int(std::lround(v * 4)); // 0-4 police
}

bool GameMenus::driverChecked(const std::string &w) const {
    if (_screen == Screen::kRaceOptions)
        return w == "dlg_chkb" && _conditions.hidePeds;
    if (_screen == Screen::kNewDriver)
        return w == "checkbox#2" ? _newPro : !_newPro;
    const size_t hash = w.find('#');
    const size_t n = hash == std::string::npos ? 0 : size_t(std::atoi(w.c_str() + hash + 1)) - 1;
    // The stats' five are the records' last five (events, cities).
    const size_t i = _screen == Screen::kDriverStats ? n + 3 : n;
    return i < _filters.size() && _filters[i];
}

void GameMenus::driverToggle(const std::string &w) {
    if (_screen == Screen::kRaceOptions) {
        if (w == "dlg_chkb")
            _conditions.hidePeds = !_conditions.hidePeds;
        return;
    }
    if (_screen == Screen::kNewDriver) {
        _newPro = w == "checkbox#2";
        return;
    }
    const size_t hash = w.find('#');
    const size_t n = hash == std::string::npos ? 0 : size_t(std::atoi(w.c_str() + hash + 1)) - 1;
    const size_t i = _screen == Screen::kDriverStats ? n + 3 : n;
    if (i < _filters.size())
        _filters[i] = !_filters[i];
    _tableRow = 0;
    buildTable();
}

GameMenus::Action GameMenus::driverActivate(const Item &item) {
    const std::string &w = item.widget;
    if (item.kind == Kind::kCheck) {
        driverToggle(w);
        return Action::kNone;
    }
    if (item.kind == Kind::kText) // Enter in the name: done
        return driverActivate(Item{Kind::kButton, "popup_done", {}, 4, true});
    auto close = [&] {
        _screen = Screen::kNone;
        _sys.setTextInput(false);
    };
    if (_screen == Screen::kRaceOptions || _screen == Screen::kCitySelection) {
        if (item.kind == Kind::kList)
            return driverActivate(Item{Kind::kButton, "dlg_done", {}, 4, true});
        if (w == "dlg_done" && _front) {
            if (_screen == Screen::kRaceOptions) {
                _front->choice().conditions = _conditions;
                _front->conditionsChanged();
            } else if (_cities && _cityRow < _cities->size()) {
                _front->choice().city = int(_cityRow);
                _front->refreshChoice();
            }
        }
        close();
        return Action::kClosed;
    }
    if (_screen == Screen::kNewDriver && w == "popup_done") {
        if (_newName.empty() || !_roster)
            return Action::kNone;
        if (_roster->has(_newName)) {
            go(Screen::kDuplicateDriver); // "The driver name you entered already exists"
            return Action::kNone;
        }
        _roster->add(_newName, _newPro);
        _roster->save();
        close();
        return Action::kDriverChanged;
    }
    if (_screen == Screen::kDuplicateDriver) {
        go(Screen::kNewDriver);
        return Action::kNone;
    }
    if (_screen == Screen::kDeleteDriver && w == "dlg_done" && _roster) {
        _roster->remove(_roster->current());
        _roster->save();
        close();
        return Action::kDriverChanged;
    }
    close();
    return Action::kClosed;
}

void GameMenus::buildTable() {
    // Each race of the events and cities ticked: the driver's best place
    // and time at each level (the stats); the best time at each level
    // ticked and whose it is, of every driver (the records).
    _table.clear();
    if (!_cities || !_roster)
        return;
    const bool stats = _screen == Screen::kDriverStats;
    std::vector<std::pair<std::string, PlayerRecord>> records;
    if (!stats)
        for (const DriverEntry &d : _roster->drivers()) {
            PlayerRecord r;
            r.load(_roster->folder(), d.file);
            records.push_back({d.name, std::move(r)});
        }
    char line[160];
    _table.push_back(stats ? "Race                       Amateur          Professional"
                           : "Race                       Amateur              Professional");
    for (const CityInfo &city : *_cities) {
        const bool london = city.map == "london";
        if (!_filters[london ? 7 : 6])
            continue;
        for (int k = 0; k < 3; ++k) {
            if (!_filters[size_t(3 + k)])
                continue;
            const std::vector<std::string> names = raceNames(_files, city.map, kKinds[k]);
            for (size_t i = 0; i < names.size(); ++i) {
                const std::string name = names[i].substr(0, 24);
                if (stats && _record) {
                    std::snprintf(line, sizeof line, "%-26s %-4s %-10s  %-4s %s", name.c_str(),
                                  placeText(_record->bestPlace(city.map, kKinds[k], int(i), false)).c_str(),
                                  timeText(_record->bestTime(city.map, kKinds[k], int(i), false)).c_str(),
                                  placeText(_record->bestPlace(city.map, kKinds[k], int(i), true)).c_str(),
                                  timeText(_record->bestTime(city.map, kKinds[k], int(i), true)).c_str());
                } else {
                    std::string cells[2];
                    for (int pro = 0; pro < 2; ++pro) {
                        if (!_filters[size_t(pro)]) {
                            cells[pro] = "";
                            continue;
                        }
                        float best = -1;
                        std::string who;
                        for (const auto &[driver, r] : records) {
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
                _table.push_back(line);
            }
        }
    }
}

void GameMenus::drawTable(Surface &o) const {
    const int x = originX() + kTableX, y = originY() + kTableY;
    if (_screen == Screen::kDriverStats && _roster)
        font::drawText(o, x, y - 30,
                       _roster->driver().name + (_roster->driver().professional ? " (Professional)" : " (Amateur)"),
                       {255, 230, 140});
    if (_table.empty())
        return;
    font::drawText(o, x, y, _table[0], {150, 200, 255});
    const size_t first = _tableRow >= size_t(kTableRows - 1) ? _tableRow - size_t(kTableRows - 2) : 0;
    for (size_t i = 1 + first; i < _table.size() && i < 1 + first + size_t(kTableRows - 1); ++i) {
        const int row = int(i - first);
        const bool chosen = i - 1 == _tableRow;
        font::drawText(o, x, y + row * kTableRow, _table[i], chosen ? Color{255, 255, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::age
