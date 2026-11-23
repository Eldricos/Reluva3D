// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/players.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>

namespace reluva::tpw {

/**
 * The "SC Personnel File" window of the front end, where a player is
 * chosen, named or deleted, drawn from the game's UI models, fonts and
 * strings at the places a recording shows them. See
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
        kCoasterKit,
        kNameDone,     // the tick by the name box
        kConfirmYes,   // the delete dialog's tick
        kConfirmNo,    // its cross
        kConfirmClose, // its close button
    };

    /** What the window shows, besides the players. */
    struct View {
        int selected = 0; // the slot shown
        Control hovered = Control::kNone;
        /** For a slot with no player: the name box's text, and whether it's still "Enter name here", selected. */
        std::string typed;
        bool placeholder = true;
        bool confirmingDelete = false; // asking whether to delete the player shown
    };

    /** Reads what it draws with, and the settings a new player starts from. Fails on the first piece missing. */
    Error load(const SearchSet &files);

    /** A new player's record, unnamed, from the settings (`levels/Standard.sam`, each world's `global.sam`, `Challenges.sam`). */
    const PlayerFile &newPlayer() const { return _newPlayer; }

    /** Draws the window over what's on the screen (640 × 480, RGBA32). */
    void draw(Surface &screen, const PlayerSlots &players, const View &view) const;

    /** What's at a point of the screen that can be clicked, as the window shows `players` and `view`. */
    static Control controlAt(int x, int y, const PlayerSlots &players, const View &view);
    /** Where a control is on the screen; empty for kNone. */
    static ScreenRect placeOf(Control control);

    /**
     * Adds typed text to a name, as much of it as fits on a tab; a
     * backspace (U+0008) takes its last character off.
     */
    void type(std::string &name, const std::string &utf8) const;

private:
    void drawText(Surface &screen, const Font &font, int x, int y, const std::string &text, Color c) const;
    void drawCentred(Surface &screen, const Font &font, const ScreenRect &in, int y, const std::string &text,
                     Color c) const;
    std::string worldLine(const PlayerFile::World &world) const;
    void drawRecord(Surface &screen, const PlayerFile &p) const;
    void drawNameBox(Surface &screen, const View &view) const;
    void drawDeleteDialog(Surface &screen, const PlayerFile &p, const View &view) const;

    LoadedModel _window;
    LoadedModel _dialog;
    LoadedModel _page;
    LoadedModel _tab;
    LoadedModel _button;
    LoadedModel _bigButton;
    LoadedModel _frame;
    LoadedModel _portraits;
    LoadedModel _nameBox;
    LoadedModel _nameDone;
    LoadedModel _yes;
    LoadedModel _no;
    LoadedModel _close;
    CharacterSet _characters;
    Font _title;
    Font _small;
    Font _label;
    Font _message;
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
 * The Personnel File over `backdrop`, its players read from and kept in
 * the save folder (none kept without one): the mouse lights the buttons,
 * a tab shows its slot; an empty slot's name box takes a name and its
 * tick makes the player; "Delete Player" asks, then empties the slot;
 * "Enter Game" goes on with the player shown, whose first objective is
 * set if it had none; "Quit Game" leaves. Without its pieces, goes on at
 * once with a new player as the settings give it, or an empty one.
 * @param chosen Gets the player shown when "Enter Game" is clicked.
 * @return True to go on into the game; false to leave it, or if the
 *     window was closed (or the launcher asked for).
 */
bool choosePlayer(System &sys, const SearchSet &files, const std::filesystem::path &saveFolder,
                  const Backdrop &backdrop, PlayerFile &chosen);

} // namespace reluva::tpw
