// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's menus, redrawn: tune/widget.csv places each control on its
// menu's jpg/ background; the buttons are texture/ pictures of their
// states. What each screen shows and does is Reluva's reading of the
// pictures; see docs/engines/age-menus.md.

#include "engines/age/front_end.h"

#include "common/font.h"
#include "engines/age/texture.h"
#include "engines/age/viewers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace reluva::age {

namespace {

std::vector<std::string> splitLine(const std::string& line) {
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
    for (char& c : s)
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

const char* kindStem(RaceKind k) {
    switch (k) {
    case RaceKind::Blitz: return "blitz";
    case RaceKind::Circuit: return "circuit";
    case RaceKind::Checkpoint: return "race";
    case RaceKind::CrashCourse: return "crash";
    }
    return "race";
}

} // namespace

Error parseWidgets(const std::string& csv, std::vector<Widget>& out) {
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

const image::Image* MenuPictures::jpg(const std::string& name) {
    const std::string key = "jpg/" + lowerCase(name);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::unique_ptr<image::Image> im;
        std::vector<uint8_t> data;
        for (const char* ext : {".jpg", ".JPG"})
            if (!files_.read("jpg/" + name + ext, data)) {
                auto decoded = std::make_unique<image::Image>();
                if (!image::decode(data, *decoded))
                    im = std::move(decoded);
                break;
            }
        it = cache_.emplace(key, std::move(im)).first;
    }
    return it->second.get();
}

const image::Image* MenuPictures::texture(const std::string& name) {
    const std::string key = "tex/" + lowerCase(name);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::unique_ptr<image::Image> im;
        Tex tex;
        if (!loadTexture(files_, name, tex))
            im = std::make_unique<image::Image>(std::move(tex.image));
        it = cache_.emplace(key, std::move(im)).first;
    }
    return it->second.get();
}

void drawPicture(Surface& overlay, const image::Image& im, int x, int y, int w, int h) {
    w = w > 0 ? w : im.w;
    h = h > 0 ? h : im.h;
    if (im.w <= 0 || im.h <= 0)
        return;
    for (int yy = std::max(0, y); yy < std::min(overlay.h, y + h); ++yy) {
        uint8_t* row = overlay.row(yy);
        const int sy = std::min(im.h - 1, (yy - y) * im.h / h);
        for (int xx = std::max(0, x); xx < std::min(overlay.w, x + w); ++xx) {
            const uint8_t* s = im.pixel(std::min(im.w - 1, (xx - x) * im.w / w), sy);
            uint8_t* d = row + xx * 4;
            const float a = float(s[3]) / 255, da = float(d[3]) / 255;
            const float out = a + da * (1 - a);
            if (out <= 0)
                continue;
            for (int k = 0; k < 3; ++k)
                d[k] = uint8_t(std::lround((float(s[k]) * a + float(d[k]) * da * (1 - a)) / out));
            d[3] = uint8_t(std::lround(out * 255));
        }
    }
}

