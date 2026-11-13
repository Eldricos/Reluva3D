// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Clue commands for its board: a level's placements, the
// board it lays out, and games of computer players on it
// (docs/engines/clue/formats/board.md).

#include "common/fs.h"
#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/resi.h"
#include "engines/clue/resources.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

int cmdClueLevel(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool clue level <clue.lvl> [-v]\n");
        return 2;
    }
    reluva::clue::DataDrive file;
    reluva::clue::Level level;
    if (reluva::Error err = file.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (reluva::Error err = reluva::clue::parseLevel(file, level)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const bool verbose = argc > 3 && std::strcmp(argv[3], "-v") == 0;
    std::printf("\"%s\": %zu object names, %zu placements\n", level.title.c_str(), level.objects.size(),
                level.placements.size());
    std::map<int, int> layers, flags;
    for (const auto& pl : level.placements) {
        ++layers[pl.layer];
        ++flags[pl.flags];
        if (verbose) {
            std::printf("%-36s layer %d  at %5d,%5d  z %4d  flags %02x", level.objects[pl.object].c_str(), pl.layer,
                        pl.x, pl.y, pl.z, pl.flags);
            if (pl.flags & 0x02)
                std::printf("  tag %u", pl.tag);
            for (const auto& t : pl.timings)
                std::printf("  [%u ms -> %u]", t.time, t.state);
            std::printf("\n");
        }
    }
    std::printf("by layer:");
    for (const auto& [l, n] : layers)
        std::printf("  %d: %d", l, n);
    std::printf("\nby flags:");
    for (const auto& [f, n] : flags)
        std::printf("  %02x: %d", f, n);
    std::printf("\n");
    return 0;
}

// The board clue.lvl lays out, drawn in characters: '.' corridors, a room's
// letter, '#' its doors' squares, a digit a suspect's start.
int cmdClueBoard(int argc, char** argv) {
    if (argc != 3 && argc != 4 && argc != 6) {
        std::fprintf(stderr, "usage: reluva-tool clue board <clue.lvl> [games [novice|normal|expert players]]\n");
        return 2;
    }
    reluva::clue::DataDrive file;
    reluva::clue::Level level;
    reluva::clue::Board board;
    reluva::Error err = file.open(argv[2]);
    if (!err)
        err = reluva::clue::parseLevel(file, level);
    if (!err)
        err = board.build(level);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    static const char* const kRooms[] = {"Hall", "Lounge", "Dining Room", "Kitchen", "Ballroom",
                                         "Conservatory", "Billiard Room", "Library", "Study"};
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -x0, y1 = -y0;
    const auto extend = [&](const reluva::clue::Square& s) {
        x0 = std::min(x0, s.x), y0 = std::min(y0, s.y), x1 = std::max(x1, s.x), y1 = std::max(y1, s.y);
    };
    for (const auto& s : board.corridor())
        extend(s);
    for (int r = 0; r < reluva::clue::kRoomCount; ++r)
        for (const auto& s : board.roomSquares(r))
            extend(s);
    std::set<reluva::clue::Square> doorSquares;
    for (const auto& d : board.doors())
        doorSquares.insert(d.inside);
    for (int y = y0; y <= y1; ++y) {
        std::string line;
        for (int x = x0; x <= x1; ++x) {
            const reluva::clue::Square s{x, y};
            char c = ' ';
            if (board.isCorridor(s))
                c = '.';
            else if (int r = board.roomAt(s); r != reluva::clue::kNoRoom)
                c = doorSquares.count(s) ? '#' : char('A' + r);
            for (int k = 0; k < reluva::clue::kSuspectCount; ++k)
                if (board.start(k) == s)
                    c = char('0' + k);
            line += c;
        }
        std::printf("%3d %s\n", y, line.c_str());
    }
    std::printf("columns %d to %d; %zu corridor squares\n", x0, x1, board.corridor().size());
    for (int r = 0; r < reluva::clue::kRoomCount; ++r) {
        int doors = 0;
        for (const auto& d : board.doors())
            doors += d.room == r;
        const int to = board.passage(r);
        std::printf("%c %-13s %3zu squares, %d doors%s%s\n", 'A' + r, kRooms[r], board.roomSquares(r).size(), doors,
                    to == reluva::clue::kNoRoom ? "" : ", a passage to the ", to == reluva::clue::kNoRoom ? "" : kRooms[to]);
    }
    for (int k = 0; k < reluva::clue::kSuspectCount; ++k)
        std::printf("suspect %d starts at %d,%d\n", k, board.start(k).x, board.start(k).y);
    // Games of six computer players on it, alternately with the die and without.
    const int games = argc > 3 ? std::atoi(argv[3]) : 0;
    // Or all of one level, so many of them: "novice 3", "expert 5" ...
    int onlyLevel = -1, players = 6;
    if (argc > 5) {
        const std::string lv = argv[4];
        onlyLevel = lv == "novice" ? 0 : lv == "normal" ? 1 : 2;
        players = std::clamp(std::atoi(argv[5]), 3, 6);
    }
    int turns = 0, unfinished = 0, wrong = 0, suggestions = 0, selfNamed = 0, within12 = 0;
    // As a recording can show it: games whose first 15 suggestions have no
    // one naming itself.
    int noSelfIn15 = 0;
    std::array<int, 3> wins{};
    for (int g = 0; g < games; ++g) {
        using reluva::clue::ComputerPlayer;
        using reluva::clue::Game;
        Game game;
        std::array<bool, reluva::clue::kSuspectCount> playing{};
        for (int s = 0; s < players; ++s)
            playing[size_t(s)] = true;
        game.start(board, playing, onlyLevel < 0 && g % 2 == 1, uint32_t(g + 1));
        std::array<ComputerPlayer, reluva::clue::kSuspectCount> ai;
        // The levels mixed: two players of each, their seats changing game
        // to game.
        for (int s = 0; s < reluva::clue::kSuspectCount; ++s)
            ai[size_t(s)].reset(game, s, uint32_t(g * 31 + s), onlyLevel >= 0 ? onlyLevel : (s + g) % 3);
        const int turnsBefore = turns;
        int steps = 0, early = 0, earlySelf = 0;
        for (; game.phase() != Game::Phase::Over && steps < 100000; ++steps) {
            ComputerPlayer& me = ai[size_t(game.turn())];
            reluva::Error e;
            switch (me.decide(game)) {
            case ComputerPlayer::Action::Roll: game.rollDie(); break;
            case ComputerPlayer::Action::Passage: e = game.takePassage(); break;
            case ComputerPlayer::Action::Walk: e = game.walk(me.chooseWalk(game)); break;
            case ComputerPlayer::Action::Suggest: {
                const auto a = me.chooseSuggestion(game);
                ++suggestions;
                selfNamed += a.suspect == game.turn();
                if (early < 15) {
                    ++early;
                    earlySelf += a.suspect == game.turn();
                }
                e = game.suggest(a.suspect, a.weapon);
                if (!e && game.phase() == Game::Phase::Disprove)
                    e = game.show(ai[size_t(game.disprover())].chooseShow(game, game.turn()));
                for (int s = 0; s < reluva::clue::kSuspectCount; ++s)
                    if (playing[size_t(s)])
                        ai[size_t(s)].observe(game);
                break;
            }
            case ComputerPlayer::Action::Accuse: {
                const auto a = me.chooseAccusation();
                const int accuser = game.turn();
                e = game.accuse(a);
                if (!e && game.phase() != Game::Phase::Over)
                    for (int s = 0; s < reluva::clue::kSuspectCount; ++s)
                        if (playing[size_t(s)])
                            ai[size_t(s)].observeAccusation(a, accuser);
                break;
            }
            case ComputerPlayer::Action::EndTurn:
                e = game.endTurn();
                ++turns;
                break;
            }
            if (e) {
                std::printf("game %d: %s\n", g, e.message.c_str());
                break;
            }
        }
        unfinished += game.phase() != Game::Phase::Over;
        within12 += game.winner() >= 0 && turns - turnsBefore < 12 * players;
        noSelfIn15 += early == 15 && earlySelf == 0;
        if (game.winner() >= 0)
            ++wins[size_t(ai[size_t(game.winner())].level())];
        for (int s = 0; s < reluva::clue::kSuspectCount; ++s)
            wrong += game.seat(s).out;
    }
    if (games)
        std::printf("%d games: %d unfinished, %d wrong accusations, %.1f turns a game; won by novices %d, normal "
                    "players %d, experts %d\n",
                    games, unfinished, wrong, double(turns) / games, wins[0], wins[1], wins[2]);
    if (games && onlyLevel >= 0)
        std::printf("%d players: %.1f rounds a game, %d%% won within 12 rounds; named itself %.0f%% of %d "
                    "suggestions\n",
                    players, double(turns) / games / players, within12 * 100 / games,
                    suggestions ? 100.0 * selfNamed / suggestions : 0.0, suggestions);
    if (games && onlyLevel >= 0)
        std::printf("no one named itself in the first 15 suggestions in %d%% of games\n", noSelfIn15 * 100 / games);
    return unfinished ? 1 : 0;
}

const Commands commands("clue", {
    {"level", "<clue.lvl> [-v]", "a Clue level's placements (each one with -v)", cmdClueLevel},
    {"board", "<clue.lvl> [games [novice|normal|expert players]]",
     "the board a Clue level lays out; with a number, that many games of computer players on it", cmdClueBoard},
});

} // namespace
