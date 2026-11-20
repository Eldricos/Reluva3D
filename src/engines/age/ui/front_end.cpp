// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's menus, redrawn: tune/widget.csv places each control on its
// menu's jpg/ background; the buttons are texture/ pictures of their
// states. What each screen shows and does is Reluva's reading of the
// pictures; see docs/engines/age/games/mm2/systems/menus.md.

#include "engines/age/ui/front_end.h"

#include "graphics/font.h"
#include "engines/age/formats/texture.h"
#include "engines/age/ui/screen.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace reluva::age {

namespace {

std::vector<std::string> splitLine(const std::string &line) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        const size_t comma = line.find(',', at);
        out.push_back(line.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (comma == std::string::npos)
            return out;
        at = comma + 1;
    }
}

std::string lowerCase(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The curriculum's lines on the Crash Course backgrounds (the tops of
// lessons 1-13 in widget order: three lessons, a midterm...), and its
// PASS column's middle.
constexpr int kCurriculumRows[13] = {99, 113, 127, 155, 183, 197, 211, 239, 267, 281, 294, 323, 351};
constexpr int kPassColumn = 200;
// The garage's window, where the game shows the car turning (measured
// on jpg/veh_bk).
constexpr FrontEnd::Rect kCarWindow{30, 48, 580, 190};
// The box in each screen's heading, where the Crash Course shows its city.
constexpr FrontEnd::Rect kCityBox{453, 10, 164, 24};

const char *kindStem(RaceKind k) {
    switch (k) {
    case RaceKind::kBlitz:
        return "blitz";
    case RaceKind::kCircuit:
        return "circuit";
    case RaceKind::kCheckpoint:
        return "race";
    case RaceKind::kCrashCourse:
        return "crash";
    case RaceKind::kCruise:
        return "roam";
    }
    return "race";
}

} // namespace

Error parseWidgets(const std::string &csv, std::vector<Widget> &out) {
    // MENU NAME, ID, WIDGET NAME, ID, X, Y, W, H, DESC.
    out.clear();
    size_t at = 0;
    bool header = true;
    while (at < csv.size()) {
        size_t eol = csv.find('\n', at);
        if (eol == std::string::npos)
            eol = csv.size();
        std::string line = csv.substr(at, eol - at);
        at = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (header || line.empty()) {
            header = false;
            continue;
        }
        const auto f = splitLine(line);
        if (f.size() < 8)
            continue;
        Widget w;
        w.menu = f[0];
        w.name = f[2];
        w.x = std::atoi(f[4].c_str());
        w.y = std::atoi(f[5].c_str());
        w.w = std::atoi(f[6].c_str());
        w.h = std::atoi(f[7].c_str());
        w.desc = f.size() > 8 ? f[8] : "";
        out.push_back(w);
    }
    if (out.empty())
        return Error::fail("no widgets");
    return Error::success();
}

const image::Image *MenuPictures::jpg(const std::string &name) {
    const std::string key = "jpg/" + lowerCase(name);
    auto it = _cache.find(key);
    if (it == _cache.end()) {
        std::unique_ptr<image::Image> im;
        std::vector<uint8_t> data;
        for (const char *ext : {".jpg", ".JPG"})
            if (!_files.read("jpg/" + name + ext, data)) {
                auto decoded = std::make_unique<image::Image>();
                if (!image::decode(data, *decoded))
                    im = std::move(decoded);
                break;
            }
        it = _cache.emplace(key, std::move(im)).first;
    }
    return it->second.get();
}

const image::Image *MenuPictures::texture(const std::string &name) {
    const std::string key = "tex/" + lowerCase(name);
    auto it = _cache.find(key);
    if (it == _cache.end()) {
        std::unique_ptr<image::Image> im;
        Tex tex;
        if (!loadTexture(_files, name, tex))
            im = std::make_unique<image::Image>(std::move(tex.image));
        it = _cache.emplace(key, std::move(im)).first;
    }
    return it->second.get();
}

void drawPicture(Surface &overlay, const image::Image &im, int x, int y, int w, int h) {
    w = w > 0 ? w : im.w;
    h = h > 0 ? h : im.h;
    if (im.w <= 0 || im.h <= 0)
        return;
    for (int yy = std::max(0, y); yy < std::min(overlay.h, y + h); ++yy) {
        uint8_t *row = overlay.row(yy);
        const int sy = std::min(im.h - 1, (yy - y) * im.h / h);
        for (int xx = std::max(0, x); xx < std::min(overlay.w, x + w); ++xx) {
            const uint8_t *s = im.pixel(std::min(im.w - 1, (xx - x) * im.w / w), sy);
            uint8_t *d = row + xx * 4;
            const float a = float(s[3]) / 255;
            const float da = float(d[3]) / 255;
            const float out = a + da * (1 - a);
            if (out <= 0)
                continue;
            for (int k = 0; k < 3; ++k)
                d[k] = uint8_t(std::lround((float(s[k]) * a + float(d[k]) * da * (1 - a)) / out));
            d[3] = uint8_t(std::lround(out * 255));
        }
    }
}

