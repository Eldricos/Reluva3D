// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Harbinger commands for a game's folder as the engine sees it:
// its parts and files, what its data holds, and every conversation played
// through (STDS9H's test with the game's own data, as a command).
//
// A game folder is anything the launcher takes: a CD (or a copy of one),
// both CDs side by side, an install, or Reluva's import of them.

#include "common/headless_system.h"
#include "common/log.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/game/game.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"
#include "devtools/tool.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stharbinger;
using namespace reluva::tool;

// The engine's options for the game in `folder`; false (said why) if
// there's none.
bool gameOptions(const char *folder, Options &options) {
    for (const GamePart &part : findGameParts(folder))
        options.paths.push_back(part.folder.string());
    if (options.paths.empty()) {
        std::fprintf(stderr, "%s: no part of Harbinger found (a CD, both CDs or an install)\n", folder);
        return false;
    }
    return true;
}

// Runs `fn` on the game, reporting a fault in its data as the engine does.
template<class Fn>
int withGame(const char *folder, Fn fn) {
    Options options;
    if (!gameOptions(folder, options))
        return 1;
    HeadlessSystem system;
    Platform platform(system, nullptr, {});
    try {
        Harbinger engine(&platform, options);
        engine.initResources();
        return fn(engine);
    } catch (const FatalError &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}

// The parts of the game in a folder, and every file the engine can read,
// by the name the game uses.
int cmdFiles(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger files <game folder>\n");
        return 2;
    }
    const auto parts = findGameParts(argv[2]);
    if (parts.empty()) {
        std::fprintf(stderr, "%s: no part of Harbinger found\n", argv[2]);
        return 1;
    }
    for (const GamePart &p : parts)
        std::printf("part: %s (%s%s%s%s)\n", p.folder.string().c_str(),
                    p.disc >= 0 ? ("disc " + std::to_string(p.disc + 1)).c_str() : "no CD",
                    p.scripts ? ", scripts" : "", p.data ? ", data" : "", p.exe ? ", DEEP.EXE" : "");
    // As the engine searches them, without needing the scene scripts.
    Resources resources;
    for (const GamePart &p : parts)
        resources.addGamePath(p.folder.string());
    const SearchSet &set = resources.getSearchSet();
    for (const std::string &name : set.list()) {
        const Archive *a = set.find(name);
        std::printf("%-48s %s\n", name.c_str(), a ? a->describe().c_str() : "");
    }
    return 0;
}

// What the game's data holds: its scenes (with how many of their
// backgrounds, animations and sounds are found), its dialogues and credits.
int cmdInfo(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger info <game folder>\n");
        return 2;
    }
    return withGame(argv[2], [](Harbinger &engine) {
        engine.printInfo();
        return 0;
    });
}

// Plays every conversation `runs` times, picking replies at random (from
// seeds 1, 2, ...), as far as each goes without the player: a conversation
// is stuck if it neither ends nor asks for 200 s of game time.
int cmdConversations(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger conversations <game folder> [runs]\n");
        return 2;
    }
    const int runs = argc == 4 ? std::max(1, std::atoi(argv[3])) : 3;
    return withGame(argv[2], [runs](Harbinger &engine) {
        int played = 0;
        int stuck = 0;
        int menus = 0;
        for (int seed = 1; seed <= runs; seed++) {
            std::srand(unsigned(seed));
            Game game(&engine);
            for (int d = 0; d < kNumDialogues; d++) {
                if (!game.debugTalk(getDialogueName(d)))
                    continue;
                played++;
                int steps = 0;
                int lastMenu = 0;
                while (game.isConversationActive() && steps < 20000 && steps - lastMenu < 10000) {
                    const std::vector<std::string> menu = game.getMenuLabels();
                    if (!menu.empty()) {
                        menus++;
                        lastMenu = steps;
                        game.pickMenuEntry(std::rand() % int(menu.size()));
                    }
                    game.runSteps(1);
                    steps++;
                }
                if (game.isConversationActive() && steps - lastMenu >= 10000) {
                    std::printf("%s did not end (run %d)\n", getDialogueName(d), seed);
                    stuck++;
                }
            }
        }
        std::printf("%d conversations played, %d menus, %d stuck\n", played, menus, stuck);
        return played > 0 && stuck == 0 ? 0 : 1;
    });
}

const Commands kCommands(
    "stharbinger",
    {
        {"files", "<game folder>",
         "the game's parts in a folder (CDs, an install), and every file the engine reads from them, with where "
         "it comes from (a folder, or an archive such as SSE.ALL)",
         cmdFiles},
        {"info", "<game folder>",
         "the scenes (with the backgrounds, animations and sounds found for each), the dialogues and the credits",
         cmdInfo},
        {"conversations", "<game folder> [runs]",
         "play every conversation through [runs] times (3), picking replies at random; fails if one gets stuck",
         cmdConversations},
    });

} // namespace
