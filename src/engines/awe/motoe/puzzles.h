// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/formats/ac2.h"

#include <vector>

// Murder on the Orient Express's puzzle screens: what each keeps while it's
// open. ScenePage's puzzle hooks (motoe/puzzles.cpp) run them.

namespace reluva::awe::motoe {

/** The torn letter (0x43ec00): its pieces, dragged into place. */
struct TornLetter {
    std::vector<const Ac2Record *> pieces;
    int selected = -1;
    bool dragging = false;
    bool solved = false;
    float topz = 0;
    int dragX = 0;
    int dragY = 0;
};
/** The baggage car's safe (0x434390): its dial, turned to a number. */
struct Safe {
    const Ac2Record *dial = nullptr;
    int spinning = 0; // 1 right, 2 left
    float timer = 0;
    float interval = 0.25f;
    int number = 0;
    int entered = 0;
    int expected = 2;
    bool opening = false; // a tool's step has pulled the handle
};
/** The puzzle box (0x433290): its pieces, slid in order. */
struct PuzzleBox {
    int step = 0;
    int selected = 0;
    int misses = 0;
    int state[5] = {0, 0, 0, 0, 0}; // by piece: 1 upper end, 2 lower end, 3 side, 4 lid
    bool solved = false;
};

} // namespace reluva::awe::motoe