void drawButtonState(Surface &overlay, const image::Image &im, int x, int y, int states, int state) {
    // One band of the stack: copied out and drawn.
    const int h = im.h / std::max(1, states);
    if (h <= 0)
        return;
    image::Image band;
    band.w = im.w;
    band.h = h;
    const size_t rowBytes = size_t(im.w) * 4;
    const size_t from = size_t(std::clamp(state, 0, states - 1)) * size_t(h) * rowBytes;
    band.rgba.assign(im.rgba.begin() + std::ptrdiff_t(from),
                     im.rgba.begin() + std::ptrdiff_t(from + size_t(h) * rowBytes));
    drawPicture(overlay, band, x, y);
}

FrontEnd::FrontEnd(const SearchSet &files, const std::vector<CityInfo> &cities, const std::vector<CarInfo> &cars,
                   const GameStrings &strings, const PlayerRecord &record)
    : _files(files), _cities(cities), _cars(cars), _strings(strings), _record(record), _pictures(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        _ok = !parseWidgets(std::string(data.begin(), data.end()), _widgets) && _pictures.jpg("main_bk") &&
              !cities.empty() && !cars.empty();
    // The race menu's CHANGE (texture/race_chg, 25 x 122 a state) isn't in
    // widget.csv: up the right of the time, weather and densities.
    _widgets.push_back({"Single Race Menu", "race_chg", 612, 247, 25, 122, "5 State"});
    // The garage's bars: each car's .info figures against the most.
    for (const CarInfo &car : cars) {
        std::array<float, 4> f{};
        if (!files.read("tune/" + car.base + ".info", data)) {
            const std::string text(data.begin(), data.end());
            const char *const keys[4] = {"Horsepower=", "Top Speed=", "Durability=", "Mass="};
            for (int k = 0; k < 4; ++k)
                if (const size_t at = text.find(keys[k]); at != std::string::npos)
                    f[size_t(k)] = std::strtof(text.c_str() + at + std::strlen(keys[k]), nullptr);
        }
        for (size_t k = 0; k < 4; ++k)
            _most[k] = std::max(_most[k], f[k]);
        _figures.push_back(f);
    }
    refresh();
}

const Widget *FrontEnd::widget(const std::string &name) const {
    static const char *const kMenus[] = {"Main Menu", "Single Race Menu",   "Crash Course",    "Garage Menu",
                                         "",          "Crash Course Intro", "Vehicle Showcase"};
    const std::string menu = kMenus[int(_screen)];
    for (const Widget &w : _widgets)
        if (w.menu == menu && w.name == name)
            return &w;
    // The navigation bar, top right of every screen.
    for (const Widget &w : _widgets)
        if (w.menu == "Navigation Bar" && w.name == name)
            return &w;
    return nullptr;
}

bool FrontEnd::raceOpen(int index) const {
    if (openAll || _cities.empty())
        return true;
    const std::string &map = _cities[size_t(_choice.city)].map;
    const RaceUnlocking u = raceUnlocking(_files, map);
    return age::raceOpen(_record, map, _choice.kind, index, _choice.professional, u.group, u.mustPlace);
}

bool FrontEnd::carOpen(int car) const {
    return paintOpen(car, -1);
}

bool FrontEnd::paintOpen(int car, int paintJob) const {
    if (openAll || car < 0 || size_t(car) >= _cars.size())
        return true;
    const SearchSet &files = _files;
    return unlocked(
        _record, _cars[size_t(car)].base, paintJob, _choice.professional,
        [&files](const std::string &city, RaceKind kind) { return int(raceNames(files, city, kind).size()); });
}

