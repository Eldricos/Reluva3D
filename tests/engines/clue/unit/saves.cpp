// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: Saved games: written and read back.

#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/netplay.h"
#include "engines/clue/resi.h"
#include "engines/clue/resources.h"
#include "engines/clue/rules.h"
#include "engines/clue/ui.h"
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

// A game of computer players saved half way and loaded again goes on
// exactly as the one that wasn't.
static void testSaveLoad() {
    Board board;
    CHECK(!board.build(boardLevel()));
    const std::array<bool, kSuspectCount> playing{true, true, false, true, false, true};
    Game game;
    CHECK(!game.start(board, playing, false, 77));
    std::array<ComputerPlayer, kSuspectCount> ai;
    for (int s = 0; s < kSuspectCount; ++s)
        if (playing[size_t(s)])
            ai[size_t(s)].reset(game, s, 100u + uint32_t(s), s % 3);
    const auto step = [](Game& g, std::array<ComputerPlayer, kSuspectCount>& players,
                         const std::array<bool, kSuspectCount>& in) {
        ComputerPlayer& me = players[size_t(g.turn())];
        switch (me.decide(g)) {
        case ComputerPlayer::Action::Roll: g.rollDie(); break;
        case ComputerPlayer::Action::Passage: g.takePassage(); break;
        case ComputerPlayer::Action::Walk: g.walk(me.chooseWalk(g)); break;
        case ComputerPlayer::Action::Suggest: {
            const Accusation a = me.chooseSuggestion(g);
            g.suggest(a.suspect, a.weapon);
            if (g.phase() == Game::Phase::Disprove)
                g.show(players[size_t(g.disprover())].chooseShow(g, g.turn()));
            for (int s = 0; s < kSuspectCount; ++s)
                if (in[size_t(s)])
                    players[size_t(s)].observe(g);
            break;
        }
        case ComputerPlayer::Action::Accuse: g.accuse(me.chooseAccusation()); break;
        case ComputerPlayer::Action::EndTurn: g.endTurn(); break;
        }
    };
    for (int i = 0; i < 60 && game.phase() != Game::Phase::Over; ++i)
        step(game, ai, playing);
    std::stringstream saved;
    game.save(saved);
    for (int s = 0; s < kSuspectCount; ++s)
        if (playing[size_t(s)])
            ai[size_t(s)].save(saved);
    Game copy;
    std::array<ComputerPlayer, kSuspectCount> copyAi;
    CHECK(!copy.load(saved, board));
    for (int s = 0; s < kSuspectCount; ++s)
        if (playing[size_t(s)])
            CHECK(!copyAi[size_t(s)].load(saved, copy));
    for (int i = 0; i < 20000 && game.phase() != Game::Phase::Over; ++i) {
        step(game, ai, playing);
        step(copy, copyAi, playing);
        CHECK(copy.turn() == game.turn() && copy.phase() == game.phase());
        for (int s = 0; s < kSuspectCount; ++s)
            CHECK(copy.place(s) == game.place(s));
    }
    CHECK(copy.phase() == Game::Phase::Over && copy.winner() == game.winner());
    // Damaged saves are refused.
    std::stringstream bad("game 1\n1 0 9");
    Game broken;
    CHECK(broken.load(bad, board));
    std::stringstream other("notebook 1");
    CHECK(broken.load(other, board));
    std::string text;
    std::stringstream strings;
    saveio::putString(strings, "a name with spaces");
    CHECK(saveio::getString(strings, text) && text == "a name with spaces");
}

static const reluva::test::Suite suite("clue.unit.saves", {
    {"save and load", [](const std::filesystem::path&) { testSaveLoad(); }},
});
