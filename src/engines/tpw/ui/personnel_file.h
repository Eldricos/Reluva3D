// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"

#include <array>
#include <functional>
#include <string>

namespace reluva::tpw {

/** A player's record, as the Personnel File shows it. */
struct PlayerFile {
    /** A world's line: its name's number in `THEMENAMES.str`, and how its challenges stand. */
    struct World {
        int theme = 0;
        bool open = false;
        int won = 0;
        int challenges = 0;
    };

    std::string name; // empty for a player not yet named
    int rank = 1;     // in PROMOTIONLEVELS.str: 1 "Assistant Manager"
    int goldenTickets = 0;
    int sharesOwned = 0;
    int sharesIssued = 0;
    int sharePrice = 0;
    std::array<World, 3> worlds{};
    int objective = 0;      // in OPERATION.str: 0 "Operation Expand!"
    int objectiveTheme = 0; // where it is, in THEMENAMES.str
};

/**
 * The "SC Personnel File" window of the front end, where a player is
 * chosen, drawn from the game's UI models, fonts and strings at the
 * places a recording shows them. See
 * docs/engines/tpw/games/simcoaster/systems/personnel-file.md.
 */
class PersonnelFile {
public:
    /** What the mouse can be over. */
    enum class Control {
        kNone,
        kTab0,
        kTab1,
        kTab2,
        kTab3,
        kEnterGame,
        kLoadGame,
        kDeletePlayer,
        kQuitGame,
        kCoasterKit
    };

    static constexpr int kPlayers = 4;

    /** Reads what it draws with, and the settings a new player starts from. Fails on the first piece missing. */
    Error load(const SearchSet &files);

    /** A new player's record, from the settings (`levels/Standard.sam`, each world's `global.sam`, `Challenges.sam`). */
    const PlayerFile &newPlayer() const { return _newPlayer; }

    /**
     * Draws the window over what's on the screen (640 × 480, RGBA32): the
     * players' tabs, `selected`'s record, the buttons, `hovered` lit.
     */
    void draw(Surface &screen, const std::array<PlayerFile, kPlayers> &players, int selected, Control hovered) const;

    /** What's at a point of the screen. */
    static Control controlAt(int x, int y);
    /** Where a control is on the screen; empty for kNone. */
    static ScreenRect placeOf(Control control);

    /**
     * Adds typed text to a player's name, as much of it as fits on its
     * tab; a backspace (U+0008) takes its last character off.
     */
    void type(PlayerFile &player, const std::string &utf8) const;

private:
    void drawText(Surface &screen, const Font &font, int x, int y, const std::string &text, Color c) const;
    void drawCentred(Surface &screen, const Font &font, const ScreenRect &in, int y, const std::string &text,
                     Color c) const;
    std::string worldLine(const PlayerFile::World &world) const;

    LoadedModel _window;
    LoadedModel _page;
    LoadedModel _tab;
    LoadedModel _button;
    LoadedModel _bigButton;
    LoadedModel _frame;
    LoadedModel _portraits;
    CharacterSet _characters;
    Font _title;
    Font _small;
    Font _label;
    Font _hand;
    StringTable _ui;
    StringTable _ranks;
    StringTable _operations;
    StringTable _themes;
    PlayerFile _newPlayer;
};

/** Draws what's behind the Personnel File onto a 640 × 480 RGBA32 screen, as it is at the time. */
using Backdrop = std::function<void(Surface &screen)>;

/**
 * The Personnel File over `backdrop`, four new players, the first shown:
 * the mouse lights the buttons, a tab shows its player, typing names the
 * one shown, "Enter Game" goes on with the player shown, "Quit Game"
 * leaves. Without its pieces, goes on at once with a new player's record
 * as the settings give it, or an empty one.
 * @param chosen Gets the player shown when "Enter Game" is clicked.
 * @return True to go on into the game; false to leave it, or if the
 *     window was closed (or the launcher asked for).
 */
bool choosePlayer(System &sys, const SearchSet &files, const Backdrop &backdrop, PlayerFile &chosen);

} // namespace reluva::tpw
