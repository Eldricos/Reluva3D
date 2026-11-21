// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island command for running the game's own code in the
// x86 interpreter (docs/engines/legoisland/systems/running.md).

#include "common/stream.h"
#include "devtools/tool.h"
#include "emulation/x86/com.h"
#include "image/image.h"
#include "engines/legoisland/host/game_process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
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
        std::fprintf(
            stderr,
            "usage: reluva-tool legoisland run <game folder> [-log] [-files] [-limit instructions] "
            "[-trace count] [-calls count] [-memory MB] [-save folder] [-shots folder] [-every frames] [-at address]... [-watch address,bytes] [-click frame@x,y]...\n");
        return 2;
    }
    bool log = false;
    bool files = false;
    uint64_t limit = 4000000000ull;
    size_t traceCount = 32;
    size_t callCount = 40;
    uint32_t memoryMb = 512;
    std::string saveDir;
    std::string shotsDir;
    std::vector<uint32_t> at;
    uint32_t watchFrom = 0;
    // -click: at a frame, a left click at a point of the game's picture.
    struct Click {
        uint64_t frame;
        int x;
        int y;
    };
    std::vector<Click> clicks;
    uint32_t watchBytes = 0;
    uint64_t every = 25;
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
        else if (arg == "-click" && a + 1 < argc) {
            Click k{};
            if (std::sscanf(argv[++a], "%llu@%d,%d", reinterpret_cast<unsigned long long *>(&k.frame), &k.x, &k.y) == 3)
                clicks.push_back(k);
        } else if (arg == "-watch" && a + 1 < argc) {
            char *rest = nullptr;
            watchFrom = uint32_t(std::strtoul(argv[++a], &rest, 0));
            watchBytes = rest && *rest == ',' ? uint32_t(std::strtoul(rest + 1, nullptr, 0)) : 4;
        } else if (arg == "-at" && a + 1 < argc)
            at.push_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        else if (arg == "-shots" && a + 1 < argc)
            shotsDir = argv[++a];
        else if (arg == "-every" && a + 1 < argc)
            every = std::max<uint64_t>(1, std::strtoull(argv[++a], nullptr, 0));
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
    // -shots: every `every`th frame shown, as frame_NNNNN.tga.
    if (!shotsDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(shotsDir, ec);
    }
    game.directX().presented = [&](const x86::DirectX::Frame &f) {
        frames++;
        // A click: pressed at its frame, let go at the next, where the
        // picture is in its window's client area.
        for (const Click &k : clicks) {
            if (frames != k.frame && frames != k.frame + 1)
                continue;
            int left = 0;
            int top = 0;
            if (const auto area = process.clientArea(process.mainWindow())) {
                left = (*area)[0];
                top = (*area)[1];
            }
            process.mouseMoveTo(left + k.x, top + k.y);
            process.mouseButton(0, frames == k.frame);
            std::printf("[click] %s at %d,%d, frame %llu\n", frames == k.frame ? "down" : "up", k.x, k.y,
                        static_cast<unsigned long long>(frames));
        }
        if (shotsDir.empty() || (frames - 1) % every != 0 || f.width <= 0 || f.height <= 0)
            return;
        image::Image picture;
        picture.w = f.width;
        picture.h = f.height;
        picture.rgba.resize(size_t(f.width) * size_t(f.height) * 4);
        for (size_t i = 0; i < f.pixels.size() && i * 4 + 3 < picture.rgba.size(); ++i) {
            const uint32_t argb = f.pixels[i];
            picture.rgba[i * 4] = uint8_t(argb >> 16);
            picture.rgba[i * 4 + 1] = uint8_t(argb >> 8);
            picture.rgba[i * 4 + 2] = uint8_t(argb);
            picture.rgba[i * 4 + 3] = 255;
        }
        char name[32];
        std::snprintf(name, sizeof name, "frame_%05llu.tga", static_cast<unsigned long long>(frames));
        writeFile(std::filesystem::path(shotsDir) / name, image::encodeTga(picture));
    };

    // The last instructions and stand-ins, to show where a failure was;
    // with -log, every stand-in called, repeats counted.
    std::vector<uint32_t> recent(traceCount);
    size_t recentAt = 0;
    // -at: the registers, and the stack's first words, each time the program
    // gets to an address; text where one points at some.
    auto textAt = [](x86::Cpu &c, uint32_t v) {
        const std::string text = c.valid(v, 1) ? c.string(v, 48) : "";
        const bool printable =
            text.size() >= 3 && std::all_of(text.begin(), text.end(), [](char ch) { return ch >= 32 && ch < 127; });
        return printable ? text : std::string();
    };
    // A value, the text it points to, or the text a pointer it points to does.
    auto show = [&](x86::Cpu &c, uint32_t v) {
        std::printf(" %08x", v);
        if (const std::string t = textAt(c, v); !t.empty())
            std::printf(" \"%s\"", t.c_str());
        else if (c.valid(v, 4))
            if (const std::string t2 = textAt(c, c.read32(v)); !t2.empty())
                std::printf(" -> \"%s\"", t2.c_str());
    };
    // -watch: each store into a range, the value, and where it was made.
    if (watchBytes) {
        cpu.watchStart = watchFrom;
        cpu.watchEnd = watchFrom + watchBytes;
        cpu.onWatchedStore = [&](x86::Cpu &c, uint32_t address, uint32_t value, int size) {
            std::printf("[store] %08x <- %08x (%d bytes) at %s\n", address, value, size, where(game, c.eip).c_str());
        };
    }
    cpu.trace = [&](x86::Cpu &c) {
        recent[recentAt++ % recent.size()] = c.eip;
        if (at.empty() || std::find(at.begin(), at.end(), c.eip) == at.end())
            return;
        std::printf("[at] %s:", where(game, c.eip).c_str());
        const char *names[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
        for (int r = 0; r < 8; ++r) {
            std::printf(" %s", names[r]);
            show(c, c.reg[r]);
        }
        std::printf("\n     stack:");
        for (uint32_t i = 0; i < 6; ++i)
            if (c.valid(c.reg[x86::Cpu::ESP] + 4 * i, 4))
                show(c, c.read32(c.reg[x86::Cpu::ESP] + 4 * i));
        std::printf("\n");
    };
    std::deque<std::string> calls;
    std::string last;
    unsigned repeats = 0;
    cpu.nativeTrace = [&](x86::Cpu &c, const std::string &name) {
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
        std::printf("[call] %s", name.c_str());
        // A file call's handle and numbers: where it reads, how much.
        for (const char *f : {"!_llseek", "!_hread", "!_lread", "!ReadFile", "!SetFilePointer"})
            if (name.size() > std::strlen(f) && name.compare(name.size() - std::strlen(f), std::strlen(f), f) == 0)
                std::printf(" (%x, %x, %x)", c.arg(0), c.arg(1), c.arg(2));
        // A registry value's name: which settings the program reads.
        if (name == "ADVAPI32.dll!RegQueryValueExA" && c.valid(c.arg(1), 1))
            std::printf(" \"%s\"", c.string(c.arg(1), 64).c_str());
        // Which interface is asked for: the next one to stand in for, when refused.
        if (name.size() > 16 && name.compare(name.size() - 16, 16, "::QueryInterface") == 0)
            std::printf(" %s", x86::ComHost::guid(c, c.arg(1)).c_str());
        std::printf("\n");
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

const reluva::tool::Commands kCommands(
    "legoisland",
    {
        {"run", "<game folder> [-log] [-files] [-limit instructions] [-trace count] [-calls count] [-shots folder]",
         "run ISLE.EXE and LEGO1.DLL in the x86 interpreter, and say how far they get: what nothing "
         "stands in for, the stand-ins called, the frames shown, where a failure was",
         cmdRun},
    });

} // namespace
