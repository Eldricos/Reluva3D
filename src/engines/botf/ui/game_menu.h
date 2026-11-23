// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/formats/text_table.h"
#include "engines/botf/formats/window_layout.h"
#include "engines/botf/ui/assets.h"

#include <cstdint>

// The menu a right-click opens on the game's screens, how the player moves
// between them (docs/engines/botf/systems/screens.md, "The game menu").

namespace reluva::botf {

/**
 * The game menu: the player's empire's ?marker.wdf, a round picture of six
 * buttons, opened centred on a right-click and kept on the screen. The
 * button under the pointer shows its "on" picture and, with tool tips on,
 * its tip from teachtip.tdb (keyed by its id).
 */
class GameMenu {
public:
    /** Where its buttons lead, by their ids in the layout. */
    enum class Choice {
        kNone = 0,
        kGalaxy = 8001,       // the main galactic screen
        kSystem = 8002,       // the selected system, or the home system
        kDiplomacy = 8003,    // diplomatic events and proposals
        kIntelligence = 8004, // the intelligence agents
        kResearch = 8005,     // research and the encyclopedia
        kMainMenu = 8006,     // the main menu, the game paused
    };

    /** Draws with `assets`, which must outlive it. */
    explicit GameMenu(Assets &assets) : _assets(assets) {}

    /** Reads `empire`'s layout (0 Cardassians to 4 Romulans) and the tool tips, which may be missing. */
    Error load(int empire);
    /** Whether the buttons' tool tips are shown (the options' Tool Tips). */
    void setToolTips(bool shown) { _toolTips = shown; }

    /** Opens it centred on the point (x, y), kept on the 800x600 screen. */
    void open(int x, int y);
    /** Closing it, and whether it's open. */
    ///@{
    void close() { _open = false; }
    bool isOpen() const { return _open; }
    ///@}
    /** Where it is: its layout's corner on the screen. */
    std::pair<int, int> corner() const { return {_x, _y}; }

    /**
     * An event while it's open: a pointer move lights the button under it;
     * a left click on a button chooses it and closes the menu, anywhere else
     * closes it; a right click opens it again there; Escape closes it.
     * @return The button chosen, or kNone.
     */
    Choice handle(const Event &event);
    /** The menu, the button under the pointer lit, and its tip; nothing when closed. */
    void draw(Surface &screen) const;

private:
    // The button at the screen's point (x, y), or 0.
    int32_t buttonAt(int x, int y) const;
    void drawToolTip(Surface &screen) const;

    Assets &_assets;
    WindowLayout _layout;
    TextTable _tips;
    bool _toolTips = true;
    bool _open = false;
    int _x = 0;
    int _y = 0;
    int _pointerX = 0;
    int _pointerY = 0;
    int32_t _pointed = 0; // the button under the pointer, or 0
};

} // namespace reluva::botf
