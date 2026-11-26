// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/hire_window.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/object_names.h"
#include "engines/tpw/ui/ui_model.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * What the park's smaller windows share: the interface's pieces, its fonts
 * and strings, and ways to draw them. See
 * docs/engines/tpw/games/simcoaster/systems/staff.md#the-windows.
 */
class WindowKit {
public:
    /** Reads the models, the bars' picture, the fonts and UITEXT and THOUGHTS. */
    Error load(const SearchSet &files);

    /** A control's meshes, in the order the models name them. */
    static constexpr size_t kPlain = 0;
    static constexpr size_t kGreyed = 1;
    static constexpr size_t kLit = 2;

    /** A window's frame and its title. */
    void window(Surface &screen, const ScreenRect &r, const std::string &title) const;
    /** A sunken frame, black within. */
    void frame(Surface &screen, const ScreenRect &r) const;
    /** The menus' see-through panel. */
    void panel(Surface &screen, const ScreenRect &r) const;
    /** A line in the titles' font. */
    void title(Surface &screen, int x, int y, const std::string &s, Color c) const;
    /** A day as the HUD's date shows it: "14.Jan.00". */
    std::string date(uint32_t day) const;
    /** A line of text, in the window's small font. */
    void text(Surface &screen, int x, int y, const std::string &s, Color c = Color{255, 255, 255}) const;
    int textWidth(const std::string &s) const;
    /**
     * Text broken into lines at its line breaks and between words to fit `width`, from (x, y).
     * @return the y below its last line
     */
    int paragraph(Surface &screen, int x, int y, int width, const std::string &s) const;
    /** A rating as a bar, 0 to 1: the stat bars' picture's red to green. */
    void bar(Surface &screen, int x, int y, int width, float level) const;
    /** A button: minus, plus, the door, fire, train, OK, cancel, close, the arrows. */
    enum class Button { kMinus, kPlus, kDoor, kFire, kTrain, kOk, kCancel, kClose, kLeft, kRight };
    void button(Surface &screen, Button b, const ScreenRect &r, size_t state) const;
    /** A column's header button with its label. */
    void header(Surface &screen, const ScreenRect &r, const std::string &label) const;
    /** A tab of the staff's kinds. */
    void staffTab(Surface &screen, Park::StaffKind kind, const ScreenRect &r, size_t state) const;

    /** UITEXT's string, with its parameters filled. */
    std::string ui(uint32_t index) const { return _ui.text(index); }
    std::string ui(uint32_t index, const std::map<int, std::string> &values) const;
    /** THOUGHTS.str's string. */
    std::string thought(uint32_t index) const { return _thoughts.text(index); }
    /** A kind of staff's name, as STAFF_TYPES.str has it: "Engineer". */
    std::string staffType(Park::StaffKind kind) const;
    /** What a member of staff is doing, as STAFFSTATES.str has it: "Resting". */
    std::string staffState(const Park::StaffMember &m) const;
    /** TAG_SYSTEM.str's string, the park's messages, with its parameters filled. */
    std::string tag(uint32_t index, const std::map<int, std::string> &values = {}) const;
    /** A lender's name, LOANNAMES.str's: "Cash Unlimited". */
    std::string lender(int index) const { return _lenders.text(uint32_t(index)); }
    /** What a visitor's doing, as KIDSTATES.str has it: 6 "Doing nothing". */
    std::string kidState(uint32_t index, const std::map<int, std::string> &values = {}) const;
    /** One of the interface's other models (`ui/<name>.MD2`, read with the rest), a mesh of it. */
    void piece(Surface &screen, const std::string &name, size_t mesh, const ScreenRect &r) const;
    /** A line in the big menus' font (MENUBIG), and its width. */
    void menuText(Surface &screen, int x, int y, const std::string &s, Color c) const;
    int menuTextWidth(const std::string &s) const;
    /** A rectangle filled with a colour. */
    static void fill(Surface &screen, const ScreenRect &r, Color c);
    /** Money as the windows show it: "$ 500", "-$ 500". */
    static std::string money(int amount);
    /** Whether an event is a left click. */
    static bool isClick(const Event &ev);
    /** The windows' list rows' height, and a chosen row's colour. */
    static constexpr int kRowHeight = 13;
    static constexpr Color kChosenRow{106, 125, 150};
    /** The president's letter on a mission done, OBJECTIVE_COMPLETE.str's, by the mission. */
    std::string objectiveLetter(int mission) const { return _objectiveLetters.text(uint32_t(mission)); }
    /** A park's message as it reads: its title; its text and lines, and the challenge it's about. */
    std::string messageTitle(const Park::Message &m) const;
    std::vector<std::string> messageText(const Park::Message &m) const;
    /** An ability's name, ABILITIES.str's: 10 "Water/Drainage". */
    std::string ability(int index) const { return _abilities.text(uint32_t(index)); }
    /** A challenge's title, CHALLENGE_TITLES.str's, by its number. */
    std::string challengeTitle(int number) const { return _challengeTitles.text(uint32_t(number)); }
    /** A world's name, THEMENAMES.str's, by the missions' theme number (Park::theme): 1 "Land of Invention". */
    std::string worldName(int theme) const { return _worlds.text(uint32_t(Park::themeName(theme))); }
    /** A rank, PROMOTIONLEVELS.str's: 1 "Assistant Manager". */
    std::string rank(int index) const { return _ranks.text(uint32_t(index)); }

private:
    LoadedModel _window;
    LoadedModel _frame;
    LoadedModel _header;
    std::array<LoadedModel, 10> _buttons;
    std::array<LoadedModel, Park::kStaffKinds> _tabs;
    image::Image _bar;
    CharacterSet _characters;
    Font _titleFont;
    Font _font;
    StringTable _ui;
    StringTable _thoughts;
    StringTable _months;
    StringTable _staffTypes;
    StringTable _staffStates;
    StringTable _tags;
    StringTable _lenders;
    StringTable _ranks;
    StringTable _objectiveLetters;
    StringTable _kidStates;
    StringTable _worlds;
    StringTable _challengeTitles;
    StringTable _abilities;
    Font _menuFont;
    std::map<std::string, LoadedModel> _pieces;
    LoadedModel _panel;
};

