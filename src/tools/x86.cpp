// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's shared commands for Windows programs: reading them, and running
// them in the x86 interpreter.

#include "tools/tool.h"

#include "common/pe.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// What a Windows executable or DLL holds and needs (for working out how
// native game code such as Elite Force's single player DLLs could run):
// its sections, exports, imports and relocations.
int cmdPeInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool pe-info <file.dll|file.exe> [-all]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const bool all = argc > 3 && std::strcmp(argv[3], "-all") == 0;
    reluva::PeImage pe;
    if (reluva::Error err = reluva::parsePe(d, pe)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    std::printf("machine 0x%x (%s), %s, characteristics 0x%x (%s), linker %s, built %u (unix time)\n", pe.machine,
                pe.machine == 0x14c ? "x86" : pe.machine == 0x8664 ? "x86-64" : "other", pe.pe32Plus ? "PE32+" : "PE32",
                pe.characteristics, (pe.characteristics & 0x2000) ? "DLL" : "executable", pe.linkerVersion.c_str(),
                pe.timestamp);
    std::printf("image base 0x%llx, size 0x%x, entry point RVA 0x%x, subsystem %u (2 GUI, 3 console)\n",
                static_cast<unsigned long long>(pe.imageBase), pe.imageSize, pe.entryPoint, pe.subsystem);
    std::printf("relocations: %u%s; TLS: %s", pe.relocationCount,
                pe.relocationCount ? " (can load at another address)" : " (fixed address)", pe.hasTls ? "yes" : "no");
    for (const std::string& o : pe.otherDirectories)
        std::printf("; %s", o.c_str());
    std::printf("\n\nsections:\n");
    for (const auto& sec : pe.sections)
        std::printf("  %-8s rva 0x%06x size 0x%06x file 0x%06x+0x%06x %s%s%s%s\n", sec.name.c_str(), sec.virtualAddress,
                    sec.virtualSize, sec.rawOffset, sec.rawSize, (sec.characteristics & 0x20000000) ? "exec " : "",
                    (sec.characteristics & 0x40000000) ? "read " : "", (sec.characteristics & 0x80000000) ? "write " : "",
                    (sec.characteristics & 0x80) ? "(zero-filled)" : "");
    std::printf("\nexports (%s): %zu\n", pe.exportName.c_str(), pe.exports.size());
    for (const auto& x : pe.exports)
        std::printf("  %4u %-40s rva 0x%06x%s%s\n", x.ordinal, x.name.empty() ? "(by ordinal)" : x.name.c_str(), x.rva,
                    x.forwarder.empty() ? "" : " -> ", x.forwarder.c_str());
    size_t total = 0;
    for (const auto& dll : pe.imports)
        total += dll.functions.size();
    std::printf("\nimports: %zu functions from %zu DLLs\n", total, pe.imports.size());
    for (const auto& dll : pe.imports) {
        std::printf("  %s: %zu\n", dll.name.c_str(), dll.functions.size());
        std::string line;
        for (const auto& f : dll.functions) {
            const std::string n = f.name.empty() ? "#" + std::to_string(f.ordinal) : f.name;
            if (line.size() + n.size() > 100) {
                std::printf("    %s\n", line.c_str());
                line.clear();
            }
            line += (line.empty() ? "" : " ") + n;
        }
        if (!line.empty())
            std::printf("    %s\n", line.c_str());
    }
    (void)all;
    return 0;
}