void FrontEnd::refresh() {
    if (_cities.empty())
        return;
    const std::string &map = _cities[size_t(_choice.city)].map;
    _raceNames = raceNames(_files, map, _choice.kind);
    _choice.race = std::clamp(_choice.race, 0, std::max(0, int(_raceNames.size()) - 1));
    if (!_raceNames.empty())
        loadRace(_files, map, _choice.kind, _choice.race, _race);
    // Another race (or level): its own conditions again.
    const std::string key = map + " " + std::to_string(int(_choice.kind)) + " " + std::to_string(_choice.race) +
                            (_choice.professional ? " p" : " a");
    if (key != _conditionsFor) {
        _conditionsFor = key;
        _choice.conditions = raceConditions();
        _choice.changed = false;
    }
    if (!_cars.empty())
        _paintJobs = std::max(1, int(_cars[size_t(_choice.car)].colors.size()));
    _choice.paintJob = std::clamp(_choice.paintJob, 0, _paintJobs - 1);
}

FrontEnd::Choice::Conditions FrontEnd::raceConditions() const {
    Choice::Conditions c;
    if (_raceNames.empty())
        return c;
    const RaceSettings &s = _race.settings[_choice.professional ? 1 : 0];
    c.laps = std::max(1, s.laps);
    c.opponents = std::min(std::max(0, s.opponents), mostOpponents());
    c.timeOfDay = std::clamp(s.timeOfDay, 0, 3);
    c.weather = std::clamp(s.weather, 0, 3);
    c.cops = std::max(0, s.cops);
    c.traffic = s.ambient;
    c.peds = s.peds;
    return c;
}

bool FrontEnd::conditionsOpen() const {
    // The game's help: a race's conditions (and a circuit's laps and
    // opponents) open once it's been placed in, 1st-3rd as an Amateur,
    // 1st as a Professional; a cruise's always.
    if (_choice.kind == RaceKind::kCruise || openAll)
        return true;
    if (_choice.kind == RaceKind::kCrashCourse || _cities.empty())
        return false;
    const std::string &map = _cities[size_t(_choice.city)].map;
    const int pro = _record.bestPlace(map, _choice.kind, _choice.race, true);
    if (_choice.professional)
        return pro == 1;
    const int amateur = _record.bestPlace(map, _choice.kind, _choice.race, false);
    const int best = amateur > 0 && pro > 0 ? std::min(amateur, pro) : std::max(amateur, pro);
    return best >= 1 && best <= 3;
}

int FrontEnd::mostOpponents() const {
    return int(_race.opponents[_choice.professional ? 1 : 0].size());
}

std::vector<FrontEnd::Item> FrontEnd::items() const {
    std::vector<Item> list = screenItems();
    // The navigation bar: Options, Help (the game's help file, when it's
    // in the game's folder), Minimise and Exit.
    if (_screen != Screen::kSplash) {
        list.push_back({"mnav_opt", true, true, "MNAV_OPT", 5});
        list.push_back({"mnav_help", true, _files.has("MM2HELP.HLP"), "mnav_hlp", 3});
        list.push_back({"mnav_stow", true, true, "mnav_sto", 3});
        list.push_back({"mnav_exit", true, true, "mnav_ext", 3});
    }
    return list;
}

std::vector<FrontEnd::Item> FrontEnd::screenItems() const {
    switch (_screen) {
    case Screen::kMain:
        return {{"dvrcc", true},
                {"main_sp", true},
                {"main_mp", true, multiplayer},
                {"main_qck", true, quickRace()},
                {"DRIVER NAME", false},
                {"dvrnew", true, _roster != nullptr, "DVRNEW", 4},
                {"dvrdel", true, _roster != nullptr, "DVRDEL", 4},
                {"dvrsts", true, _roster != nullptr, "DVRSTS", 4},
                // RACE RECORDS and INSTANT REPLAY overlap where widget.csv
                // puts them: the replay's drawn last, over the records'.
                {"race_rec", true, _roster != nullptr, "RACE_REC", 4},
                {"main_rpl", true, true, "main_rpl", 4}};
    case Screen::kRaces:
        // LAPS (a circuit's) and OPPONENTS change with Left/Right; CHANGE
        // (texture/race_chg) the rest.
        return {{"cruise", true},
                {"blitz", true},
                {"cp", true},
                {"circuit", true},
                {"RACE NAME", false},
                {"LAPS", false, _choice.kind == RaceKind::kCircuit && conditionsOpen()},
                {"OPPONENTS", false, mostOpponents() > 0 && conditionsOpen()},
                {"RACE LOCALE", false},
                {"race_chg", true, !_raceNames.empty() && conditionsOpen(), "race_chg", 5},
                {"race_veh", true}};
    case Screen::kCrashCourse:
        return {{"city", false}, {"CRASH NAME", false}, {"host_dn", true}};
    case Screen::kGarage:
        return {{"VEHICLES", false},
                {"CAR COLOR", false},
                {"TRANSMISSION", false},
                {"veh_show", true, true, "veh_show", 4},
                {"vesel_godrive", true}};
    case Screen::kCrashIntro:
        // widget.csv's row for its BACK is garbled: the navigation bar's.
        return {{"cci_lon", true, true, "cci_lon", 4},
                {"cci_sf", true, true, "cci_sf", 4},
                {"mnav_prev", true, true, "mnav_prv", 4}};
    case Screen::kShowcase:
        return {{"opt_done", true, true, "opt_done", 4}};
    case Screen::kSplash:
        return {};
    }
    return {};
}