/** What a click in one of these windows did. */
enum class WindowResult { kNone, kClosed, kChose };

/**
 * A right-click menu of an attraction, a member of staff, the gates or a
 * plant: its title and items (UITEXT's), at the point clicked, as the
 * recording's at 11:16.
 */
class PopupMenu {
public:
    /** Opens it; its items' parameters filled from `values` ({25}, the cost of another; {27}, a scrap value). */
    void open(std::string title, std::vector<uint32_t> items, int x, int y, std::map<int, std::string> values = {});
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** Takes a mouse event; a click on an item chooses it, one elsewhere closes the menu. */
    WindowResult handle(const Event &ev);
    /** The item chosen, its string's number. */
    uint32_t chosen() const { return _chosen; }
    void draw(Surface &screen, const WindowKit &kit) const;
    /** An item's line, at 640 × 480. */
    ScreenRect itemRect(size_t item) const;

private:
    ScreenRect panel() const;
    bool _open = false;
    std::string _title;
    std::vector<uint32_t> _items;
    std::map<int, std::string> _values;
    int _x = 0;
    int _y = 0;
    int _lit = -1;
    uint32_t _chosen = 0;
};

/**
 * An attraction's window, as the recording's at 11:18: its model turning,
 * its ratings and accounts, and its price, which − and + change; the door
 * opens and closes it.
 */
class AttractionWindow {
public:
    enum class Control { kNone, kMinus, kPlus, kDoor, kOk, kClose };
    void open(const Park &park, size_t object, std::string name, const LoadedModel *model);
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    size_t object() const { return _object; }
    /** Takes a mouse event, changing the park's price and opening as clicked. */
    WindowResult handle(const Event &ev, Park &park);
    void advance(uint32_t ms) { _clock += ms; }
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    size_t _object = 0;
    std::string _name;
    const LoadedModel *_model = nullptr;
    Control _over = Control::kNone;
    uint32_t _clock = 0;
    mutable soft3d::Rasterizer _rasterizer;
};

/** The gates' window: the park's entry price, and the door opening and closing the park. */
class GateWindow {
public:
    enum class Control { kNone, kMinus, kPlus, kDoor, kOk, kClose };
    void open() { _open = true; }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    Control _over = Control::kNone;
};

/**
 * A member of staff's window: their ratings and wage, which − and +
 * change; train sends them for training, fire lets them go.
 */
