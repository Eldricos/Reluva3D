// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/object_names.h"
#include "engines/tpw/ui/park_windows.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The park's menus' windows of information: the visitors' statistics, a
 * visitor's window and their list, the attractions' list, the park's
 * statistics and awards, the Message Center, and the game's menu. See
 * docs/engines/tpw/games/simcoaster/systems/park-windows.md.
 */

/** Which visitors a tab shows: all, or a generation's. */
enum class VisitorGroup { kAll, kChildren, kAdults, kSeniors };

/** Visitor Statistics (UITEXT 376 to 385), as the recording shows it at 7:20. */
class VisitorStatsWindow {
public:
    enum class Control { kNone, kTab, kClose };
    void open() {
        _open = true;
        _group = VisitorGroup::kAll;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    bool _open = false;
    VisitorGroup _group = VisitorGroup::kAll;
    Control _over = Control::kNone;
};

/**
 * A visitor's window ("Visitor #{28}", UITEXT 658), as the recording
 * shows it at 9:30: their ratings and counts, what they're doing, and
 * the arrows to the visitors before and after them.
 */
class VisitorWindow {
public:
    enum class Control { kNone, kGoTo, kPrevious, kNext, kClose };
    void open(int visitor) {
        _open = true;
        _visitor = visitor;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    int visitor() const { return _visitor; }
    /** kChose when the camera's to go to them (the left button). */
    WindowResult handle(const Event &ev, const Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    int _visitor = 0;
    Control _over = Control::kNone;
};

/** List All Visitors: a tab a generation, their number, cash, time, rides, thought and happiness (366 to 371). */
class VisitorListWindow {
public:
    enum class Control { kNone, kTab, kRow, kOk, kClose };
    void open() {
        _open = true;
        _chosen = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** The visitor chosen: 0 for none. */
    int chosen() const { return _chosen; }
    WindowResult handle(const Event &ev, const Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    std::vector<int> listed(const Park &park) const;
    bool _open = false;
    VisitorGroup _group = VisitorGroup::kAll;
    int _chosen = 0;
    Control _over = Control::kNone;
};

/** List All Attractions: a tab a kind (342 to 346), each with its kind's columns (347 to 364). */
class AttractionListWindow {
public:
    enum class Control { kNone, kTab, kRow, kOk, kClose };
    void open() {
        _open = true;
        _chosen = -1;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** The object chosen, by its place in the park's objects; −1 for none. */
    int chosen() const { return _chosen; }
    WindowResult handle(const Event &ev, const Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const ObjectNames &names) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    std::vector<size_t> listed(const Park &park) const;
    bool _open = false;
    size_t _tab = 0;
    int _chosen = -1;
    Control _over = Control::kNone;
};

/** Park Statistics (422 to 426): the visitors in the park and to date, how fast they come, the park's rating. */
class ParkStatsWindow {
public:
    void open() { _open = true; }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect okRect();

private:
    bool _open = false;
};

/**
 * Park Awards ("Park Awards - {20}", 452): the awards (453 to 460), each
 * held with its measure, else "None" (472).
 */
class AwardsWindow {
public:
    void open(std::string park) {
        _open = true;
        _park = std::move(park);
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect okRect();

private:
    bool _open = false;
    std::string _park;
};

/**
 * The Message Center (UITEXT 861), as the recording shows it at 20:10:
 * the messages received, newest first, with their dates; the one chosen
 * in full; deleting it.
 */
class MessageCenterWindow {
public:
    /** A message as it was received, on a day of the park's. */
    struct Received {
        Park::Message message;
        uint32_t day = 0;
        bool read = false;
    };
    enum class Control { kNone, kRow, kDelete, kOk, kClose };
    void open() {
        _open = true;
        _chosen = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, std::vector<Received> &inbox);
    /** Draws the inbox, its chosen message to `player`, its dates as `date` writes a day. */
    void draw(Surface &screen, const WindowKit &kit, const std::vector<Received> &inbox, const std::string &player,
              const std::function<std::string(uint32_t)> &date) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    bool _open = false;
    size_t _chosen = 0; // in the inbox as listed, newest first
    Control _over = Control::kNone;
};

/**
 * Activating a special ride ("Activate Ride", UITEXT 984 to 987): the
 * staff its abilities need and those assigned, the golden tickets it
 * costs; the tick activates it once all's there.
 */
class ActivateRideWindow {
public:
    void open(size_t object, std::string name) {
        _open = true;
        _object = object;
        _name = std::move(name);
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    size_t object() const { return _object; }
    /** kChose once it's activated. */
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const;
    static ScreenRect okRect();
    static ScreenRect cancelRect();

private:
    bool _open = false;
    size_t _object = 0;
    std::string _name;
};

/**
 * The Park Map (UITEXT 852): the world's map picture (`2dmap.tga`), its
 * attractions' marks (`2dmap/map_ride.tga` and the like), the visitors as
 * dots and the camera's place; a click on it takes the camera there.
 */
class ParkMapWindow {
public:
    /**
     * The Park Map (852), or Visitor Opinion (854): the same map, each
     * visitor's dot coloured by how happy they are, as Visitor Statistics
     * colours its bars (guessed: the recording never opens it).
     */
    enum class Mode { kPark, kOpinion };
    /** Reads the world's map and the marks; without them it shows a bare frame. */
    void load(const SearchSet &files, const std::string &folder);
    void open(Mode mode = Mode::kPark) {
        _open = true;
        _mode = mode;
    }
    Mode mode() const { return _mode; }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** kChose with the cell clicked in target(). */
    WindowResult handle(const Event &ev);
    Park::Cell target() const { return _target; }
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const Park::Cell &camera) const;
    /** A point of the screen. */
    struct Spot {
        int x = 0;
        int y = 0;
    };
    /** Where a cell is on the screen, and the cell under a point of the map. */
    static Spot placeOf(const Park::Cell &cell);
    static Park::Cell cellAt(int x, int y);
    static ScreenRect mapRect();
    static ScreenRect okRect();

private:
    bool _open = false;
    Mode _mode = Mode::kPark;
    Park::Cell _target;
    image::Image _map;
    std::array<image::Image, Attractions::kKinds> _marks; // by Attractions::Kind
};

/**
 * The Overview Map (853), to move between the worlds ("use the Overview
 * Map to move between Land of Invention and Polar Zone", MISSION_DESC 3):
 * each world with its challenges won ("{5}: {22} of {23} challenges
 * completed", UITEXT 491) and whether it's open (494, 495); clicking
 * another open one goes there. Laid out by guess: the recording never
 * opens it.
 */
class OverviewMapWindow {
public:
    /** A world as the player's file and the park stand. */
    struct World {
        int theme = 0; // as Park numbers themes
        bool open = false;
        int won = 0; // its challenges won, of `challenges`
        int challenges = 0;
    };
    void open(std::vector<World> worlds, int here) {
        _open = true;
        _worlds = std::move(worlds);
        _here = here;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** kChose with the world clicked in chosen(): another, open. */
    WindowResult handle(const Event &ev);
    int chosen() const { return _chosen; }
    void draw(Surface &screen, const WindowKit &kit) const;
    /** A world's row, by its place in the list. */
    static ScreenRect rowRect(size_t index);
    static ScreenRect okRect();

private:
    bool _open = false;
    std::vector<World> _worlds;
    int _here = -1;
    int _chosen = -1;
};

/**
 * Save Game and Load Game (UITEXT 777, 778): the player's saves by name
 * and date (779, 780), "New Save" (782) first when saving; a new save
 * named by typing, an old one replaced once the player says yes ("FILE
 * ALREADY EXISTS", 781). Laid out by guess: the recording opens neither.
 * See docs/engines/tpw/games/simcoaster/systems/saving.md.
 */
class SaveLoadWindow {
public:
    enum class Mode { kSave, kLoad };
    /** A save as listed: its name, and its date's text. */
    struct Row {
        std::string name;
        std::string date;
    };
    enum class Control { kNone, kRow, kOk, kCancel };
    void open(Mode mode, std::vector<Row> rows) {
        _open = true;
        _mode = mode;
        _rows = std::move(rows);
        _chosen = -1;
        _naming = false;
        _confirming = false;
        _typed.clear();
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    Mode mode() const { return _mode; }
    /** Whether a new save's name is being typed. */
    bool naming() const { return _naming; }
    /**
     * Takes clicks and typing: kChose once a save's to be written (to
     * chosen(), or a new one, chosen() −1, named typed()) or loaded
     * (chosen()); kClosed on the cross.
     */
    WindowResult handle(const Event &ev);
    /** The save chosen, in the rows; −1 for a new one. */
    int chosen() const { return _chosen; }
    const std::string &typed() const { return _typed; }
    void draw(Surface &screen, const WindowKit &kit) const;
    /** Where a control is: a list row by its place, New Save first when saving. */
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    bool _open = false;
    Mode _mode = Mode::kSave;
    std::vector<Row> _rows;
    int _chosen = -1; // in _rows; −1 New Save, or none chosen when loading
    bool _picked = false;
    bool _naming = false;
    bool _confirming = false;
    std::string _typed;
};

/**
 * The game's menu, over the park on Esc, as the recording shows it at
 * 4:20: Load Game, Save Game, Select A New Player, Go Online, Options,
 * Go to Coaster Design Kit, Resume Game, Quit Game. Loading, saving,
 * resuming and quitting work; the rest are grey.
 */
class GameMenu {
public:
    enum class Choice { kNone, kResume, kQuit, kLoad, kSave };
    void open() {
        _open = true;
        _over = -1;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    Choice handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit) const;
    /** An item's line, top to bottom. */
    static ScreenRect itemRect(size_t item);

private:
    bool _open = false;
    int _over = -1;
};

} // namespace reluva::tpw