void FrontEnd::go(Screen s) {
    _screen = s;
    _focus = 0;
    if (s == Screen::kCrashCourse)
        _choice.kind = RaceKind::kCrashCourse;
    else if (s == Screen::kRaces && _choice.kind == RaceKind::kCrashCourse)
        _choice.kind = RaceKind::kBlitz;
    refresh();
}

void FrontEnd::activate(const Item &item, Action &action) {
    if (!item.enabled)
        return;
    const std::string &w = item.widget;
    if (w == "dvrcc")
        go(Screen::kCrashIntro);
    else if (w == "cci_lon" || w == "cci_sf") {
        // London's cab company or San Francisco's stunt school.
        for (size_t i = 0; i < _cities.size(); ++i)
            if ((_cities[i].map == "london") == (w == "cci_lon"))
                _choice.city = int(i);
        go(Screen::kCrashCourse);
    } else if (w == "mnav_prev" && _screen == Screen::kCrashIntro)
        go(Screen::kMain);
    else if (w == "veh_show")
        go(Screen::kShowcase);
    else if (w == "opt_done" && _screen == Screen::kShowcase) {
        _screen = Screen::kGarage;
        _focus = 3;
    } else if (w == "main_sp")
        go(Screen::kRaces);
    else if (w == "main_mp")
        action = Action::kMultiplayer;
    else if (w == "mnav_help")
        action = Action::kHelp;
    else if (w == "mnav_stow")
        action = Action::kMinimise;
    else if (w == "mnav_opt")
        action = Action::kOptions;
    else if (w == "main_rpl")
        action = Action::kReplays;
    else if (w == "dvrnew")
        action = Action::kNewDriver;
    else if (w == "dvrdel")
        action = Action::kDeleteDriver;
    else if (w == "dvrsts")
        action = Action::kDriverStats;
    else if (w == "race_rec")
        action = Action::kRaceRecords;
    else if (w == "main_qck") {
        // Quick Race: "Choose your vehicle and you're off! Quick Race starts
        // you in the last race you ran."
        std::string map;
        RaceKind kind = RaceKind::kBlitz;
        int index = 0;
        if (!_record.lastRace(map, kind, index))
            return;
        for (size_t i = 0; i < _cities.size(); ++i)
            if (_cities[i].map == map)
                _choice.city = int(i);
        _choice.kind = kind;
        _choice.race = index;
        refresh();
        _afterGarage = Screen::kMain;
        _screen = Screen::kGarage;
        _focus = 0;
        action = Action::kChanged;
    } else if (w == "mnav_exit")
        action = Action::kQuit;
    else if (w == "cruise" || w == "blitz" || w == "cp" || w == "circuit") {
        _choice.kind = w == "cruise"  ? RaceKind::kCruise
                       : w == "blitz" ? RaceKind::kBlitz
                       : w == "cp"    ? RaceKind::kCheckpoint
                                      : RaceKind::kCircuit;
        refresh();
        action = Action::kChanged;
    } else if (w == "race_veh" || w == "host_dn") {
        if (_raceNames.empty() || !raceOpen(_choice.race))
            return;
        _afterGarage = _screen;
        _screen = Screen::kGarage;
        _focus = 0;
        action = Action::kChanged;
    } else if (w == "vesel_godrive") {
        action = carOpen(_choice.car) && paintOpen(_choice.car, _choice.paintJob) ? Action::kStart : Action::kLocked;
    } else if (w == "race_chg") {
        action = Action::kRaceOptions;
    }
}

