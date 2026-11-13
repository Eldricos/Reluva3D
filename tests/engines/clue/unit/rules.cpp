// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: The rules: dealing, suggestions, disproving,
// accusations; the notebook; computer players.

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

static void testRules() {
    Board board;
    CHECK(!board.build(boardLevel()));
    Game game;
    CHECK((game.start(board, {true, true, false, false, false, false}, false, 1))); // two aren't enough
    const std::array<bool, kSuspectCount> three{true, false, true, false, true, false};
    CHECK(!game.start(board, three, false, 7));
    // The deal: 18 cards, six each, none of them the case file's.
    size_t dealt = 0;
    for (int s = 0; s < kSuspectCount; ++s) {
        dealt += game.seat(s).hand.size();
        CHECK(game.seat(s).hand.size() == (three[size_t(s)] ? 6u : 0u));
        for (int c : game.seat(s).hand)
            CHECK(!game.solution().has(c));
    }
    CHECK(dealt == 18);
    Game same;
    same.start(board, three, false, 7);
    CHECK(same.solution() == game.solution() && same.seat(kWhite).hand == game.seat(kWhite).hand);

    // Miss Scarlet first: she can only roll (no passage, no room).
    CHECK(game.turn() == kScarlet && game.phase() == Game::Phase::kStart);
    CHECK(game.canRoll() && !game.canSuggest() && !game.canTakePassage() && game.canAccuse() && !game.canEndTurn());
    const int roll = game.rollDie();
    CHECK(roll >= 1 && roll <= 6 && game.phase() == Game::Phase::kMoving && !game.canRoll());
    // From (0, 3): up to (0, 2), then along row 2. Into the hall from (0, 2).
    std::vector<Place> path{{{0, 2}, kNoRoom}};
    for (int x = 1; int(path.size()) < roll; ++x)
        path.push_back({{x, 2}, kNoRoom});
    CHECK(game.canWalk(path));
    if (roll > 1) {
        CHECK((game.canWalk({{{0, 2}, kNoRoom}, {{}, kHall}}))); // a room ends the move early
        std::vector<Place> shortPath(path.begin(), path.end() - 1);
        CHECK(!game.canWalk(shortPath)); // short of the die's number
    }
    CHECK((!game.canWalk({{{1, 3}, kNoRoom}, {{0, 3}, kNoRoom}}))); // back to where she stood
    CHECK((!game.canWalk({{{1, 2}, kNoRoom}})));                    // not a neighbour
    const auto steps = game.nextSteps({{{0, 2}, kNoRoom}});
    CHECK((std::count(steps.begin(), steps.end(), Place{{}, kHall}) == (roll > 1 ? 1 : 0)));
    const auto all = game.destinations();
    CHECK(!all.empty());
    for (const auto &d : all)
        CHECK(game.canWalk(d));
    CHECK((!game.walk({{{0, 2}, kNoRoom}, {{}, kHall}}) == (roll > 1)));
    if (roll == 1)
        CHECK((!game.walk({{{0, 2}, kNoRoom}})));
    CHECK(game.phase() == Game::Phase::kMoved);
    CHECK(game.canSuggest() == (roll > 1) && game.canEndTurn());
    CHECK(!game.endTurn() && game.turn() == kWhite);

    // No Die Roll: nine moves; suggestions, passages and accusations three.
    Game nd;
    CHECK((!nd.start(board, {true, true, true, false, false, false}, true, 3)));
    CHECK(nd.moves() == 9 && !nd.canRoll() && nd.canMove());
    CHECK((nd.canWalk({{{0, 2}, kNoRoom}}) && nd.canWalk({{{0, 2}, kNoRoom}, {{}, kHall}})));
    CHECK((!nd.walk({{{0, 2}, kNoRoom}, {{}, kHall}}) && nd.moves() == 7 && nd.place(kScarlet) == Place{{}, kHall}));
    // Mr. Green isn't played; he is brought to the hall all the same.
    const Accusation s{kGreen, kRope, kHall};
    CHECK(nd.canSuggest() && !nd.suggest(kGreen, kRope) && nd.moves() == 4);
    CHECK((nd.place(kGreen) == Place{{}, kHall} && nd.suggestion() == s && !nd.canSuggest()));
    // The first to her left with a card named shows one.
    int expect = -1;
    for (int k : {kMustard, kWhite})
        if (expect < 0 && std::any_of(nd.seat(k).hand.begin(), nd.seat(k).hand.end(), [&](int c) { return s.has(c); }))
            expect = k;
    CHECK(nd.disprover() == expect);
    if (expect >= 0) {
        CHECK(nd.phase() == Game::Phase::kDisprove && !nd.disproving().empty());
        CHECK(nd.show(roomCard(kStudy) == nd.disproving()[0] ? -1 : roomCard(kStudy)));
        CHECK(!nd.show(nd.disproving()[0]) && nd.shown() >= 0 && s.has(nd.shown()));
    }
    CHECK(nd.phase() == Game::Phase::kSuggested && nd.canAccuse());
    // Wrong: out, and on to the next. Mustard, then, right: he wins.
    Accusation wrong = nd.solution();
    wrong.room = (wrong.room + 1) % kRoomCount;
    CHECK(!nd.accuse(wrong) && nd.seat(kScarlet).out && nd.turn() == kMustard);
    CHECK(!nd.accuse(nd.solution()) && nd.phase() == Game::Phase::kOver && nd.winner() == kMustard);

    // A suspect blocking the only door traps the one inside.
    Game trap;
    trap.start(board, {true, true, true, false, false, false}, true, 5);
    CHECK((!trap.walk({{{0, 2}, kNoRoom}, {{}, kHall}}) && !trap.endTurn())); // Scarlet into the hall
    CHECK((!trap.walk({{{4, 3}, kNoRoom},
                       {{3, 3}, kNoRoom},
                       {{2, 3}, kNoRoom},
                       {{1, 3}, kNoRoom},
                       {{1, 2}, kNoRoom},
                       {{0, 2}, kNoRoom}}) &&
           !trap.endTurn())); // Mustard before its door
    while (trap.turn() != kScarlet)
        CHECK(!trap.walk(trap.destinations().front()) && !trap.endTurn());
    CHECK(trap.trapped() && !trap.canMove() && trap.canEndTurn() && trap.destinations().empty());
    // Two suspects in a room stand apart.
    CHECK(trap.standingSquare(kScarlet) == board.standing(kHall)[0]);
}

