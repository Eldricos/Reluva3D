// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the front end's main menu, its buttons where the
// game's frame has them.

#include "engines/storm3d/shell/main_menu.h"

#include "tests/support/test.h"

#include <array>
#include <vector>

using reluva::storm3d::MainMenu;
using reluva::storm3d::MenuChoice;

namespace {

// The labels' and the panels' sizes as the install's bitmaps and movies
// have them, in buttons()' order.
const std::vector<std::array<int, 4>> kSizes = {
    {136, 16, 226, 180}, {137, 16, 226, 174}, {100, 16, 370, 160}, {84, 15, 200, 200}, {67, 16, 0, 0}, {140, 16, 0, 0},
};

MenuChoice at(int x, int y) {
    const int i = MainMenu::buttonAt(x, y, kSizes);
    return i < 0 ? MenuChoice::kNone : MainMenu::buttons()[size_t(i)].choice;
}

// A label is its button, and so is its panel: the game's frame lit
// Options with the pointer over the construction yard.
void testButtons() {
    CHECK(MainMenu::buttons().size() == kSizes.size());
    CHECK(at(90, 18) == MenuChoice::kSinglePlayer);
    CHECK(at(100, 100) == MenuChoice::kSinglePlayer);
    CHECK(at(552, 18) == MenuChoice::kMultiplayer);
    CHECK(at(300, 300) == MenuChoice::kOptions);
    CHECK(at(450, 300) == MenuChoice::kCredits);
    CHECK(at(470, 458) == MenuChoice::kReplayIntro);
    // Quit's label is over the Credits panel's bottom rows: Quit.
    CHECK(at(560, 416) == MenuChoice::kQuit);
    CHECK(at(320, 10) == MenuChoice::kNone);
    CHECK(at(639, 479) == MenuChoice::kNone);
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.shell",
                                        {
                                            {"buttons", [](const std::filesystem::path &) { testButtons(); }},
                                        });
