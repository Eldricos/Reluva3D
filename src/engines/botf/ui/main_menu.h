// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <string>
#include <utility>

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

    using LayoutScreen::pulsing;
    using LayoutScreen::setTime;
    MainMenu(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load();
    /**
     * Whether a game is in progress: without one, Continue, Save Game and
     * Retire can't be used.
     */
    void setGameInProgress(bool inProgress);
    /**
     * The version drawn at the bottom left, as the game draws Stbof.ini's
     * (its "Version" key); none when empty.
     */
    void setVersion(std::string version) { _version = std::move(version); }
    void draw(Surface &screen) const;
    /** The mouse: pointing at a button lights it, and a click on it chooses it. */
    Choice handle(const Event &event);
    /**
     * Coming back from another screen: no button is lit until the pointer
     * moves, as the one it was on when the menu was left may not be under it.
     */
    void forgetPointer() {
        _pointed = 0;
        setPulsing(0);
    }

private:
    std::string _version;
};

} // namespace reluva::botf
