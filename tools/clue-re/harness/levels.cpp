// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game/ai.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/formats/resources.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
using namespace reluva; using namespace reluva::clue;
int main(int argc, char** argv) {
    DataDrive f; f.open(argv[1]); Level lvl; parseLevel(f, lvl); Board board; board.build(lvl);
    const int level = std::atoi(argv[2]), games = 300, players = std::atoi(argv[3]);

    double rounds = 0; int selfNamed = 0, sugg = 0; long within12 = 0;
    for (int g = 0; g < games; ++g) {
        Game game; std::array<bool,6> pl{}; for (int k = 0; k < players; ++k) pl[k] = true; game.start(board, pl, false, uint32_t(g + 1));
        std::array<ComputerPlayer, 6> ai; for (int s = 0; s < 6; ++s) ai[s].reset(game, s, g * 31 + s, level);
        int turns = 0;
        for (int steps = 0; game.phase() != Game::Phase::kOver && steps < 200000; ++steps) {
            auto& me = ai[game.turn()];
            switch (me.decide(game)) {
            case ComputerPlayer::Action::kRoll: game.rollDie(); break;
            case ComputerPlayer::Action::kPassage: game.takePassage(); break;
            case ComputerPlayer::Action::kWalk: game.walk(me.chooseWalk(game)); break;
            case ComputerPlayer::Action::kSuggest: { auto a = me.chooseSuggestion(game); ++sugg; selfNamed += a.suspect == game.turn();
                game.suggest(a.suspect, a.weapon); if (game.phase() == Game::Phase::kDisprove) game.show(ai[game.disprover()].chooseShow(game, game.turn()));
                for (auto& p : ai) p.observe(game); break; }
            case ComputerPlayer::Action::kAccuse: game.accuse(me.chooseAccusation()); break;
            case ComputerPlayer::Action::kEndTurn: game.endTurn(); ++turns; break;
            }
        }
        rounds += double(turns) / players; within12 += turns <= 12 * players;
    }
    std::printf("level %d: %.1f rounds a game, %ld%% won within 12 rounds, self-named %d%% of suggestions\n", level, rounds / games, within12 * 100 / games, selfNamed * 100 / std::max(1, sugg));
}
