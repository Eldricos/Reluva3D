// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco command for running the game's own program and
// seeing how far it gets (docs/engines/legoloco/systems/running.md).

#include "devtools/tool.h"
#include "engines/legoloco/host/game_process.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// Options: -log prints each stand-in called; -limit n stops after n
// million instructions; -save folder is where it may write.
int cmdRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco run <game folder> [-log] [-limit millions] [-save folder]\n");
        return 2;
    }
    bool log = false;
    uint64_t limit = 2000;
    std::string save;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-log"))
            log = true;
        else if (!std::strcmp(argv[i], "-limit") && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "-save") && i + 1 < argc)
            save = argv[++i];
    }
    if (save.empty()) {
        std::fprintf(stderr, "a save folder is needed: -save <folder>\n");
        return 2;
    }
    legoloco::GameProcess game(argv[2], save);
    if (Error err = game.load()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const auto &missing = game.process().missingImports();
    std::printf("%zu imports stood in for by nothing:", missing.size());
    for (const std::string &m : missing)
        std::printf(" %s", m.c_str());
    std::printf("\n");

    x86::Cpu &cpu = game.cpu();
    cpu.setInstructionLimit(limit * 1000000);
    std::mutex mutex;
    std::vector<std::string> calls(48);
    size_t callsAt = 0;
    std::string last;
    int repeats = 0;
    game.process().output = [](const std::string &s) { std::printf("[program] %s\n", s.c_str()); };
    cpu.nativeTrace = [&](x86::Cpu &c, const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex);
        char line[256];
        std::snprintf(line, sizeof line, "%s(%x, %x, %x, %x) from %x", name.c_str(), c.arg(0), c.arg(1), c.arg(2),
                      c.arg(3), c.read32(c.reg[x86::Cpu::ESP]));
        calls[callsAt++ % calls.size()] = line;
        if (!log)
            return;
        if (name == last) {
            ++repeats;
            return;
        }
        if (repeats > 1)
            std::printf("  (%d times)\n", repeats);
        std::printf("%s\n", line);
        last = name;
        repeats = 1;
    };
    uint32_t exitCode = 0;
    const Error err = game.run(exitCode);
    std::printf("%llu instructions; ", (unsigned long long)cpu.executed());
    if (!err) {
        std::printf("the program ended, code %u\n", exitCode);
        return 0;
    }
    std::printf("stopped: %s\nlast stand-ins called:\n", err.message.c_str());
    for (size_t i = callsAt > calls.size() ? callsAt - calls.size() : 0; i < callsAt; ++i)
        std::printf("  %s\n", calls[i % calls.size()].c_str());
    const uint32_t esp = cpu.reg[x86::Cpu::ESP];
    std::printf("eip %08x esp %08x; return addresses on the stack:", cpu.faultEip, esp);
    for (uint32_t at = esp, found = 0; at < esp + 4096 && found < 16; at += 4) {
        const uint32_t a = cpu.read32(at);
        if (a >= 0x401000 && a < 0x477000 && cpu.read8(a - 5) == 0xe8) {
            std::printf(" %x", a);
            ++found;
        }
    }
    std::printf("\n");
    return 1;
}

const Commands kCommands("legoloco", {
                                         {"run", "<game folder> [-log] [-limit millions] [-save folder]",
                                          "run the game's own loco.exe and report how far it gets", cmdRun},
                                     });

} // namespace
