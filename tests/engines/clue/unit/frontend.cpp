// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: The front end: a dialog drawn from
// dialogInfo.res, and the original's Prefs.ini.

#include "engines/clue/game/ai.h"
#include "engines/clue/game/board.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/formats/resources.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"
#include "tests/engines/clue/support.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;
using namespace reluva::clue::testing;

static void testDialog(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "Data" / "Classic");
    GameData data;
    CHECK(!data.open(tmp, Language::US));
    Resi resi;
    CHECK(!parseResi(resiFile(), resi));
    const Fonts fonts(data, resi); // its animation isn't there: a font without glyphs
    DialogView view(data, fonts, *resi.dialog(7));
    // Group 1 starts hidden; the game shows it.
    CHECK(view.visible(10001) && !view.visible(10000));
    view.setGroupVisible(1, true);
    CHECK(view.visible(10000));
    view.setText(10001, "Oui");
    CHECK(view.text(10001) == "Oui" && view.text(9999) == "Are you sure you wish|to exit Clue?");
    // A click is a press and a release on the same button.
    const auto mouse = [&](EventType type, int x, int y) {
        Event e;
        e.type = type;
        e.mouseX = x;
        e.mouseY = y;
        e.button = MouseButton::Left;
        return view.handleEvent(e);
    };
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 200) == 10001);
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 240) == -1);
    view.setEnabled(10001, false);
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 200) == -1);
    // Typing goes to the edit box that has the focus.
    view.focus(10);
    Event typed;
    typed.type = EventType::TextInput;
    std::snprintf(typed.text, sizeof typed.text, "Jo\xc3\xa9"); // é, kept as Windows-1252
    view.handleEvent(typed);
    CHECK(view.text(10) == "Jo\xe9");
    Event back;
    back.type = EventType::KeyDown;
    back.key = key::Backspace;
    view.handleEvent(back);
    CHECK(view.text(10) == "Jo");
    Surface s;
    s.create(640, 480, PixelFormat::RGBA32);
    view.draw(s); // without its pictures: nothing to draw, nothing to fail
    std::filesystem::remove_all(tmp);
}

// The original's Prefs.ini, as the install has it.
static void testOriginalPrefs() {
    Preferences p;
    CHECK(!p.on[Preferences::AutoNotes] && p.musicVolume == 50); // the original's defaults
    const std::vector<uint8_t> file{1, 0, 1, 1, 1, 0, 1, 0, 3, 0, 0, 0, 10, 0, 0, 0};
    CHECK(p.loadOriginal(file));
    CHECK(p.on[Preferences::SuggestMovies] && !p.on[Preferences::Lighting] && p.on[Preferences::AutoNotes] &&
          !p.on[Preferences::WalkAnimations] && p.on[Preferences::MansionWalls]);
    CHECK(p.musicVolume == 30 && p.masterVolume == 100);
    CHECK(!p.loadOriginal({1, 2, 3}));
}

static const reluva::test::Suite suite("clue.unit.frontend", {
    {"dialog", [](const std::filesystem::path& tmp) { testDialog(tmp / "dialog"); }},
    {"original preferences", [](const std::filesystem::path&) { testOriginalPrefs(); }},
});