// The notebook's deductions, on a made-up deal.
static void testNotebook() {
    Board board;
    CHECK(!board.build(boardLevel()));
    Game game;
    CHECK(!game.start(board, {true, true, true, false, false, false}, false, 9));
    const Accusation &answer = game.solution();
    Notebook n;
    n.reset(game, kScarlet, true, true);
    // Its own cards; the seats not playing hold nothing.
    for (int c : game.seat(kScarlet).hand)
        CHECK(n.holder(c) == kScarlet && n.outOfCaseFile(c));
    CHECK(n.mark(0, kWhite + 1) == Notebook::kNo);
    CHECK(!n.complete());
    // Mustard shown holding a card: nobody else has it.
    const int m = game.seat(kMustard).hand[0];
    n.holds(kMustard, m);
    CHECK(n.holder(m) == kMustard && n.mark(m, kWhite) == Notebook::kNo && n.outOfCaseFile(m));
    // White, who showed one of three without it being seen, two of them
    // being known to be elsewhere: the third is hers.
    const int w = game.seat(kWhite).hand[0];
    std::array<int, 3> three{w, game.seat(kScarlet).hand[0], m};
    std::sort(three.begin(), three.end());
    n.holdsOneOf(kWhite, three);
    CHECK(n.holder(w) == kWhite);
    // Told everything but the answer's: the answer is found.
    for (int s : {kMustard, kWhite})
        for (int c : game.seat(s).hand)
            n.holds(s, c);
    CHECK(n.complete() && n.solved(0) == suspectCard(answer.suspect) && n.solved(1) == weaponCard(answer.weapon) &&
          n.solved(2) == roomCard(answer.room));
    // Counting: a player whose hand is all found holds nothing else; one
    // lacking all cards but as many as they hold, holds those.
    Notebook c;
    c.reset(game, kScarlet, true, true);
    for (int card : game.seat(kWhite).hand)
        c.holds(kWhite, card);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(kWhite).hand.begin(), game.seat(kWhite).hand.end(), card) ==
            game.seat(kWhite).hand.end())
            CHECK(c.mark(card, kWhite) == Notebook::kNo);
    Notebook d;
    d.reset(game, kScarlet, true, true);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(kMustard).hand.begin(), game.seat(kMustard).hand.end(), card) ==
            game.seat(kMustard).hand.end())
            d.lacks(kMustard, card);
    for (int card : game.seat(kMustard).hand)
        CHECK(d.holder(card) == kMustard);
    // Without counting (a novice), the same leaves them open.
    Notebook e;
    e.reset(game, kScarlet, false, false);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(kMustard).hand.begin(), game.seat(kMustard).hand.end(), card) ==
            game.seat(kMustard).hand.end())
            e.lacks(kMustard, card);
    CHECK(e.holder(game.seat(kMustard).hand[0]) == -1); // White's or the case file's, for all it can tell
    // A wrong accusation naming the two cards known to be the answer's:
    // its room isn't.
    Notebook f;
    f.reset(game, kScarlet, true, true);
    for (int s : {kMustard, kWhite})
        for (int card : game.seat(s).hand)
            if (card < roomCard(0))
                f.holds(s, card);
    CHECK(f.solved(0) == suspectCard(answer.suspect) && f.solved(1) == weaponCard(answer.weapon));
    int wrongRoom = -1;
    for (int r = 0; r < kRoomCount; ++r)
        if (r != answer.room && !f.outOfCaseFile(roomCard(r)))
            wrongRoom = r;
    CHECK(wrongRoom >= 0);
    f.notAll({suspectCard(answer.suspect), weaponCard(answer.weapon), roomCard(wrongRoom)});
    CHECK(f.outOfCaseFile(roomCard(wrongRoom)));
}

