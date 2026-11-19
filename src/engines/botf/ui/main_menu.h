// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

// The main menu (opening.wdf): the first screen after the opening movie,
// from which a game is started, loaded, saved or retired, the options and
// the Hall of Fame are opened, and the game is left. What each button does
// is the manual's (chapter 4); how they look, a recording's
// (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The main menu: nine buttons, some of which can be used only with a game in progress. */
class MainMenu : LayoutScreen {
public:
    /** What the player chose. */
    enum class Choice {
        kNone,
        kContinue,
        kNewSinglePlayer,
        kNewMultiplayer,
        kLoadGame,
        kSaveGame,
        kOptions,
        kHallOfFame,
        kRetire,
        kQuit,
    };

    MainMenu(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) { _highlightPointed = true; }
    Error load();
    /**
     * Whether a game is in progress: without one, Continue, Save Game and
     * Retire can't be used.
     */
    void setGameInProgress(bool inProgress);
    void draw(Surface &screen) const { drawLayouts(screen); }
    /** The mouse: pointing at a button lights it, and a click on it chooses it. */
    Choice handle(const Event &event);
};

} // namespace reluva::botf
