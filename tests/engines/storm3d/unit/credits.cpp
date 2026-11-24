// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the credits, their script (Credits.txt) and how
// the pages are laid out and timed, as the original's draws and frames
// had them.

#include "engines/storm3d/formats/credits_script.h"
#include "engines/storm3d/shell/credits_screen.h"

#include "tests/support/test.h"

#include <cmath>
#include <map>
#include <string>

using reluva::storm3d::CreditsScreen;
using reluva::storm3d::CreditsScript;

namespace {

// The script's start, as the install's: settings carried from page to
// page, blank lines inside a page kept, an unknown command left out.
void testScript() {
    const std::string text = "@colour=(0.0,0.3,0.6)\r\n@scale=(0.8,0.8,0.8)\r\n@hold=6.0\r\nStar Trek: Armada\r\n"
                             "is dedicated to\r\n@cls\r\nPROJECT LEADS\r\n\r\nDirector\r\n\r\n@cls\r\n@wobble=1\r\n"
                             "@hold=11.0\r\n@scale=(0.5,0.5,0.5)\r\nPROGRAMMING\r\n@cls\r\n@cls\r\n";
    const CreditsScript s = CreditsScript::parse(text);
    CHECK(s.pages.size() == 3);
    CHECK((s.pages[0].lines == std::vector<std::string>{"Star Trek: Armada", "is dedicated to"}));
    CHECK(s.pages[0].hold == 6 && s.pages[0].scale[0] == 0.8f && s.pages[0].colour[2] == 0.6f);
    CHECK((s.pages[1].lines == std::vector<std::string>{"PROJECT LEADS", "", "Director"}));
    CHECK(s.pages[1].hold == 6 && s.pages[1].colour[1] == 0.3f);
    CHECK(s.pages[2].hold == 11 && s.pages[2].scale[0] == 0.5f);
}

// A page's turn: 3 s of nothing, a second fading in, the hold, then 3 s
// going away, its size 1 / (1 + seconds), the last second fading out.
void testTiming() {
    CreditsScript::Page page;
    page.hold = 6;
    CHECK(CreditsScreen::momentAt(page, 2).alpha == 0);
    CHECK(std::abs(CreditsScreen::momentAt(page, 3.5).alpha - 0.5f) < 1e-5f);
    CHECK(CreditsScreen::momentAt(page, 9).alpha == 1 && CreditsScreen::momentAt(page, 9).scale == 1);
    const CreditsScreen::Moment away = CreditsScreen::momentAt(page, 11);
    CHECK(away.alpha == 1 && std::abs(away.scale - 0.5f) < 1e-5f);
    CHECK(std::abs(CreditsScreen::momentAt(page, 12.5).alpha - 0.5f) < 1e-5f);
    CHECK(!CreditsScreen::momentAt(page, 12.9).over && CreditsScreen::momentAt(page, 13).over);
}

// Two letters a unit wide as the font model has them, at its baseline:
// placed as the original drew its pages: a line centred across the
// screen with the 7.19-pixel gap after each letter counted, a space 7
// units; the lines 7.19 pixels and 10.755 units apart, the block's
// middle baseline 0.67 + 8.72 x scale pixels below the middle.
void testLayout() {
    std::map<char, CreditsScreen::Glyph> font;
    for (char c : {'A', 'B'}) {
        CreditsScreen::Glyph g;
        g.left = c == 'A' ? 100 : 200;
        g.width = 1;
        g.corners = {{g.left, 84.403f}, {g.left + 1, 84.403f}, {g.left, 85.403f}};
        font[c] = g;
    }
    for (float scale : {0.8f, 0.5f}) {
        CreditsScript::Page page;
        page.scale = {scale, scale, scale};
        page.lines = {"A B", "A"};
        const auto placed = CreditsScreen::layout(page, font);
        CHECK(placed.size() == 3);
        const float k = 3.084f * scale;
        const float width = (1 * k + 7.19f) * 2 + 7 * k + 7.19f;
        CHECK(std::abs(placed[0].corners[0][0] - (319.5f - width / 2)) < 1e-3f);
        CHECK(std::abs(placed[1].corners[0][0] - placed[0].corners[0][0] - (8 * k + 2 * 7.19f)) < 1e-3f);
        const float pitch = 7.19f + 10.755f * k;
        CHECK(std::abs(placed[0].corners[0][1] - (239.5f - pitch / 2 + 0.67f + 8.72f * scale)) < 1e-3f);
        CHECK(std::abs(placed[2].corners[0][1] - placed[0].corners[0][1] - pitch) < 1e-3f);
        CHECK(std::abs(placed[0].corners[2][1] - (placed[0].corners[0][1] - k)) < 1e-3f);
    }
    // The first page's colour, (0, 0.3, 0.6), drawn as (128, 168, 208).
    const auto lit = CreditsScreen::litColour({0, 0.3f, 0.6f});
    CHECK(std::lround(lit[0] * 255) == 128 && std::lround(lit[1] * 255) == 168 && std::lround(lit[2] * 255) == 208);
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.credits",
                                        {
                                            {"script", [](const std::filesystem::path &) { testScript(); }},
                                            {"timing", [](const std::filesystem::path &) { testTiming(); }},
                                            {"layout", [](const std::filesystem::path &) { testLayout(); }},
                                        });