// Computer players alone, to the end: someone must win, and as they only
// accuse when sure, nobody wrongly.
static void testComputerPlayers() {
    Board board;
    CHECK(!board.build(boardLevel()));
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const bool noDie = seed % 2 == 0;
        const std::array<bool, kSuspectCount> playing{true, seed % 3 != 0, true, true, seed % 5 != 0, true};
        Game game;
        CHECK(!game.start(board, playing, noDie, seed));
        std::array<ComputerPlayer, kSuspectCount> ai;
        for (int s = 0; s < kSuspectCount; ++s)
            if (playing[size_t(s)])
                ai[size_t(s)].reset(game, s, seed * 7 + uint32_t(s));
        int actions = 0;
        while (game.phase() != Game::Phase::kOver && actions++ < 20000) {
            ComputerPlayer &me = ai[size_t(game.turn())];
            switch (me.decide(game)) {
            case ComputerPlayer::Action::kRoll:
                CHECK(game.rollDie() > 0);
                break;
            case ComputerPlayer::Action::kPassage:
                CHECK(!game.takePassage());
                break;
            case ComputerPlayer::Action::kWalk:
                CHECK(!game.walk(me.chooseWalk(game)));
                break;
            case ComputerPlayer::Action::kSuggest: {
                const Accusation a = me.chooseSuggestion(game);
                CHECK(!game.suggest(a.suspect, a.weapon));
                if (game.phase() == Game::Phase::kDisprove)
                    CHECK(!game.show(ai[size_t(game.disprover())].chooseShow(game, game.turn())));
                for (int s = 0; s < kSuspectCount; ++s)
                    if (playing[size_t(s)])
                        ai[size_t(s)].observe(game);
                break;
            }
            case ComputerPlayer::Action::kAccuse:
                CHECK(!game.accuse(me.chooseAccusation()));
                break;
            case ComputerPlayer::Action::kEndTurn:
                CHECK(!game.endTurn());
                break;
            }
        }
        CHECK(game.phase() == Game::Phase::kOver && game.winner() >= 0);
        for (int s = 0; s < kSuspectCount; ++s)
            CHECK(!game.seat(s).out);
    }
}

static const reluva::test::Suite
    kSuite("clue.unit.rules", {
                                  {"rules", [](const std::filesystem::path &) { testRules(); }},
                                  {"notebook", [](const std::filesystem::path &) { testNotebook(); }},
                                  {"computer players", [](const std::filesystem::path &) { testComputerPlayers(); }},
                              });