void FrontEnd::change(const Item &item, int step) {
    auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
    const std::string &w = item.widget;
    if (w == "RACE NAME" || w == "CRASH NAME") {
        _choice.race = wrap(_choice.race + step, int(_raceNames.size()));
    } else if (w == "LAPS") {
        _choice.conditions.laps = std::clamp(_choice.conditions.laps + step, 1, 20);
        conditionsChanged();
        return;
    } else if (w == "OPPONENTS") {
        _choice.conditions.opponents = std::clamp(_choice.conditions.opponents + step, 0, mostOpponents());
        conditionsChanged();
        return;
    } else if (w == "RACE LOCALE" || w == "city") {
        _choice.city = wrap(_choice.city + step, int(_cities.size()));
    } else if (w == "DRIVER NAME") {
        // The next driver (their level comes with them).
        if (_roster && !_roster->drivers().empty()) {
            _roster->setCurrent((_roster->current() + step + int(_roster->drivers().size())) %
                                int(_roster->drivers().size()));
            _roster->save();
            _choice.professional = _roster->driver().professional;
            _driverChanged = true;
        } else
            _choice.professional = !_choice.professional;
    } else if (w == "VEHICLES") {
        _choice.car = wrap(_choice.car + step, int(_cars.size()));
        _choice.paintJob = 0;
    } else if (w == "CAR COLOR") {
        _choice.paintJob = wrap(_choice.paintJob + step, _paintJobs);
    } else if (w == "TRANSMISSION") {
        _choice.manual = !_choice.manual;
    }
    refresh();
}

bool FrontEnd::hit(const Item &item, int x, int y) {
    if (item.widget == "city")
        return x >= kCityBox.x && x < kCityBox.x + kCityBox.w && y >= kCityBox.y && y < kCityBox.y + kCityBox.h;
    const Widget *w = widget(item.widget);
    if (!w)
        return false;
    int width = w->w;
    int height = w->h;
    if (item.button)
        if (const image::Image *im = _pictures.texture(item.pictureName())) {
            const int states = statesOf(item, *w);
            width = im->w;
            height = im->h / states;
        }
    return x >= w->x && x < w->x + width && y >= w->y && y < w->y + height;
}

FrontEnd::Action FrontEnd::handle(const Event &ev) {
    Action action = Action::kNone;
    if (_screen == Screen::kSplash) {
        // Any key or click goes on to the main menu.
        if (ev.type == EventType::kKeyDown || (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft)) {
            go(Screen::kMain);
            action = Action::kChanged;
        }
        return action;
    }
    const auto list = items();
    if (list.empty())
        return action;
    _focus = std::clamp(_focus, 0, int(list.size()) - 1);
    if (ev.type == EventType::kMouseMove) {
        // Pointed at: focused (and its word said), as the game's are.
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].enabled && hit(list[i], ev.mouseX, ev.mouseY)) {
                _focus = int(i);
                break;
            }
        return action;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        for (size_t i = 0; i < list.size(); ++i)
            if (hit(list[i], ev.mouseX, ev.mouseY)) {
                _focus = int(i);
                if (list[i].button)
                    activate(list[i], action);
                else {
                    // A field: its right half steps on, its left back.
                    const Widget *w = widget(list[i].widget);
                    change(list[i], w && ev.mouseX < w->x + w->w / 2 ? -1 : 1); // no widget (the city's box): on
                    if (_driverChanged) {
                        _driverChanged = false;
                        return Action::kDriverChanged;
                    }
                    action = Action::kChanged;
                }
                return action;
            }
        return action;
    }
    if (ev.type != EventType::kKeyDown)
        return action;
    const Item &item = list[size_t(_focus)];
    switch (ev.key) {
    case key::kUp:
    case key::kDown: {
        const int step = ev.key == key::kDown ? 1 : -1;
        for (size_t tries = 0; tries < list.size(); ++tries) {
            _focus = (_focus + step + int(list.size())) % int(list.size());
            if (list[size_t(_focus)].enabled)
                break;
        }
        break;
    }
    case key::kLeft:
    case key::kRight:
        if (!item.button) {
            change(item, ev.key == key::kRight ? 1 : -1);
            if (_driverChanged) {
                _driverChanged = false;
                return Action::kDriverChanged;
            }
            action = Action::kChanged;
        }
        break;
    case key::kReturn:
    case key::kKpEnter:
        if (item.button)
            activate(item, action);
        else if (_screen == Screen::kRaces && item.widget == "RACE LOCALE")
            action = Action::kCityChoice; // the game's Race Locales
        else if (_screen == Screen::kRaces)
            activate({"race_veh", true}, action);
        else if (_screen == Screen::kCrashCourse)
            activate({"host_dn", true}, action);
        else if (_screen == Screen::kGarage)
            activate({"vesel_godrive", true}, action);
        break;
    case key::kEscape:
        if (_screen == Screen::kGarage)
            go(_afterGarage);
        else if (_screen == Screen::kShowcase) {
            _screen = Screen::kGarage;
            _focus = 3;
        } else if (_screen == Screen::kCrashCourse)
            go(Screen::kCrashIntro);
        else if (_screen != Screen::kMain)
            go(Screen::kMain);
        else {
            action = Action::kQuit; // "Quit?"
            break;
        }
        action = Action::kChanged;
        break;
    case 'u':
        openAll = !openAll;
        break;
    default:
        break;
    }
    return action;
}

