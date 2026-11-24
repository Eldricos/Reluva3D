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
    /** THOUGHTS.str's string: the staff's states among them. */
    std::string thought(uint32_t index) const { return _thoughts.text(index); }

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
    void open(std::string title, std::vector<uint32_t> items, int x, int y);
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

} // namespace reluva::tpw