class StaffWindow {
public:
    enum class Control { kNone, kMinus, kPlus, kTrain, kFire, kOk, kClose };
    void open(int staff) {
        _open = true;
        _staff = staff;
        _message = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    int staff() const { return _staff; }
    WindowResult handle(const Event &ev, Park &park);
    void advance(uint32_t ms) { _clock += ms; }
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    int _staff = 0;
    uint32_t _message = 0; // a training refusal's, UITEXT's
    Control _over = Control::kNone;
    uint32_t _clock = 0;
};

/**
 * The staff listed by kind (List All Staff), their wages, skill and
 * happiness; a row clicked is chosen, to open its window.
 */
class StaffListWindow {
public:
    enum class Control { kNone, kTab, kRow, kOk, kClose };
    void open(Park::StaffKind kind) {
        _open = true;
        _kind = kind;
        _chosen = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** The member chosen: 0 for none. */
    int chosen() const { return _chosen; }
    WindowResult handle(const Event &ev, const Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    std::vector<int> listed(const Park &park) const;
    bool _open = false;
    Park::StaffKind _kind = Park::StaffKind::kEngineer;
    int _chosen = 0;
    Control _over = Control::kNone;
    size_t _overIndex = 0;
};

/** The training room's window: those in training, how able, how far through. */
class TrainingWindow {
public:
    void open() { _open = true; }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const;
    static ScreenRect okRect();

private:
    bool _open = false;
};

/** The staff's finance controls: a kind's wages set, changed by so much, or by a percentage. */
class StaffFinanceWindow {
public:
    enum class Control { kNone, kTab, kLeft, kRight, kMinus, kPlus, kOk, kClose };
    void open() {
        _open = true;
        _amount = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    bool _open = false;
    Park::StaffKind _kind = Park::StaffKind::kEngineer;
    Park::Adjustment _how = Park::Adjustment::kChangeBy;
    int _amount = 0;
    Control _over = Control::kNone;
    size_t _overIndex = 0;
};

/**
 * A wage demand (UITEXT 251 to 259): what they want, the player's offer,
 * which − and + change, and how it's taken; refused, accept the demand
 * or let them go.
 */
class WageDemandWindow {
public:
    enum class Control { kNone, kMinus, kPlus, kOk, kAccept, kLetGo };
    void open(int raise) {
        _open = true;
        _offer = raise;
        _answer = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    int _offer = 0;
    uint32_t _answer = 0; // UITEXT's 255 to 257 once offered
    Control _over = Control::kNone;
};

/**
 * The Research Lab (UITEXT 285), as the recording's at 14:52: its two
 * projects, each with how far on, the days to go, what's left to spend
 * and its scientists, Edit Project and Cancel Project; Start New Project;
 * the work rate, which − and + change.
 */
class ResearchLabWindow {
public:
    enum class Control { kNone, kEdit, kCancel, kStart, kRateDown, kRateUp, kOk, kClose };
    void open() { _open = true; }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    /** What a click asks for beyond the lab itself: a project to choose, or one's staff to assign. */
    enum class Wants { kNothing, kChoose, kAssign };
    Wants handle(const Event &ev, Park &park);
    /** The slot an Edit Project or Start New Project was for. */
    size_t slot() const { return _slot; }
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &staffNames,
              const ObjectNames &names) const;
    static ScreenRect placeOf(Control control, size_t slot = 0);

private:
    bool _open = false;
    size_t _slot = 0;
    Control _over = Control::kNone;
    size_t _overSlot = 0;
};

/**
 * Choosing a project (UITEXT 307), as the recording's at 14:42: each
 * with its type, the consultant it needs, a bar of its time, and its
 * cost; the tick starts the one chosen.
 */
class ProjectWindow {
public:
    enum class Control { kNone, kRow, kOk, kCancel };
    void open(size_t slot) {
        _open = true;
        _slot = slot;
        _row = 0;
        _message = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const ObjectNames &names) const;
    static ScreenRect placeOf(Control control, size_t row = 0);

private:
    bool _open = false;
    size_t _slot = 0;
    size_t _row = 0;
    Control _over = Control::kNone;
    uint32_t _message = 0; // why the chosen can't be started, UITEXT's; 0 for none
};

/**
 * Assigning scientists to a project (UITEXT 321): those available and
 * those assigned, a row chosen moved across by the arrows; its
 * consultant, or none needed.
 */
class AssignWindow {
public:
    enum class Control { kNone, kAvailableRow, kAssignedRow, kAssign, kUnassign, kOk };
    void open(size_t slot) {
        _open = true;
        _slot = slot;
        _chosen = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &staffNames,
              const ObjectNames &names) const;
    static ScreenRect placeOf(Control control, size_t row = 0);

private:
    std::vector<int> available(const Park &park) const;
    std::vector<int> assigned(const Park &park) const;
    bool _open = false;
    size_t _slot = 0;
    int _chosen = 0;
    Control _over = Control::kNone;
};

/** An attraction's financial summary (UITEXT 45, 170 to 178): last month's takings, costs and profit, and to date. */
class FinanceSummaryWindow {
public:
    void open(size_t object, std::string name) {
        _open = true;
        _object = object;
        _name = std::move(name);
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect okRect();

private:
    bool _open = false;
    size_t _object = 0;
    std::string _name;
};

/** Naming an attraction (Edit name): what's typed, Backspace, the tick or Return to keep it. */
class RenameWindow {
public:
    void open(size_t object, std::string name) {
        _open = true;
        _object = object;
        _typed = std::move(name);
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    size_t object() const { return _object; }
    const std::string &typed() const { return _typed; }
    /** Takes typing and clicks: kChose once the name's kept. */
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit) const;
    static ScreenRect okRect();

private:
    bool _open = false;
    size_t _object = 0;
    std::string _typed;
};

/**
 * Financial Statistics (UITEXT 386 to 402): the bank balance and the
 * park's value, this year's income and expenditure by line, and last
 * month's in summary.
 */
class FinanceStatsWindow {
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
 * Loans (UITEXT 403 to 421): those available and those outstanding, each
 * a list; a row chosen is taken or repaid, once confirmed.
 */
class LoansWindow {
public:
    enum class Control { kNone, kAvailable, kOutstanding, kRow, kAct, kYes, kNo, kOk, kClose };
    void open() {
        _open = true;
        _outstanding = false;
        _row = 0;
        _asking = Asking::kNothing;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control, size_t row = 0);

private:
    // The confirmation over it: to take a loan, to repay one, or that it can't be.
    enum class Asking { kNothing, kTake, kRepay, kCantRepay };
    bool _open = false;
    bool _outstanding = false; // which list shows
    size_t _row = 0;
    Asking _asking = Asking::kNothing;
    Control _over = Control::kNone;
};

/** The Share Portfolio (UITEXT 466 to 470): the share price, the player's shares, their value and stake. */
class PortfolioWindow {
public:
    void open(std::string owner, int rank) {
        _open = true;
        _owner = std::move(owner);
        _rank = rank;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect okRect();

private:
    bool _open = false;
    std::string _owner; // empty for none known
    int _rank = 1;
};

/**
 * Attraction Finance Controls (UITEXT 179 to 189): a kind of price, as
 * StaffFinanceWindow does the wages, set, changed by so much or by a
 * percentage.
 */
class AttractionFinanceWindow {
public:
    enum class Control { kNone, kKindLeft, kKindRight, kLeft, kRight, kMinus, kPlus, kOk, kClose };
    void open() {
        _open = true;
        _amount = 0;
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    WindowResult handle(const Event &ev, Park &park);
    void draw(Surface &screen, const WindowKit &kit, const Park &park) const;
    static ScreenRect placeOf(Control control);

private:
    bool _open = false;
    Park::PriceKind _kind = Park::PriceKind::kRide;
    Park::Adjustment _how = Park::Adjustment::kChangeBy;
    int _amount = 0;
    Control _over = Control::kNone;
};

/** A message to the player, TAG_SYSTEM's: its title, its text and lines; the tick puts it away. */
class MessageWindow {
public:
    void open(Park::Message message) {
        _open = true;
        _message = std::move(message);
    }
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    const Park::Message &message() const { return _message; }
    WindowResult handle(const Event &ev);
    /** Draws it, as tall as its text; the tick at its foot. */
    void draw(Surface &screen, const WindowKit &kit) const;
    /** The tick, where it was last drawn. */
    ScreenRect okRect() const;

private:
    bool _open = false;
    Park::Message _message;
    mutable int _height = 0; // as last drawn
};

} // namespace reluva::tpw
