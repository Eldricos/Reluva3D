// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the ride and shop scripts
// (.RSE).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/rse.h"
#include "engines/tpw/game_files.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::Script;

// Every script of a game read: how often each opcode is used, with a
// checksum of every script's disassembly.
int cmdRseCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw rse-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::Md5 all;
    std::map<uint16_t, int> uses;
    int failed = 0;
    int scripts = 0;
    size_t instructions = 0;
    for (const std::string &name : files.list()) {
        if (reluva::fs::toLower(extensionOf(name)) != ".rse")
            continue;
        std::vector<uint8_t> data;
        Script script;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = script.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++scripts;
        instructions += script.instructions().size();
        for (const Script::Instruction &ins : script.instructions())
            ++uses[ins.opcode];
        const std::string text = script.disassemble();
        all.update(text.data(), text.size());
    }
    for (const auto &[opcode, n] : uses)
        std::printf("  %3u %-16s %5d\n", opcode, Script::opcodeName(opcode), n);
    std::printf("%d failed; %d scripts, %zu instructions, %zu opcodes used; md5 %s\n", failed, scripts, instructions,
                uses.size(), all.hexDigest().c_str());
    return failed || scripts == 0 ? 1 : 0;
}

// One script as source, in the language of the sources that ship with
// the game.
int cmdRseDis(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool tpw rse-dis <game folder> <script in Data>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    Script script;
    reluva::Error err = files.read(argv[3], data);
    if (!err)
        err = script.parse(data);
    if (err) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    std::fputs(script.disassemble().c_str(), stdout);
    return 0;
}

const Commands kCommands("tpw",
                         {
                             {"rse-check", "<game folder>", "read every ride and shop script (.RSE)", cmdRseCheck},
                             {"rse-dis", "<game folder> <script in Data>",
                              "write a script out as source, as the game's own .rss files are", cmdRseDis},
                         });

} // namespace
