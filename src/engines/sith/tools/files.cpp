// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith commands for a game's files. Add a command as a
// function and a line in the table at the end (src/devtools/tool.h); add a
// file to reluva_add_engine_tools in the engine's CMakeLists.txt.

#include "devtools/tool.h"
#include "engines/sith/folders.h"
#include "engines/sith/games.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>

namespace {

using namespace reluva::tool;

// Every file in a game's folder, with its size: where to start.
int cmdFiles(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith files <game folder>\n");
        return 2;
    }
    std::error_code ec;
    const std::filesystem::path root = argv[2];
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
        if (entry.is_regular_file())
            std::printf("%12llu  %s\n", static_cast<unsigned long long>(entry.file_size()),
                        entry.path().lexically_relative(root).generic_string().c_str());
    if (ec) {
        std::fprintf(stderr, "%s: %s\n", argv[2], ec.message().c_str());
        return 1;
    }
    return 0;
}

// The game recognised in a folder, and where each of its CDs was found.
int cmdFolders(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith folders <install> [cd1path] [cd2path]\n");
        return 2;
    }
    const std::filesystem::path install = argv[2];
    for (const reluva::DetectedGame &game : reluva::sith::detectGames(install))
        std::printf("game: %s (%s)\n", game.description.c_str(), game.gameId.c_str());
    std::map<std::string, std::string> settings;
    for (int i = 3; i < argc && i < 3 + reluva::sith::kCdCount; ++i)
        settings["cd" + std::to_string(i - 2) + "path"] = argv[i];
    const reluva::sith::GameFolders folders = reluva::sith::findFolders(install, settings);
    for (int i = 0; i < reluva::sith::kCdCount; ++i) {
        if (folders.cds[i].empty())
            std::printf("CD %d: not found\n", i + 1);
        else
            std::printf("CD %d: %s (%s)\n", i + 1, folders.cds[i].generic_string().c_str(),
                        folders.cdSources[i].c_str());
    }
    return 0;
}

const Commands kCommands("sith",
                         {
                             {"files", "<game folder>", "list every file in a game's folder, with its size", cmdFiles},
                             {"folders", "<install> [cd1path] [cd2path]",
                              "the game recognised in an install, and where its CDs were found", cmdFolders},
                         });

} // namespace
