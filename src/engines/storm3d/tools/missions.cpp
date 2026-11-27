// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's mission scripts
// (docs/engines/storm3d/games/armada/systems/mission-scripts.md).
//
//   reluva-tool storm3d mission-scripts <game folder> <mission> [-run seconds]

#include "devtools/tool.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/play/mission_scripts.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::storm3d::GameFiles;
using reluva::storm3d::MissionScripts;

int cmdMissionScripts(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d mission-scripts <game folder> <mission> [-run seconds]\n");
        return 2;
    }
    GameFiles files;
    if (Error e = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    const std::string mission = argv[3];
    std::vector<uint8_t> script;
    std::vector<uint8_t> rules;
    if (files.read("missions/" + mission + "S.dsl", script) || files.read("missions/" + mission + "R.drl", rules)) {
        std::fprintf(stderr, "%s's scripts aren't in the install\n", mission.c_str());
        return 1;
    }
    MissionScripts scripts;
    scripts.trace = [](const std::string &line) { std::printf("%s\n", line.c_str()); };
    const Error e = scripts.load(mission, script, rules);
    for (const std::string &name : scripts.unknown())
        std::printf("unread import: %s\n", name.c_str());
    if (e) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    // Then run, 30 passes a second, each cineractive shot taken as done at
    // once *(Reluva's own)*: what the scripts do with no game round them.
    float seconds = 0;
    for (int i = 4; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-run")
            seconds = std::stof(argv[i + 1]);
    std::printf("%zu rules, %zu scripts\n", scripts.ruleCount(), scripts.scriptCount());
    for (int pass = 0; pass < int(seconds * 30); ++pass) {
        if (Error err = scripts.tick(1.0f / 30)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        scripts.setCineractive(true, false);
    }
    return 0;
}

const reluva::tool::Commands kCommands(
    "storm3d",
    {
        {"mission-scripts", "<game folder> <mission> [-run seconds]",
         "a campaign mission's scripts (Federation1) loaded and started, then run for seconds, each call they make of the game",
         cmdMissionScripts},
    });

} // namespace
