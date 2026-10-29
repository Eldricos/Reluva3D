// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/image.h"
#include "common/types.h"
#include "engines/age/city_scene.h"
#include "engines/age/lang.h"
#include "engines/age/race.h"
#include "engines/age/record.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

struct CarInfo;

// A control of the game's menus (tune/widget.csv: menu, its id, the
// control's name, its id, x, y, width, height and a description).
struct Widget {
    std::string menu, name;
    int x = 0, y = 0, w = 0, h = 0;
    std::string desc;
};
Error parseWidgets(const std::string& csv, std::vector<Widget>& out);

// The menus' pictures: jpg/<name>.jpg backgrounds and texture/<name>
// buttons (stacks of 4 or 5 states, one above another), read once.
class MenuPictures {
public:
    explicit MenuPictures(const SearchSet& files) : files_(files) {}
    const image::Image* jpg(const std::string& name);
    const image::Image* texture(const std::string& name);

private:
    const SearchSet& files_;
    std::map<std::string, std::unique_ptr<image::Image>> cache_;
};

// Draws `im` (scaled to w x h when given) over the overlay, blending by
// its alpha; or one state of a stacked button.
void drawPicture(Surface& overlay, const image::Image& im, int x, int y, int w = 0, int h = 0);
void drawButtonState(Surface& overlay, const image::Image& im, int x, int y, int states, int state);

// The game's menus as Reluva draws them: the main menu, the races, the
// Crash Course and the garage, laid out by widget.csv on the jpg/
// backgrounds. See docs/engines/age-menus.md.
class FrontEnd {
public:
    // Splash: jpg/splash.jpg, shown first (in place of the opening movie,
    // logos.avi, which is Indeo 5 and not played).
    // CrashIntro: jpg/ilon_bk, choosing the school; Showcase: the garage's
    // car alone, the whole screen.
    enum class Screen { Main, Races, CrashCourse, Garage, Splash, CrashIntro, Showcase };
    // What the menus have chosen.
    struct Choice {
        int city = 0;
        RaceKind kind = RaceKind::Blitz;
        int race = 0;
        bool professional = false;
        int car = 0, paintJob = 0;
        bool manual = false;
    };
    enum class Action { None, Changed, Start, Multiplayer, Options, Quit, Replays };

    FrontEnd(const SearchSet& files, const std::vector<CityInfo>& cities, const std::vector<CarInfo>& cars,
             const GameStrings& strings, const PlayerRecord& record);
    bool loaded() const { return ok_; }

    Action handle(const Event& ev);
    void draw(Surface& overlay);

    Screen screen() const { return screen_; }
    // Back from a race: to the screen it was chosen on (refreshed, for
    // what's been unlocked).
    void afterRace() { go(afterGarage_); }
    Choice& choice() { return choice_; }
    // The control with the focus (for the menus' voices).
    std::string focusedWidget() const {
        const auto list = items();
        return focus_ >= 0 && size_t(focus_) < list.size() ? list[size_t(focus_)].widget : std::string();
    }
    // Where the garage shows the car (in the overlay's pixels), to draw it
    // there in 3D; empty off the garage.
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
    };
    Rect carWindow() const;
    // "U": everything open, whatever the record says.
    bool openAll = false;
    // Whether the main menu's Multiplayer button leads anywhere.
    bool multiplayer = false;
    // The garage alone (for the multiplayer lobby's car): Esc goes back
    // to the main menu, GO DRIVE starts.
    void showGarage() {
        afterGarage_ = Screen::Main;
        screen_ = Screen::Garage;
        focus_ = 0;
        refresh();
    }
    bool raceOpen(int index) const;
    bool carOpen(int car) const;
    bool paintOpen(int car, int paintJob) const;

private:
    struct Item {
        Item(std::string w, bool b, bool e = true, std::string p = {}, int n = 0)
            : widget(std::move(w)), button(b), enabled(e), picture(std::move(p)), states(n) {}
        std::string widget;  // its widget.csv name (its picture, for buttons)
        bool button = false; // else a field changed with Left/Right
        bool enabled = true;
        std::string picture; // when it isn't the widget's name
        int states = 0;      // when the description doesn't say
        const std::string& pictureName() const { return picture.empty() ? widget : picture; }
    };
    std::vector<Item> items() const;
    std::vector<Item> screenItems() const;
    int statesOf(const Item& item, const Widget& w) const {
        return item.states ? item.states : w.desc.find("5 State") != std::string::npos ? 5 : 4;
    }
    const Widget* widget(const std::string& name) const;
    void go(Screen s);
    void activate(const Item& item, Action& action);
    void change(const Item& item, int step);
    void refresh(); // the race's names and settings
    void text(Surface& o, const std::string& widget, const std::string& s, Color c = {255, 230, 140}) const;
    void bar(Surface& o, const std::string& widget, float share) const;
    bool hit(const Item& item, int x, int y);

    const SearchSet& files_;
    const std::vector<CityInfo>& cities_;
    const std::vector<CarInfo>& cars_;
    const GameStrings& strings_;
    const PlayerRecord& record_;
    MenuPictures pictures_;
    std::vector<Widget> widgets_;
    bool ok_ = false;

    Screen screen_ = Screen::Splash;
    Screen afterGarage_ = Screen::Races; // where the garage goes back to
    int focus_ = 0;
    Choice choice_;
    std::vector<std::string> raceNames_;
    RaceDefinition race_;
    int paintJobs_ = 1;
    // The .info figures (Horsepower, Top Speed, Durability, Mass) of each
    // car, for the garage's bars, and the most of each.
    std::vector<std::array<float, 4>> figures_;
    std::array<float, 4> most_{};
};

} // namespace reluva::age
