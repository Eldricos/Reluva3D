// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <string>
#include <vector>

// The Hall of Fame (fame.wdf), opened from the main menu: the ten best
// games, each with the player's name, the empire, a description of the
// victory and the score, and Return. What it shows is the manual's
// (chapter 4); how, a screenshot of the game's
// (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** One game in the Hall of Fame. */
struct FameEntry {
    std::string name;        // UTF-8
    int empire = 0;          // 0 Cardassians to 4 Romulans, race.rst's order
    std::string description; // UTF-8, as "Klingons 381 Large Ring Galaxy"
    int score = 0;
};

/** The Hall of Fame. */
class FameScreen : LayoutScreen {
public:
    using LayoutScreen::setSoundHandler;
    static constexpr int kRows = 10;

    FameScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // fame.wdf and fameitem.wdf
    /** The games to show, best first; past kRows aren't shown. */
    void open(std::vector<FameEntry> entries) { _entries = std::move(entries); }
    void draw(Surface &screen) const;
    /** Return, or Return/Escape on the keyboard: true when the screen is left. */
    bool handle(const Event &event);

private:
    WindowLayout _item;
    std::vector<FameEntry> _entries;
};

} // namespace reluva::botf
