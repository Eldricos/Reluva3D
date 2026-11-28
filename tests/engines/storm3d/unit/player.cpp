// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the player's files, its preferences (ARMADA.PRF)
// and progress (shell.set), as the game wrote them under the emulator.

#include "engines/storm3d/formats/preferences.h"
#include "engines/storm3d/formats/shell_progress.h"

#include "tests/support/test.h"

#include <string>
#include <vector>

using reluva::storm3d::Preferences;
using reluva::storm3d::ShellProgress;

namespace {

// After Stereo Sound was turned off and the game quit.
const std::string kWritten = "9404509\r\n2 3 1 6 1 1 0\r\n10 10 10 8 0 1 1\r\nReluva DirectDraw\r\n"
                             "0.5 0.5 5 5 5 4 640 480 16 1 0\r\n0\r\n";

void testPreferences() {
    Preferences p = Preferences::parse(kWritten, "Reluva");
    CHECK(p.text() == kWritten);
    CHECK(p.number(Preferences::kGame, 0) == 2);
    CHECK(p.number(Preferences::kSound, 4) == 0);
    CHECK(p.number(Preferences::kGraphics, 1) == 0.5);
    CHECK(p.number(Preferences::kGraphics, 8) == 16);
    CHECK(p.line(Preferences::kDriver) == "Reluva DirectDraw");
    // Written as the game writes them: a tenth as "0.9", a whole as "10".
    p.setNumber(Preferences::kGame, 2, 0.9);
    p.setNumber(Preferences::kGame, 3, 10);
    CHECK(p.line(Preferences::kGame) == "2 3 0.9 10 1 1 0");
    // A new player's, the driver theirs.
    const Preferences fresh = Preferences::defaults("Reluva");
    CHECK(fresh.line(Preferences::kSound) == "10 10 10 8 1 1 1");
    CHECK(fresh.line(Preferences::kDriver) == "Reluva");
    CHECK(Preferences::parse("1\r\n", "Reluva").line(Preferences::kSound) == fresh.line(Preferences::kSound));
}

// The install's shell.set (80 bytes, "Player"), and one with the
// Federation finished and three Klingon missions open.
void testProgress() {
    std::vector<uint8_t> file(80, 0);
    file[12] = 1;
    for (size_t i = 0; i < 6; ++i)
        file[16 + i] = uint8_t("Player"[i]);
    ShellProgress p;
    CHECK(!p.parse(file));
    CHECK(p.player == "Player" && p.flags[2] == 1 && !p.hasLogin);
    CHECK(p.open(0) && !p.open(1));
    CHECK(p.bytes() == file);
    p.finished[0] = true;
    p.lastMission[1] = 2;
    CHECK(p.open(1) && !p.open(2));
    // A newline byte goes out as CR LF and comes back as one.
    p.lastMission[2] = '\n';
    const std::vector<uint8_t> written = p.bytes();
    CHECK(written.size() == 81 && written[2] == '\r' && written[3] == '\n');
    ShellProgress back;
    CHECK(!back.parse(written));
    CHECK(back.lastMission[2] == '\n' && back.lastMission[1] == 2 && back.finished[0]);
    CHECK(ShellProgress{}.parse(std::vector<uint8_t>(10, 0)));
}

// A mission won: the campaign's next opens, its last finishes it, and the
// Borg's finished opens the finale.
void testProgressWon() {
    ShellProgress p;
    CHECK(!p.open(4));
    p.won(0, 0, 4);
    CHECK(p.lastMission[0] == 1 && !p.finished[0]);
    p.won(0, 0, 4); // one played again opens nothing
    CHECK(p.lastMission[0] == 1);
    p.lastMission[0] = 3;
    p.won(0, 3, 4);
    CHECK(p.finished[0] && p.open(1) && !p.open(4) && p.lastCampaign == 1);
    p.lastMission[3] = 3;
    p.won(3, 3, 4);
    CHECK(p.finished[3] && p.open(4));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.player",
                                        {
                                            {"preferences", [](const std::filesystem::path &) { testPreferences(); }},
                                            {"progress", [](const std::filesystem::path &) { testProgress(); }},
                                            {"progress won", [](const std::filesystem::path &) { testProgressWon(); }},
                                        });
