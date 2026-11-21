// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>
#include <map>
#include <string>

// The single-player game setup screen ("Configuration", gmesetup.wdf),
// drawn over the main menu (opening.wdf) as the game does: each empire's
// civilization level, the game preferences and the galaxy's shape and size.
// What each control is and its choices are from the manual; its words from
// lexicon.dic; how it looks from the layout and a recording of the game
// (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The settings the game setup screen chooses, as the manual gives their defaults. */
struct GameSetup {
    /**
     * Each major empire's civilization level, in race.rst's order:
     * 0 Beginning, 1 Early, 2 Developed, 3 Expanded, 4 Advanced.
     */
    std::array<int, 5> civilization{1, 1, 1, 1, 1};
    int minorRaces = 3;     // 0 none, 1 few, 2 some, 3 many
    int difficulty = 2;     // 0 simple, 1 easy, 2 normal, 3 hard, 4 impossible
    int strategicTimer = 0; // an index into strategicTimerChoices(): 0 none
    int tacticalTimer = 0;  // into tacticalTimerChoices()
    bool randomEvents = true;
    bool manualCombat = true;
    int victory = 0;     // 0 domination, 1 vendetta (team play is for multiplayer games)
    int galaxyShape = 0; // 0 irregular, 1 elliptical, 2 ring, 3 spiral
    int galaxySize = 1;  // 0 small, 1 medium, 2 large
};

/**
 * The game setup Stbof.ini keeps, the last game's
 * (docs/engines/botf/systems/game-files.md); the defaults for what it
 * doesn't have.
 */
GameSetup setupFromIni(const std::map<std::string, std::string> &ini);

/**
 * The setup as the game writes it into Stbof.ini, the last game's: the
 * galaxy, the minor races, the difficulty and each empire's level (the
 * preferences it shares with the options are optionsIniValues').
 */
std::map<std::string, std::string> setupIniValues(const GameSetup &setup);

/** The single-player game setup screen. */
class SetupScreen : LayoutScreen {
public:
    using LayoutScreen::repeating;
    using LayoutScreen::setSoundHandler;
    enum class Result { kOpen, kAccepted, kCancelled };

    /** `lexicon` may be empty: the words are then the English ones. */
    SetupScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // the two layouts
    void draw(Surface &screen);
    /**
     * The time, in milliseconds: a timer's arrow held steps again.
     * @return whether the setup changed
     */
    bool tick(uint64_t millis);
    /**
     * Mouse clicks on the controls (a click changes a setting, as in the
     * game; with Shift, a civilization level changes for every empire), and
     * Return/Escape for Accept/Cancel.
     */
    Result handle(const Event &event);

    const GameSetup &setup() const { return _setup; }
    GameSetup &setup() { return _setup; }

private:
    void click(int32_t id, bool shift);

    GameSetup _setup;
};

/**
 * The civilization level as the screens show it: its number and the word
 * ("2 EARLY").
 */
std::string civilizationLevel(const Lexicon &lexicon, int level);

/** The galaxy's shape as the screens name it: "Irregular" ... "Spiral". */
std::string galaxyShapeName(const Lexicon &lexicon, int shape);

} // namespace reluva::botf