void drawButtonState(Surface& overlay, const image::Image& im, int x, int y, int states, int state) {
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

FrontEnd::FrontEnd(const SearchSet& files, const std::vector<CityInfo>& cities, const std::vector<CarInfo>& cars,
                   const GameStrings& strings, const PlayerRecord& record)
    : files_(files), cities_(cities), cars_(cars), strings_(strings), record_(record), pictures_(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        ok_ = !parseWidgets(std::string(data.begin(), data.end()), widgets_) && pictures_.jpg("main_bk") &&
              !cities.empty() && !cars.empty();
    // The race menu's CHANGE (texture/race_chg, 25 x 122 a state) isn't in
    // widget.csv: up the right of the time, weather and densities.
    widgets_.push_back({"Single Race Menu", "race_chg", 612, 247, 25, 122, "5 State"});
    // The garage's bars: each car's .info figures against the most.
    for (const CarInfo& car : cars) {
        std::array<float, 4> f{};
        if (!files.read("tune/" + car.base + ".info", data)) {
            const std::string text(data.begin(), data.end());
            const char* const keys[4] = {"Horsepower=", "Top Speed=", "Durability=", "Mass="};
            for (int k = 0; k < 4; ++k)
                if (const size_t at = text.find(keys[k]); at != std::string::npos)
                    f[size_t(k)] = std::strtof(text.c_str() + at + std::strlen(keys[k]), nullptr);
        }
        for (size_t k = 0; k < 4; ++k)
            most_[k] = std::max(most_[k], f[k]);
        figures_.push_back(f);
    }
    refresh();
}

const Widget* FrontEnd::widget(const std::string& name) const {
    static const char* const kMenus[] = {"Main Menu", "Single Race Menu",   "Crash Course",    "Garage Menu",
                                         "",          "Crash Course Intro", "Vehicle Showcase"};
    const std::string menu = kMenus[int(screen_)];
    for (const Widget& w : widgets_)
        if (w.menu == menu && w.name == name)
            return &w;
    // The navigation bar, top right of every screen.
    for (const Widget& w : widgets_)
        if (w.menu == "Navigation Bar" && w.name == name)
            return &w;
    return nullptr;
}

bool FrontEnd::raceOpen(int index) const {
    if (openAll || cities_.empty())
        return true;
    const std::string& map = cities_[size_t(choice_.city)].map;
    const RaceUnlocking u = raceUnlocking(files_, map);
    return age::raceOpen(record_, map, choice_.kind, index, choice_.professional, u.group, u.mustPlace);
}

bool FrontEnd::carOpen(int car) const { return paintOpen(car, -1); }

bool FrontEnd::paintOpen(int car, int paintJob) const {
    if (openAll || car < 0 || size_t(car) >= cars_.size())
        return true;
    const SearchSet& files = files_;
    return unlocked(
        record_, cars_[size_t(car)].base, paintJob, choice_.professional,
        [&files](const std::string& city, RaceKind kind) { return int(raceNames(files, city, kind).size()); });
}

void FrontEnd::refresh() {
    if (cities_.empty())
        return;
    const std::string& map = cities_[size_t(choice_.city)].map;
    raceNames_ = raceNames(files_, map, choice_.kind);
    choice_.race = std::clamp(choice_.race, 0, std::max(0, int(raceNames_.size()) - 1));
    if (!raceNames_.empty())
        loadRace(files_, map, choice_.kind, choice_.race, race_);
    // Another race (or level): its own conditions again.
    const std::string key = map + " " + std::to_string(int(choice_.kind)) + " " + std::to_string(choice_.race) +
                            (choice_.professional ? " p" : " a");
    if (key != conditionsFor_) {
        conditionsFor_ = key;
        choice_.conditions = raceConditions();
        choice_.changed = false;
    }
    if (!cars_.empty())
        paintJobs_ = std::max(1, int(cars_[size_t(choice_.car)].colors.size()));
    choice_.paintJob = std::clamp(choice_.paintJob, 0, paintJobs_ - 1);
}

FrontEnd::Choice::Conditions FrontEnd::raceConditions() const {
    Choice::Conditions c;
    if (raceNames_.empty())
        return c;
    const RaceSettings& s = race_.settings[choice_.professional ? 1 : 0];
    c.laps = std::max(1, s.laps);
    c.opponents = std::min(std::max(0, s.opponents), mostOpponents());
    c.timeOfDay = std::clamp(s.timeOfDay, 0, 3);
    c.weather = std::clamp(s.weather, 0, 3);
    c.cops = std::max(0, s.cops);
    c.traffic = s.ambient;
    c.peds = s.peds;
    return c;
}

int FrontEnd::mostOpponents() const { return int(race_.opponents[choice_.professional ? 1 : 0].size()); }

std::vector<FrontEnd::Item> FrontEnd::items() const {
    std::vector<Item> list = screenItems();
    // The navigation bar's Options and Exit (the help and minimise
    // buttons aren't Reluva's).
    if (screen_ != Screen::Splash) {
        list.push_back({"mnav_opt", true, true, "MNAV_OPT", 5});
        list.push_back({"mnav_exit", true, true, "mnav_ext", 3});
    }
    return list;
}

std::vector<FrontEnd::Item> FrontEnd::screenItems() const {
    switch (screen_) {
    case Screen::Main:
        return {{"dvrcc", true},
                {"main_sp", true},
                {"main_mp", true, multiplayer},
                {"main_qck", true, quickRace()},
                {"DRIVER NAME", false},
                {"dvrnew", true, roster_ != nullptr, "DVRNEW", 4},
                {"dvrdel", true, roster_ != nullptr, "DVRDEL", 4},
                {"dvrsts", true, roster_ != nullptr, "DVRSTS", 4},
                // RACE RECORDS and INSTANT REPLAY overlap where widget.csv
                // puts them: the replay's drawn last, over the records'.
                {"race_rec", true, roster_ != nullptr, "RACE_REC", 4},
                {"main_rpl", true, true, "main_rpl", 4}};
    case Screen::Races:
        // LAPS (a circuit's) and OPPONENTS change with Left/Right; CHANGE
        // (texture/race_chg) the rest.
        return {{"blitz", true},
                {"cp", true},
                {"circuit", true},
                {"RACE NAME", false},
                {"LAPS", false, choice_.kind == RaceKind::Circuit},
                {"OPPONENTS", false, mostOpponents() > 0},
                {"RACE LOCALE", false},
                {"race_chg", true, !raceNames_.empty(), "race_chg", 5},
                {"race_veh", true}};
    case Screen::CrashCourse: return {{"city", false}, {"CRASH NAME", false}, {"host_dn", true}};
    case Screen::Garage:
        return {{"VEHICLES", false},
                {"CAR COLOR", false},
                {"TRANSMISSION", false},
                {"veh_show", true, true, "veh_show", 4},
                {"vesel_godrive", true}};
    case Screen::CrashIntro:
        // widget.csv's row for its BACK is garbled: the navigation bar's.
        return {{"cci_lon", true, true, "cci_lon", 4},
                {"cci_sf", true, true, "cci_sf", 4},
                {"mnav_prev", true, true, "mnav_prv", 4}};
    case Screen::Showcase:
        return {{"opt_done", true, true, "opt_done", 4}};
    case Screen::Splash: return {};
    }
    return {};
}

void FrontEnd::go(Screen s) {
    screen_ = s;
    focus_ = 0;
    if (s == Screen::CrashCourse)
        choice_.kind = RaceKind::CrashCourse;
    else if (s == Screen::Races && choice_.kind == RaceKind::CrashCourse)
        choice_.kind = RaceKind::Blitz;
    refresh();
}

void FrontEnd::activate(const Item& item, Action& action) {
    if (!item.enabled)
        return;
    const std::string& w = item.widget;
    if (w == "dvrcc")
        go(Screen::CrashIntro);
    else if (w == "cci_lon" || w == "cci_sf") {
        // London's cab company or San Francisco's stunt school.
        for (size_t i = 0; i < cities_.size(); ++i)
            if ((cities_[i].map == "london") == (w == "cci_lon"))
                choice_.city = int(i);
        go(Screen::CrashCourse);
    } else if (w == "mnav_prev" && screen_ == Screen::CrashIntro)
        go(Screen::Main);
    else if (w == "veh_show")
        go(Screen::Showcase);
    else if (w == "opt_done" && screen_ == Screen::Showcase) {
        screen_ = Screen::Garage;
        focus_ = 3;
    } else if (w == "main_sp")
        go(Screen::Races);
    else if (w == "main_mp")
        action = Action::Multiplayer;
    else if (w == "mnav_opt")
        action = Action::Options;
    else if (w == "main_rpl")
        action = Action::Replays;
    else if (w == "dvrnew")
        action = Action::NewDriver;
    else if (w == "dvrdel")
        action = Action::DeleteDriver;
    else if (w == "dvrsts")
        action = Action::DriverStats;
    else if (w == "race_rec")
        action = Action::RaceRecords;
    else if (w == "main_qck") {
        // Quick Race: "Choose your vehicle and you're off! Quick Race starts
        // you in the last race you ran."
        std::string map;
        RaceKind kind = RaceKind::Blitz;
        int index = 0;
        if (!record_.lastRace(map, kind, index))
            return;
        for (size_t i = 0; i < cities_.size(); ++i)
            if (cities_[i].map == map)
                choice_.city = int(i);
        choice_.kind = kind;
        choice_.race = index;
        refresh();
        afterGarage_ = Screen::Main;
        screen_ = Screen::Garage;
        focus_ = 0;
        action = Action::Changed;
    } else if (w == "mnav_exit")
        action = Action::Quit;
    else if (w == "blitz" || w == "cp" || w == "circuit") {
        choice_.kind = w == "blitz" ? RaceKind::Blitz : w == "cp" ? RaceKind::Checkpoint : RaceKind::Circuit;
        refresh();
        action = Action::Changed;
    } else if (w == "race_veh" || w == "host_dn") {
        if (raceNames_.empty() || !raceOpen(choice_.race))
            return;
        afterGarage_ = screen_;
        screen_ = Screen::Garage;
        focus_ = 0;
        action = Action::Changed;
    } else if (w == "vesel_godrive") {
        action = carOpen(choice_.car) && paintOpen(choice_.car, choice_.paintJob) ? Action::Start : Action::Locked;
    } else if (w == "race_chg") {
        action = Action::RaceOptions;
    }
}

void FrontEnd::change(const Item& item, int step) {
    auto wrap = [](int v, int n) { return n > 0 ? (v % n + n) % n : 0; };
    const std::string& w = item.widget;
    if (w == "RACE NAME" || w == "CRASH NAME") {
        choice_.race = wrap(choice_.race + step, int(raceNames_.size()));
    } else if (w == "LAPS") {
        choice_.conditions.laps = std::clamp(choice_.conditions.laps + step, 1, 20);
        conditionsChanged();
        return;
    } else if (w == "OPPONENTS") {
        choice_.conditions.opponents = std::clamp(choice_.conditions.opponents + step, 0, mostOpponents());
        conditionsChanged();
        return;
    } else if (w == "RACE LOCALE" || w == "city") {
        choice_.city = wrap(choice_.city + step, int(cities_.size()));
    } else if (w == "DRIVER NAME") {
        // The next driver (their level comes with them).
        if (roster_ && !roster_->drivers().empty()) {
            roster_->setCurrent((roster_->current() + step + int(roster_->drivers().size())) %
                                int(roster_->drivers().size()));
            roster_->save();
            choice_.professional = roster_->driver().professional;
            driverChanged_ = true;
        } else
            choice_.professional = !choice_.professional;
    } else if (w == "VEHICLES") {
        choice_.car = wrap(choice_.car + step, int(cars_.size()));
        choice_.paintJob = 0;
    } else if (w == "CAR COLOR") {
        choice_.paintJob = wrap(choice_.paintJob + step, paintJobs_);
    } else if (w == "TRANSMISSION") {
        choice_.manual = !choice_.manual;
    }
    refresh();
}

bool FrontEnd::hit(const Item& item, int x, int y) {
    if (item.widget == "city")
        return x >= kCityBox.x && x < kCityBox.x + kCityBox.w && y >= kCityBox.y && y < kCityBox.y + kCityBox.h;
    const Widget* w = widget(item.widget);
    if (!w)
        return false;
    int width = w->w, height = w->h;
    if (item.button)
        if (const image::Image* im = pictures_.texture(item.pictureName())) {
            const int states = statesOf(item, *w);
            width = im->w;
            height = im->h / states;
        }
    return x >= w->x && x < w->x + width && y >= w->y && y < w->y + height;
}

FrontEnd::Action FrontEnd::handle(const Event& ev) {
    Action action = Action::None;
    if (screen_ == Screen::Splash) {
        // Any key or click goes on to the main menu.
        if (ev.type == EventType::KeyDown || (ev.type == EventType::MouseDown && ev.button == MouseButton::Left)) {
            go(Screen::Main);
            action = Action::Changed;
        }
        return action;
    }
    const auto list = items();
    if (list.empty())
        return action;
    focus_ = std::clamp(focus_, 0, int(list.size()) - 1);
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        for (size_t i = 0; i < list.size(); ++i)
            if (hit(list[i], ev.mouseX, ev.mouseY)) {
                focus_ = int(i);
                if (list[i].button)
                    activate(list[i], action);
                else {
                    // A field: its right half steps on, its left back.
                    const Widget* w = widget(list[i].widget);
                    change(list[i], w && ev.mouseX < w->x + w->w / 2 ? -1 : 1); // the city's box: on
                    if (driverChanged_) {
                        driverChanged_ = false;
                        return Action::DriverChanged;
                    }
                    action = Action::Changed;
                }
                return action;
            }
        return action;
    }
    if (ev.type != EventType::KeyDown)
        return action;
    const Item& item = list[size_t(focus_)];
    switch (ev.key) {
    case key::Up:
    case key::Down: {
        const int step = ev.key == key::Down ? 1 : -1;
        for (size_t tries = 0; tries < list.size(); ++tries) {
            focus_ = (focus_ + step + int(list.size())) % int(list.size());
            if (list[size_t(focus_)].enabled)
                break;
        }
        break;
    }
    case key::Left:
    case key::Right:
        if (!item.button) {
            change(item, ev.key == key::Right ? 1 : -1);
            if (driverChanged_) {
                driverChanged_ = false;
                return Action::DriverChanged;
            }
            action = Action::Changed;
        }
        break;
    case key::Return:
    case key::KpEnter:
        if (item.button)
            activate(item, action);
        else if (screen_ == Screen::Races && item.widget == "RACE LOCALE")
            action = Action::CityChoice; // the game's Race Locales
        else if (screen_ == Screen::Races)
            activate({"race_veh", true}, action);
        else if (screen_ == Screen::CrashCourse)
            activate({"host_dn", true}, action);
        else if (screen_ == Screen::Garage)
            activate({"vesel_godrive", true}, action);
        break;
    case key::Escape:
        if (screen_ == Screen::Garage)
            go(afterGarage_);
        else if (screen_ == Screen::Showcase) {
            screen_ = Screen::Garage;
            focus_ = 3;
        } else if (screen_ == Screen::CrashCourse)
            go(Screen::CrashIntro);
        else if (screen_ != Screen::Main)
            go(Screen::Main);
        else {
            action = Action::Quit; // "Quit?"
            break;
        }
        action = Action::Changed;
        break;
    case 'u': openAll = !openAll; break;
    default: break;
    }
    return action;
}