void FrontEnd::text(Surface &o, const std::string &name, const std::string &s, Color c) const {
    if (const Widget *w = widget(name))
        font::drawText(o, w->x + 6, w->y + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void FrontEnd::bar(Surface &o, const std::string &name, float share) const {
    const Widget *w = widget(name);
    if (!w)
        return;
    // The garage's (6 high) sit on the grid beside their labels: 18
    // pixels lower than widget.csv says, from x 148, 130 wide (measured on
    // jpg/veh_bk).
    const bool garage = w->h < 10;
    const int x0 = garage ? 148 : w->x;
    const int y0 = w->y + (garage ? 18 : 0);
    const int width = garage ? 130 : w->w;
    const int lit = int(std::clamp(share, 0.0f, 1.0f) * float(width));
    for (int y = y0; y < y0 + std::max(w->h, 4) && y < o.h; ++y) {
        uint8_t *p = o.row(y) + x0 * 4;
        for (int x = 0; x < width && x0 + x < o.w; ++x, p += 4) {
            p[0] = x < lit ? 255 : 40;
            p[1] = x < lit ? 200 : 40;
            p[2] = x < lit ? 40 : 60;
            p[3] = 255;
        }
    }
}

FrontEnd::Rect FrontEnd::carWindow() const {
    return _screen == Screen::kGarage ? kCarWindow : _screen == Screen::kShowcase ? Rect{0, 0, 640, 480} : Rect{};
}

void FrontEnd::draw(Surface &o) {
    std::fill(o.pixels.begin(), o.pixels.end(), 0);
    if (_cities.empty() || _cars.empty())
        return;
    const std::string &map = _cities[size_t(_choice.city)].map;
    const std::string city = map == "london" ? "london" : "sf";
    const char *background = "main_bk";
    switch (_screen) {
    case Screen::kMain:
        background = "main_bk";
        break;
    case Screen::kRaces:
        background = "race_bk";
        break;
    case Screen::kCrashCourse:
        background = map == "london" ? "cclon_bk" : "ccsf_bk";
        break;
    case Screen::kGarage:
        background = "veh_bk";
        break;
    case Screen::kSplash:
        background = "splash";
        break;
    case Screen::kCrashIntro:
        background = "ilon_bk";
        break;
    case Screen::kShowcase:
        background = "";
        break; // the car, the whole screen
    }
    if (_screen == Screen::kSplash) {
        if (const image::Image *bg = _pictures.jpg(background))
            drawPicture(o, *bg, 0, 0, o.w, o.h);
        return;
    }
    if (const image::Image *bg = _pictures.jpg(background))
        drawPicture(o, *bg, 0, 0, o.w, o.h);

    // The buttons, each in its state: 0 normal, 1 focused (and for the
    // five-state ones, 2 chosen); the last, not available.
    const auto list = items();
    for (size_t i = 0; i < list.size(); ++i) {
        const Item &item = list[i];
        const Widget *w = widget(item.widget);
        if (!w)
            continue;
        const bool focused = int(i) == _focus;
        if (item.button) {
            const image::Image *im = _pictures.texture(item.pictureName());
            if (!im)
                continue;
            const int states = statesOf(item, *w);
            bool chosen = (item.widget == "cruise" && _choice.kind == RaceKind::kCruise) ||
                          (item.widget == "blitz" && _choice.kind == RaceKind::kBlitz) ||
                          (item.widget == "cp" && _choice.kind == RaceKind::kCheckpoint) ||
                          (item.widget == "circuit" && _choice.kind == RaceKind::kCircuit);
            const int state = !item.enabled ? states - 1 : states == 5 && chosen ? 2 : focused ? 1 : 0;
            drawButtonState(o, *im, w->x, w->y, states, state);
        } else if (focused) {
            // A field: a marker at its left.
            font::drawText(o, w->x - 10, w->y + 4, ">", {255, 255, 120});
        }
    }

    // The description box: the focused button's picture (jpg/mn_*), or
    // the race's or lesson's.
    std::string desc;
    if (_screen == Screen::kMain) {
        static const std::map<std::string, std::string> kDesc{
            {"dvrcc", "mn_cc"},     {"main_sp", "mn_sp"},     {"main_mp", "mn_mp"}, {"main_qck", "mn_qck"},
            {"dvrnew", "mn_new"},   {"dvrdel", "mn_del"},     {"dvrsts", "mn_sts"}, {"race_rec", "mn_rec"},
            {"main_rpl", "mn_rpl"}, {"DRIVER NAME", "mn_drv"}};
        if (auto it = kDesc.find(list[size_t(_focus)].widget); it != kDesc.end())
            desc = it->second;
        // The driver, and every driver in the box below (the chosen one lit).
        const std::string name = _roster ? _roster->driver().name : std::string("Player");
        text(o, "DRIVER NAME", name + " (" + (_choice.professional ? "Professional" : "Amateur") + ")");
        if (_roster)
            for (size_t i = 0; i < _roster->drivers().size() && i < 14; ++i) {
                const DriverEntry &d = _roster->drivers()[i];
                font::drawText(o, 184, 186 + int(i) * 14, d.name + (d.professional ? " (Pro)" : ""),
                               int(i) == _roster->current() ? Color{255, 255, 120} : Color{200, 200, 255});
            }
    } else if (_screen == Screen::kRaces) {
        desc = _choice.kind == RaceKind::kBlitz     ? "race_btz"
               : _choice.kind == RaceKind::kCircuit ? "race_cir"
               : _choice.kind == RaceKind::kCruise  ? "race_rom"
                                                    : "race_cp";
        // The race's map (a cruise's, the whole city's), its name and its
        // numbers.
        const std::string mapPic = city + "_map" +
                                   (_choice.kind == RaceKind::kCheckpoint ? "race" : kindStem(_choice.kind)) +
                                   (_choice.kind == RaceKind::kCruise ? std::string() : std::to_string(_choice.race));
        if (const image::Image *m = _pictures.jpg(mapPic))
            if (const Widget *w = widget("race map"))
                drawPicture(o, *m, w->x, w->y, w->w, w->h);
        std::string name = _raceNames.empty() ? "" : _raceNames[size_t(_choice.race)];
        if (!raceOpen(_choice.race))
            name += " (locked)";
        text(o, "RACE NAME", name);
        if (!_raceNames.empty()) {
            const Choice::Conditions &c = _choice.conditions;
            text(o, "LAPS", _choice.kind == RaceKind::kCircuit ? std::to_string(c.laps) : "-");
            text(o, "OPPONENTS", std::to_string(c.opponents));
            // mmlang.dll's words: Morning, Noon, Sunset, Night; Sunny,
            // Cloudy, Foggy, Raining.
            static const char *const kTimes[] = {"Morning", "Noon", "Sunset", "Night"};
            static const char *const kWeathers[] = {"Sunny", "Cloudy", "Foggy", "Raining"};
            text(o, "TIME", _strings.get(357 + uint32_t(c.timeOfDay & 3), kTimes[c.timeOfDay & 3]));
            text(o, "WEATHER", _strings.get(361 + uint32_t(c.weather & 3), kWeathers[c.weather & 3]));
            bar(o, "PEDESTRIAN DENSITY", c.hidePeds ? 0 : c.peds);
            bar(o, "TRAFFIC DENSITY", c.traffic);
            bar(o, "COP DENSITY", std::min(1.0f, float(c.cops) / 4));
            if (_choice.changed && _choice.kind != RaceKind::kCruise) // as the game's records say: not recorded
                font::drawText(o, 404, 186, "Changed: not recorded", {255, 160, 120});
            const int best = _record.bestPlace(map, _choice.kind, _choice.race, _choice.professional);
            if (best > 0)
                font::drawText(o, 404, 170,
                               "Best: " + std::to_string(best) +
                                   (best == 1   ? "st"
                                    : best == 2 ? "nd"
                                    : best == 3 ? "rd"
                                                : "th"),
                               {150, 255, 150});
        }
        text(o, "RACE LOCALE", _cities[size_t(_choice.city)].name);
    } else if (_screen == Screen::kCrashCourse) {
        desc = (map == "london" ? "lon_cc" : "sf_cc") + std::to_string(_choice.race);
        std::string name = _raceNames.empty() ? "" : _raceNames[size_t(_choice.race)];
        if (!raceOpen(_choice.race))
            name += " (locked)";
        text(o, "CRASH NAME", name);
        // The lessons passed, ticked on the curriculum (texture/cc_smchk:
        // blank, a tick, a cross).
        const image::Image *tick = _pictures.texture("cc_smchk");
        for (int i = 0; i < 13; ++i)
            if (_record.hasPassed(map, i)) {
                if (tick)
                    drawButtonState(o, *tick, kPassColumn - tick->w / 2, kCurriculumRows[i] - 2, 3, 1);
                else
                    font::drawText(o, kPassColumn - 4, kCurriculumRows[i], "X", {120, 255, 120});
            }
        // (The city isn't written: the background names its school, and the
        // heading's box is under the navigation bar.)
        font::drawText(o, 300, 380, "Left/Right: the city, the lesson   Enter: the garage", {200, 200, 255});
    } else if (_screen == Screen::kShowcase) {
        // The car alone, turning; its name and description.
        const CarInfo &car = _cars[size_t(_choice.car)];
        font::drawText(o, 40, 40, car.description, {255, 230, 140}, 2);
        std::string upper = car.base;
        for (char &c : upper)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        if (const image::Image *d = _pictures.texture(upper + "_DESC")) {
            image::Image flipped = *d; // stored upside down
            const size_t row = size_t(d->w) * 4;
            for (int y = 0; y < d->h; ++y)
                std::copy_n(d->rgba.begin() + std::ptrdiff_t(size_t(d->h - 1 - y) * row), row,
                            flipped.rgba.begin() + std::ptrdiff_t(size_t(y) * row));
            drawPicture(o, flipped, 40, 396, 219, 69);
        }
    } else if (_screen == Screen::kGarage) {
        const CarInfo &car = _cars[size_t(_choice.car)];
        const bool open = carOpen(_choice.car);
        const bool paint = paintOpen(_choice.car, _choice.paintJob);
        text(o, "VEHICLES", car.description);
        std::string color = size_t(_choice.paintJob) < car.colors.size() ? car.colors[size_t(_choice.paintJob)]
                                                                         : std::to_string(_choice.paintJob + 1);
        text(o, "CAR COLOR", color + (paint ? "" : " (locked)"));
        text(o, "TRANSMISSION", _choice.manual ? "Manual" : "Automatic");
        const auto &f = _figures[size_t(_choice.car)];
        const char *const bars[4] = {"HORSEPOWER", "TOP SPEED", "DURABILITY", "MASS"};
        for (size_t k = 0; k < 4; ++k)
            bar(o, bars[k], _most[k] > 0 ? f[k] / _most[k] : 0);
        // Locked: the lock and its picture's words (jpg/<car>_lck<n>, _p
        // for a professional's); else the car's own description.
        if (!open || !paint) {
            const std::string pic =
                car.base + "_lck" + (open ? std::to_string(_choice.paintJob) : "") + (_choice.professional ? "_p" : "");
            const image::Image *words = _pictures.jpg(pic);
            if (!words && _choice.professional)
                words = _pictures.jpg(car.base + "_lck" + (open ? std::to_string(_choice.paintJob) : ""));
            if (const image::Image *lock = _pictures.texture("LOCKED"))
                if (const Widget *w = widget("Locked Sign"))
                    drawPicture(o, *lock, w->x - lock->w / 2, w->y - lock->h / 2);
            if (words)
                drawPicture(o, *words, 40, 396);
        } else {
            std::string upper = car.base;
            for (char &c : upper)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            // Stored upside down (unlike the HUD's pictures).
            if (const image::Image *d = _pictures.texture(upper + "_DESC")) {
                image::Image flipped = *d;
                const size_t row = size_t(d->w) * 4;
                for (int y = 0; y < d->h; ++y)
                    std::copy_n(d->rgba.begin() + std::ptrdiff_t(size_t(d->h - 1 - y) * row), row,
                                flipped.rgba.begin() + std::ptrdiff_t(size_t(y) * row));
                drawPicture(o, flipped, 40, 396, 219, 69);
            }
        }
        // The window the car is drawn in, see-through.
        for (int y = kCarWindow.y; y < kCarWindow.y + kCarWindow.h; ++y)
            for (int x = kCarWindow.x; x < kCarWindow.x + kCarWindow.w; ++x)
                o.row(y)[x * 4 + 3] = 0;
        desc.clear();
    }
    if (!desc.empty())
        if (const image::Image *d = _pictures.jpg(desc))
            drawPicture(o, *d, 40, 396);
    font::drawText(o, 4, o.h - 12,
                   std::string("Up/Down, Left/Right, Enter, Esc; U: everything open (") + (openAll ? "on" : "off") +
                       ")",
                   {200, 200, 200});
}

} // namespace reluva::age