// Loads a 32-bit Windows DLL into the x86 interpreter, runs its start-up
// (DllMain, its C runtime's initialisation) and, if asked, one of its
// exports: to see how far emulation gets with a game's DLL and what it
// calls. "table" as an argument passes a table of functions that report
// their calls (an engine's import table, as GetGameAPI takes).
int cmdX86Run(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool x86-run <file.dll> [-call Export [arg|table]...] [-table slots]\n"
                             "                           [-limit instructions] [-trace count] [-memory MB]\n");
        return 2;
    }
    std::string exportName;
    std::vector<std::string> callArgs;
    int tableSlots = 128, traceCount = 16;
    uint64_t limit = 2000000000ull;
    uint32_t memoryMb = 512;
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-call" && a + 1 < argc) {
            exportName = argv[++a];
            while (a + 1 < argc && argv[a + 1][0] != '-')
                callArgs.push_back(argv[++a]);
        } else if (arg == "-table" && a + 1 < argc)
            tableSlots = std::atoi(argv[++a]);
        else if (arg == "-limit" && a + 1 < argc)
            limit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = std::atoi(argv[++a]);
        else if (arg == "-memory" && a + 1 < argc)
            memoryMb = uint32_t(std::atoi(argv[++a]));
    }
    Bytes file;
    if (!readFile(argv[2], file))
        return 1;
    reluva::x86::Cpu cpu(std::min<uint32_t>(memoryMb, 3072) << 20);
    cpu.setInstructionLimit(limit);
    reluva::x86::Win32Process process(cpu);
    process.output = [](const std::string& text) { std::printf("[program] %s\n", text.c_str()); };

    // The last instructions run, to show where a failure happened.
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    cpu.trace = [&](reluva::x86::Cpu& c) { recent[recentAt++ % recent.size()] = c.eip; };
    auto failed = [&](const reluva::Error& e) {
        std::printf("failed: %s\nlast instructions:", e.message.c_str());
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
            std::printf(" %08x", recent[i % recent.size()]);
        std::printf("\n");
        return 1;
    };

    const std::string name = std::filesystem::path(argv[2]).filename().string();
    const reluva::x86::Win32Process::Module* m = nullptr;
    if (reluva::Error e = process.load(name, file, m)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("%s loaded at 0x%08x (asked for 0x%08x), %u bytes\n", name.c_str(), m->base,
                uint32_t(m->image.imageBase), m->size);
    for (const std::string& missing : process.missingImports())
        std::printf("  not stood in for: %s\n", missing.c_str());
    uint64_t before = cpu.executed();
    if (reluva::Error e = process.attach(*m))
        return failed(e);
    std::printf("start-up (DllMain) done: %llu instructions\n", (unsigned long long)(cpu.executed() - before));
    if (exportName.empty())
        return 0;

    const uint32_t address = m->exportAddress(exportName);
    if (!address) {
        std::fprintf(stderr, "%s has no export %s\n", name.c_str(), exportName.c_str());
        return 1;
    }
    // The table: each slot a function that prints its call.
    int reported = 0;
    const uint32_t table = process.memory().alloc(uint32_t(tableSlots) * 4);
    for (int i = 0; i < tableSlots; ++i) {
        const uint32_t fn = cpu.addNative("table[" + std::to_string(i) + "]", [i, &reported](reluva::x86::Cpu& c) {
            if (reported++ < 500) {
                // Arguments as numbers, or as text where they point at some.
                std::string args;
                for (int n = 0; n < 4; ++n) {
                    const uint32_t v = c.arg(n);
                    const std::string text = c.string(v, 80);
                    const bool printable = text.size() >= 2 && std::all_of(text.begin(), text.end(), [](char ch) {
                        return ch == '\n' || ch == '\t' || (uint8_t(ch) >= 32 && uint8_t(ch) < 127);
                    });
                    char buf[24];
                    std::snprintf(buf, sizeof buf, "0x%x", v);
                    args += (n ? ", " : "") + (printable ? std::string(buf) + " \"" + text + "\"" : std::string(buf));
                }
                std::printf("  table[%d](%s) from 0x%08x\n", i, args.c_str(), c.read32(c.reg[reluva::x86::Cpu::ESP]));
            }
            c.reg[reluva::x86::Cpu::EAX] = 0;
            return 0u;
        });
        cpu.write32(table + 4 * uint32_t(i), fn);
    }
    std::vector<uint32_t> args;
    for (const std::string& a : callArgs)
        args.push_back(a == "table" ? table : uint32_t(std::strtoul(a.c_str(), nullptr, 0)));
    uint32_t result = 0;
    before = cpu.executed();
    if (reluva::Error e = cpu.call(address, args, result))
        return failed(e);
    std::printf("%s returned 0x%08x after %llu instructions\n", exportName.c_str(), result,
                (unsigned long long)(cpu.executed() - before));
    // If it's a pointer, what it points at (a table of the DLL's functions?).
    if (cpu.valid(result, 4)) {
        for (uint32_t i = 0; i < 64 && cpu.valid(result + 4 * i, 4); ++i) {
            const uint32_t v = cpu.read32(result + 4 * i);
            const bool inModule = v >= m->base && v < m->base + m->size;
            std::printf("  [%2u] 0x%08x%s\n", i, v, inModule ? (" (" + name + "+0x" + [&] {
                char b[16];
                std::snprintf(b, sizeof b, "%x", v - m->base);
                return std::string(b);
            }() + ")").c_str() : "");
        }
    }
    return 0;
}

const Commands commands("", {
    {"pe-info", "<file.dll|file.exe>",
     "what a Windows executable or DLL holds and needs: its sections, exports, imports and relocations",
     cmdPeInfo},
    {"x86-run", "<file.dll> [-call Export [arg|table]...]",
     "load a 32-bit Windows DLL into the x86 interpreter, run its start-up and, if asked, one of its "
     "exports (\"table\" passes a table of functions that report their calls)",
     cmdX86Run},
});

} // namespace
