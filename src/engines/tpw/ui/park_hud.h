// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/text.h"
#include "engines/tpw/ui/tooltip.h"
#include "engines/tpw/ui/ui_model.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace reluva::tpw {

/**
 * The park's screen furniture, from the interface's models: the
 * thermometer of the park's mood (empty: its fill isn't drawn yet) and
 * its face, the compass with its turn
 * and zoom buttons, the date's box, the tools down the left and the
 * buttons along the bottom, their menus and the buttons' tooltips. It
 * follows the mouse over its buttons, opens and closes the menus, and
 * says which buttons are held or chosen and which menu item was picked;
 * the money and the date are written over it by the park's screen. See
 * docs/engines/tpw/games/simcoaster/systems/park.md#the-hud.
 */
class ParkHud {
public:
    /** The tools down the left, top to bottom. */
    enum class Tool { kPointer, kPath, kBulldozer, kCamera, kPostcard };
    static constexpr size_t kTools = 5;

    /** The buttons along the bottom, left to right. */
    enum class Gizmo { kMessages, kAttractions, kStaff, kVisitors, kFinance, kPark, kMaps };
    static constexpr size_t kGizmos = 7;

    /** The compass's buttons. */
    enum class CompassButton { kTurnLeft, kTurnRight, kZoomIn, kZoomOut };
    static constexpr size_t kCompassButtons = 4;

    /** The six faces of the park's mood, unhappiest first; the last is a bare park's, a red no-entry sign. */
    static constexpr size_t kMoods = 6;

    /** What the HUD shows besides its buttons' states. */
    struct View {
        size_t mood = kMoods - 1;
        bool canZoomIn = true;
        bool canZoomOut = true;
        int unread = 0; // messages, counted by the envelope
    };
    /** The Message Center (UITEXT 861), picked by the envelope's button, which has no menu. */
    static constexpr uint32_t kMessageCenterItem = 861;

    /** A menu item: Park's Objectives, which shows the mission's briefing again. */
    static constexpr uint32_t kObjectivesItem = 848;
    /** Attractions' Build Attraction, which opens the Buy window; a right click on the button picks it too. */
    static constexpr uint32_t kBuildAttractionItem = 833;
    /** The Staff menu's Hire Staff. */
    static constexpr uint32_t kHireStaffItem = 827;
    /** Attractions' Research Lab, greyed till there's a research room. */
    static constexpr uint32_t kResearchLabMenuItem = 834;
    void setResearchLab(bool there) { _researchLab = there; }

    /** Reads the models, the fonts and the strings. */
    Error load(const SearchSet &files);

    /** Moves its clock on, in milliseconds: menus open over time, tooltips show after a while. */
    void advance(uint32_t ms) { _clock += ms; }

    /**
     * Follows the mouse over the buttons, presses and releases them; a
     * click on a tool chooses it.
     * @return True if the event is the HUD's: the mouse over or on one of
     *     its pieces, which the park beneath shouldn't take.
     */
    bool handle(const Event &ev);

    /** The tool chosen: the pointer at first. */
    Tool tool() const { return _tool; }
    /** Whether a point of the screen is on the HUD rather than the park. */
    bool covers(int x, int y) const;
    /** The tooltips' box, font and strings, for the park's own tips too. */
    const Tooltip &tooltip() const { return _tip; }
    /** Whether one of the bottom buttons' menus is open. */
    bool menuOpen() const { return _menu >= 0; }
    /**
     * The menu item picked since this was last asked, as its string's
     * number in UITEXT (kObjectivesItem), or 0 for none.
     */
    uint32_t takeChoice();
    /** Whether a compass button is held down, as the camera turns and zooms while it is. */
    bool held(CompassButton button) const;

    /** Where a piece is at 640 × 480. */
    ///@{
    static ScreenRect toolRect(Tool tool);
    static ScreenRect gizmoRect(Gizmo gizmo);
    static ScreenRect compassRect(CompassButton button);
    ///@}
    /** The black field of the date's box, where the date is written. */
    static ScreenRect dateField();
    /** A menu's panel when open, and one of its items' lines, or an empty rectangle for a button without a menu. */
    ///@{
    static ScreenRect menuRect(Gizmo gizmo);
    static ScreenRect menuItemRect(Gizmo gizmo, size_t item);
    ///@}
    /** How long a menu takes to rise, in milliseconds; its items show once it has. */
    static constexpr uint32_t kMenuOpenMillis = 300;

    /** Draws it over an RGBA32 surface of 640 × 480. */
    void draw(Surface &screen, const View &view) const;

private:
    // A button: where it is in the list of all of them, to follow the mouse.
    enum class Hit { kNone, kTool, kGizmo, kCompass, kHelp };
    struct Spot {
        Hit kind = Hit::kNone;
        size_t index = 0;
        bool operator==(const Spot &) const = default;
    };
    static Spot spotAt(int x, int y);
    size_t stateOf(const Spot &spot, bool chosen, bool disabled) const;
    bool menuRisen() const { return _menu >= 0 && _clock - _menuOpened >= kMenuOpenMillis; }
    int menuItemAt(int x, int y) const;
    uint32_t tooltipText() const;
    void drawMenu(Surface &screen) const;
    void drawTooltip(Surface &screen) const;

    LoadedModel _gauge;
    LoadedModel _faces;
    LoadedModel _compassPlate;
    LoadedModel _compassDial;
    std::array<LoadedModel, kCompassButtons> _compass;
    LoadedModel _dateBox;
    LoadedModel _toolBar;
    std::array<LoadedModel, kTools> _tools;
    LoadedModel _gizmoBar;
    std::array<LoadedModel, kGizmos> _gizmos;
    LoadedModel _help;
    LoadedModel _menuPanel;
    LoadedModel _menuTail;
    CharacterSet _characters;
    Font _titleFont;
    Font _itemFont;
    StringTable _uiText;
    Tooltip _tip;

    Tool _tool = Tool::kPointer;
    Spot _over;    // under the mouse
    Spot _pressed; // the left button held down on it
    uint32_t _clock = 0;
    uint32_t _overSince = 0; // when the mouse came onto what it's over
    int _mouseX = 0;
    int _mouseY = 0;
    int _menu = -1; // the gizmo whose menu is open
    uint32_t _menuOpened = 0;
    int _menuLit = -1; // its item under the mouse
    uint32_t _choice = 0;
    bool _researchLab = false; // the research room built: its menu item usable
};

} // namespace reluva::tpw