void FrontEnd::text(Surface& o, const std::string& name, const std::string& s, Color c) const {
    if (const Widget* w = widget(name))
        font::drawText(o, w->x + 6, w->y + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void FrontEnd::bar(Surface& o, const std::string& name, float share) const {
    const Widget* w = widget(name);
    if (!w)
        return;
    // The garage's (6 high) sit on the grid beside their labels: 18
    // pixels lower than widget.csv says, from x 148, 130 wide (measured on
    // jpg/veh_bk).
    const bool garage = w->h < 10;
    const int x0 = garage ? 148 : w->x, y0 = w->y + (garage ? 18 : 0), width = garage ? 130 : w->w;
    const int lit = int(std::clamp(share, 0.0f, 1.0f) * float(width));
    for (int y = y0; y < y0 + std::max(w->h, 4) && y < o.h; ++y) {
        uint8_t* p = o.row(y) + x0 * 4;
        for (int x = 0; x < width && x0 + x < o.w; ++x, p += 4) {
            p[0] = x < lit ? 255 : 40;
            p[1] = x < lit ? 200 : 40;
            p[2] = x < lit ? 40 : 60;
            p[3] = 255;
        }
    }
}

FrontEnd::Rect FrontEnd::carWindow() const {
    return screen_ == Screen::Garage ? kCarWindow : screen_ == Screen::Showcase ? Rect{0, 0, 640, 480} : Rect{};
}

void FrontEnd::draw(Surface& o) {
    std::fill(o.pixels.begin(), o.pixels.end(), 0);
    if (cities_.empty() || cars_.empty())
        return;
    const std::string& map = cities_[size_t(choice_.city)].map;
    const std::string city = map == "london" ? "london" : "sf";
    const char* background = "main_bk";
    switch (screen_) {
    case Screen::Main: background = "main_bk"; break;
    case Screen::Races: background = "race_bk"; break;
    case Screen::CrashCourse: background = map == "london" ? "cclon_bk" : "ccsf_bk"; break;
    case Screen::Garage: background = "veh_bk"; break;
    case Screen::Splash: background = "splash"; break;
    case Screen::CrashIntro:
        background = "ilon_bk";
        break;
    case Screen::Showcase:
        background = "";
        break; // the car, the whole screen
    }
    if (screen_ == Screen::Splash) {
        if (const image::Image* bg = pictures_.jpg(background))
            drawPicture(o, *bg, 0, 0, o.w, o.h);
        return;
    }
    if (const image::Image* bg = pictures_.jpg(background))
        drawPicture(o, *bg, 0, 0, o.w, o.h);

    // The buttons, each in its state: 0 normal, 1 focused (and for the
    // five-state ones, 2 chosen); the last, not available.
    const auto list = items();
    for (size_t i = 0; i < list.size(); ++i) {
        const Item& item = list[i];
        const Widget* w = widget(item.widget);
        if (!w)
            continue;
        const bool focused = int(i) == focus_;
        if (item.button) {
            const image::Image* im = pictures_.texture(item.pictureName());
            if (!im)
                continue;
            const int states = statesOf(item, *w);
            bool chosen = (item.widget == "blitz" && choice_.kind == RaceKind::Blitz) ||
                          (item.widget == "cp" && choice_.kind == RaceKind::Checkpoint) ||
                          (item.widget == "circuit" && choice_.kind == RaceKind::Circuit);
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
    if (screen_ == Screen::Main) {
        static const std::map<std::string, std::string> kDesc{
            {"dvrcc", "mn_cc"},     {"main_sp", "mn_sp"},     {"main_mp", "mn_mp"}, {"main_qck", "mn_qck"},
            {"dvrnew", "mn_new"},   {"dvrdel", "mn_del"},     {"dvrsts", "mn_sts"}, {"race_rec", "mn_rec"},
            {"main_rpl", "mn_rpl"}, {"DRIVER NAME", "mn_drv"}};
        if (auto it = kDesc.find(list[size_t(focus_)].widget); it != kDesc.end())
            desc = it->second;
        // The driver, and every driver in the box below (the chosen one lit).
        const std::string name = roster_ ? roster_->driver().name : std::string("Player");
        text(o, "DRIVER NAME", name + " (" + (choice_.professional ? "Professional" : "Amateur") + ")");
        if (roster_)
            for (size_t i = 0; i < roster_->drivers().size() && i < 14; ++i) {
                const DriverEntry& d = roster_->drivers()[i];
                font::drawText(o, 184, 186 + int(i) * 14, d.name + (d.professional ? " (Pro)" : ""),
                               int(i) == roster_->current() ? Color{255, 255, 120} : Color{200, 200, 255});
            }
    } else if (screen_ == Screen::Races) {
        desc = choice_.kind == RaceKind::Blitz     ? "race_btz"
               : choice_.kind == RaceKind::Circuit ? "race_cir"
                                                   : "race_cp";
        // The race's map, its name and its numbers.
        const std::string mapPic = city + "_map" +
                                   (choice_.kind == RaceKind::Checkpoint ? "race" : kindStem(choice_.kind)) +
                                   std::to_string(choice_.race);
        if (const image::Image* m = pictures_.jpg(mapPic))
            if (const Widget* w = widget("race map"))
                drawPicture(o, *m, w->x, w->y, w->w, w->h);
        std::string name = raceNames_.empty() ? "" : raceNames_[size_t(choice_.race)];
        if (!raceOpen(choice_.race))
            name += " (locked)";
        text(o, "RACE NAME", name);
        if (!raceNames_.empty()) {
            const Choice::Conditions& c = choice_.conditions;
            text(o, "LAPS", choice_.kind == RaceKind::Circuit ? std::to_string(c.laps) : "-");
            text(o, "OPPONENTS", std::to_string(c.opponents));
            static const char* const kTimes[] = {"Morning", "Noon", "Evening", "Night"};
            static const char* const kWeathers[] = {"Clear", "Cloudy", "Fog", "Rain"};
            text(o, "TIME", kTimes[c.timeOfDay & 3]);
            text(o, "WEATHER", kWeathers[c.weather & 3]);
            bar(o, "PEDESTRIAN DENSITY", c.hidePeds ? 0 : c.peds);
            bar(o, "TRAFFIC DENSITY", c.traffic);
            bar(o, "COP DENSITY", std::min(1.0f, float(c.cops) / 4));
            if (choice_.changed) // as the game's records say: not recorded
                font::drawText(o, 404, 186, "Changed: not recorded", {255, 160, 120});
            const int best = record_.bestPlace(map, choice_.kind, choice_.race, choice_.professional);
            if (best > 0)
                font::drawText(o, 404, 170,
                               "Best: " + std::to_string(best) +
                                   (best == 1   ? "st"
                                    : best == 2 ? "nd"
                                    : best == 3 ? "rd"
                                                : "th"),
                               {150, 255, 150});
        }
        text(o, "RACE LOCALE", cities_[size_t(choice_.city)].name);
    } else if (screen_ == Screen::CrashCourse) {
        desc = (map == "london" ? "lon_cc" : "sf_cc") + std::to_string(choice_.race);
        std::string name = raceNames_.empty() ? "" : raceNames_[size_t(choice_.race)];
        if (!raceOpen(choice_.race))
            name += " (locked)";
        text(o, "CRASH NAME", name);
        // The lessons passed, ticked on the curriculum (texture/cc_smchk:
        // blank, a tick, a cross).
        const image::Image* tick = pictures_.texture("cc_smchk");
        for (int i = 0; i < 13; ++i)
            if (record_.hasPassed(map, i)) {
                if (tick)
                    drawButtonState(o, *tick, kPassColumn - tick->w / 2, kCurriculumRows[i] - 2, 3, 1);
                else
                    font::drawText(o, kPassColumn - 4, kCurriculumRows[i], "X", {120, 255, 120});
            }
        // (The city: the background names its school; the heading's box,
        // where it went, is under the navigation bar now.)
        font::drawText(o, 300, 380, "Left/Right: the city, the lesson   Enter: the garage", {200, 200, 255});
    } else if (screen_ == Screen::Showcase) {
        // The car alone, turning; its name and description.
        const CarInfo& car = cars_[size_t(choice_.car)];
        font::drawText(o, 40, 40, car.description, {255, 230, 140}, 2);
        std::string upper = car.base;
        for (char& c : upper)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        if (const image::Image* d = pictures_.texture(upper + "_DESC")) {
            image::Image flipped = *d; // stored upside down
            const size_t row = size_t(d->w) * 4;
            for (int y = 0; y < d->h; ++y)
                std::copy_n(d->rgba.begin() + std::ptrdiff_t(size_t(d->h - 1 - y) * row), row,
                            flipped.rgba.begin() + std::ptrdiff_t(size_t(y) * row));
            drawPicture(o, flipped, 40, 396, 219, 69);
        }
    } else if (screen_ == Screen::Garage) {
        const CarInfo& car = cars_[size_t(choice_.car)];
        const bool open = carOpen(choice_.car), paint = paintOpen(choice_.car, choice_.paintJob);
        text(o, "VEHICLES", car.description);
        std::string color = size_t(choice_.paintJob) < car.colors.size() ? car.colors[size_t(choice_.paintJob)]
                                                                         : std::to_string(choice_.paintJob + 1);
        text(o, "CAR COLOR", color + (paint ? "" : " (locked)"));
        text(o, "TRANSMISSION", choice_.manual ? "Manual" : "Automatic");
        const auto& f = figures_[size_t(choice_.car)];
        const char* const bars[4] = {"HORSEPOWER", "TOP SPEED", "DURABILITY", "MASS"};
        for (size_t k = 0; k < 4; ++k)
            bar(o, bars[k], most_[k] > 0 ? f[k] / most_[k] : 0);
        // Locked: the lock and its picture's words (jpg/<car>_lck<n>, _p
        // for a professional's); else the car's own description.
        if (!open || !paint) {
            const std::string pic =
                car.base + "_lck" + (open ? std::to_string(choice_.paintJob) : "") + (choice_.professional ? "_p" : "");
            const image::Image* words = pictures_.jpg(pic);
            if (!words && choice_.professional)
                words = pictures_.jpg(car.base + "_lck" + (open ? std::to_string(choice_.paintJob) : ""));
            if (const image::Image* lock = pictures_.texture("LOCKED"))
                if (const Widget* w = widget("Locked Sign"))
                    drawPicture(o, *lock, w->x - lock->w / 2, w->y - lock->h / 2);
            if (words)
                drawPicture(o, *words, 40, 396);
        } else {
            std::string upper = car.base;
            for (char& c : upper)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            // Stored upside down (unlike the HUD's pictures).
            if (const image::Image* d = pictures_.texture(upper + "_DESC")) {
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
        if (const image::Image* d = pictures_.jpg(desc))
            drawPicture(o, *d, 40, 396);
    font::drawText(o, 4, o.h - 12,
                   std::string("Up/Down, Left/Right, Enter, Esc; U: everything open (") + (openAll ? "on" : "off") +
                       ")",
                   {200, 200, 200});
}

} // namespace reluva::age
