// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>

// The empire selection screen (emppick.wdf), after game setup: the five
// major empires, each with its emblem and its civilization level from the
// setup; pointing at one shows its description, clicking chooses it, and
// Accept starts the game as that empire. From the layout, the manual,
// lexicon.dic and a recording of the game (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The empire selection screen: choosing the empire to play. */
class EmpireScreen : LayoutScreen {
public:
    using LayoutScreen::setSoundHandler;
    enum class Result { kOpen, kAccepted, kCancelled };

    EmpireScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load();
    /**
     * Each empire's civilization level, as chosen on the setup screen
     * (0 Beginning ... 4 Advanced), in race.rst's order.
     */
    void setLevels(const std::array<int, 5> &levels) { _levels = levels; }
    void draw(Surface &screen);
    /**
     * The mouse: pointing and clicking on an empire's name or emblem; Accept
     * (once an empire is chosen) and Cancel. Return and Escape do the same.
     */
    Result handle(const Event &event);

    /**
     * The empire chosen, in race.rst's order (0 Cardassians ... 4
     * Romulans), or -1.
     */
    int chosen() const { return _chosen; }
    void reset() { _chosen = _pointed = -1; }

private:
    int empireAt(int x, int y) const;

    std::array<int, 5> _levels{1, 1, 1, 1, 1};
    int _chosen = -1;
    int _pointed = -1;
};

} // namespace reluva::botf
