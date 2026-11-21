// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island command for running the game's own code in the
// x86 interpreter (docs/engines/legoisland/systems/running.md).

#include "devtools/tool.h"
#include "engines/legoisland/host/game_process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::legoisland;

// An address as module+offset, or a stand-in's name.
std::string where(GameProcess &game, uint32_t a) {
    if (const std::string *n = game.cpu().nativeName(a))
        return "[" + *n + "]";
    char buf[96];
    for (const auto *m : game.process().modules())
        if (a >= m->base && a < m->base + m->size) {
            std::snprintf(buf, sizeof buf, "%s+0x%x", m->name.c_str(), a - m->base);
            return buf;
        }
    std::snprintf(buf, sizeof buf, "0x%08x", a);
    return buf;
}

// Loads ISLE.EXE and LEGO1.DLL, runs the DLL's start-up and the program,
// and says how far they got: what nothing stands in for, the stand-ins
// called, the frames shown, and where a failure happened.
int cmdRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland run <game folder> [-log] [-files] [-limit instructions] "
                             "[-trace count] [-calls count] [-memory MB] [-save folder]\n");
        return 2;
    }
    bool log = false;
    bool files = false;
    uint64_t limit = 4000000000ull;
    size_t traceCount = 32;
    size_t callCount = 40;
    uint32_t memoryMb = 512;
    std::string saveDir;
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-log")
            log = true;
        else if (arg == "-files")
            files = true;

        else if (arg == "-limit" && a + 1 < argc)
            limit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = size_t(std::max(1, std::atoi(argv[++a])));
        else if (arg == "-calls" && a + 1 < argc)
            callCount = size_t(std::max(1, std::atoi(argv[++a])));
        else if (arg == "-memory" && a + 1 < argc)
            memoryMb = uint32_t(std::atoi(argv[++a]));
        else if (arg == "-save" && a + 1 < argc)
            saveDir = argv[++a];
    }

    GameProcess game(argv[2], memoryMb);
    if (!saveDir.empty())
        game.setSaveFolder(saveDir);
    x86::Cpu &cpu = game.cpu();
    x86::Win32Process &process = game.process();
    cpu.setInstructionLimit(limit);
    process.output = [](const std::string &text) { std::printf("[program] %s\n", text.c_str()); };
    if (files)
        process.fileOpened = [](const std::string &path, bool found) {
            std::printf("[file] %s%s\n", path.c_str(), found ? "" : "  (not found)");
        };
    process.messageBox = [](const std::string &caption, const std::string &text) {
        std::printf("[message box] %s: %s\n", caption.c_str(), text.c_str());
    };
    uint64_t frames = 0;
    game.directX().presented = [&frames](const x86::DirectX::Frame &) { frames++; };

    // The last instructions and stand-ins, to show where a failure was;
    // with -log, every stand-in called, repeats counted.
    std::vector<uint32_t> recent(traceCount);
    size_t recentAt = 0;
    cpu.trace = [&](x86::Cpu &c) { recent[recentAt++ % recent.size()] = c.eip; };
    std::deque<std::string> calls;
    std::string last;
    unsigned repeats = 0;
    cpu.nativeTrace = [&](x86::Cpu &, const std::string &name) {
        calls.push_back(name);
        if (calls.size() > callCount)
            calls.pop_front();
        if (!log)
            return;
        if (name == last) {
            repeats++;
            return;
        }
        if (repeats > 1)
            std::printf("  (x%u)\n", repeats);
        std::printf("[call] %s\n", name.c_str());
        last = name;
        repeats = 1;
    };
    auto report = [&](const char *stage, const Error &e) {
        std::printf("%s failed after %llu instructions: %s\nlast stand-ins called:", stage,
                    static_cast<unsigned long long>(cpu.executed()), e.message.c_str());
        for (const std::string &c : calls)
            std::printf(" %s", c.c_str());
        std::printf("\nlast instructions:");
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
            std::printf(" %s", where(game, recent[i % recent.size()]).c_str());
        std::printf("\nframes shown: %llu\n", static_cast<unsigned long long>(frames));
        return 1;
    };

    if (Error e = game.load()) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    for (const auto *m : process.modules())
        if (m->size)
            std::printf("%-12s at 0x%08x, %u bytes\n", m->name.c_str(), m->base, m->size);
    std::map<std::string, std::vector<std::string>> missing;
    for (const std::string &m : process.missingImports()) {
        const size_t bang = m.find('!');
        missing[m.substr(0, bang)].push_back(m.substr(bang + 1));
    }
    std::printf("not stood in for: %zu functions\n", process.missingImports().size());
    for (const auto &[dll, names] : missing) {
        std::printf("  %s (%zu):", dll.c_str(), names.size());
        for (const std::string &n : names)
            std::printf(" %s", n.c_str());
        std::printf("\n");
    }

    const auto start = std::chrono::steady_clock::now();
    if (Error e = game.attach())
        return report("LEGO1.DLL's start-up", e);
    std::printf("LEGO1.DLL's start-up done: %llu instructions\n", static_cast<unsigned long long>(cpu.executed()));
    uint32_t exitCode = 0;
    if (Error e = game.run(exitCode))
        return report("The program", e);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("the program exited with %u after %llu instructions (%.1f s, %.1f million a second); %llu frames\n",
                exitCode, static_cast<unsigned long long>(cpu.executed()), seconds,
                double(cpu.executed()) / 1e6 / std::max(seconds, 1e-6), static_cast<unsigned long long>(frames));
    return 0;
}

const reluva::tool::Commands
    kCommands("legoisland",
              {
                  {"run", "<game folder> [-log] [-files] [-limit instructions] [-trace count] [-calls count]",
                   "run ISLE.EXE and LEGO1.DLL in the x86 interpreter, and say how far they get: what nothing "
                   "stands in for, the stand-ins called, the frames shown, where a failure was",
                   cmdRun},
              });

} // namespace
