// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Starfleet Command command for running the game's own
// StarFleet2.exe in the x86 emulator, without a window: how far it gets.

#include "common/fs.h"
#include "common/pe.h"
#include "devtools/tool.h"
#include "emulation/x86/disasm.h"
#include "engines/sfc/host/game_process.h"
#include "common/stream.h"
#include "image/image.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// The game run headless until it stops or the limit: what it opened, the
// frames it showed, the imports nothing stands in for, and where it ended.
int cmdHost(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc host <game folder> [-limit instructions] [-trace count] "
                             "[-frame out.png] [-quiet] [-save folder] [-calls address] [-watch address]...\n");
        return 2;
    }
    uint64_t limit = 2000000000ull;
    int traceCount = 24;
    std::string framePath;
    bool quiet = false;
    std::filesystem::path saveFolder; // where what the program writes goes
    uint32_t callsFrom = 0;
    std::vector<uint32_t> watch; // addresses at which to show the registers and the stack
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-limit" && a + 1 < argc)
            limit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = std::atoi(argv[++a]);
        else if (arg == "-frame" && a + 1 < argc)
            framePath = argv[++a];
        else if (arg == "-quiet")
            quiet = true;
        else if (arg == "-save" && a + 1 < argc)
            saveFolder = argv[++a];
        else if (arg == "-calls" && a + 1 < argc)
            callsFrom = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        else if (arg == "-watch" && a + 1 < argc)
            watch.push_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)));
    }
    sfc::GameProcess game(argv[2], 256);
    // Without -save, a folder of its own in the system's temporary folder.
    if (saveFolder.empty()) {
        std::error_code ec;
        saveFolder = std::filesystem::temp_directory_path(ec) / "reluva-sfc-host";
    }
    game.setSaveFolder(saveFolder);
    std::printf("writes go to %s\n", saveFolder.string().c_str());
    x86::Cpu &cpu = game.cpu();
    x86::Win32Process &process = game.process();
    process.output = [](const std::string &text) { std::printf("program: %s", text.c_str()); };
    game.fileOpened = [quiet](const std::string &path, bool found) {
        if (!quiet || !found)
            std::printf("open %s%s\n", path.c_str(), found ? "" : "  (not found)");
    };
    game.iniReads = [quiet](const std::string &what) {
        if (!quiet)
            std::printf("ini %s\n", what.c_str());
    };
    uint64_t raised = 0;
    process.exceptionRaised = [&](x86::Cpu &c, uint32_t record) {
        ++raised;
        if (!quiet)
            std::printf("raised %s\n", sfc::GameProcess::describeException(c, record).c_str());
    };
    uint64_t frames = 0;
    game.directX().presented = [&](const x86::DirectX::Frame &f) {
        if (frames++ == 0)
            std::printf("first frame: %dx%d after %llu instructions\n", f.width, f.height,
                        static_cast<unsigned long long>(cpu.executed()));
    };
    if (Error err = game.load()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("%s loaded; %zu imports not stood in for:\n", sfc::GameProcess::kProgram,
                process.missingImports().size());
    for (const std::string &m : process.missingImports())
        std::printf("  %s\n", m.c_str());

    cpu.setInstructionLimit(limit);
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    // With -calls, each call into a stand-in from the moment the program
    // reaches an address, and the interface each QueryInterface asks for.
    bool logging = false;
    cpu.trace = [&](x86::Cpu &c) {
        recent[recentAt++ % recent.size()] = c.eip;
        if (callsFrom && c.eip == callsFrom)
            logging = true;
        if (std::find(watch.begin(), watch.end(), c.eip) != watch.end()) {
            std::printf("at %08x: eax %x ebx %x ecx %x edx %x esi %x edi %x ebp %x esp %x; stack", c.eip,
                        c.reg[x86::Cpu::EAX], c.reg[x86::Cpu::EBX], c.reg[x86::Cpu::ECX], c.reg[x86::Cpu::EDX],
                        c.reg[x86::Cpu::ESI], c.reg[x86::Cpu::EDI], c.reg[x86::Cpu::EBP], c.reg[x86::Cpu::ESP]);
            for (uint32_t i = 0; i < 8; ++i)
                std::printf(" %x", c.read32(c.reg[x86::Cpu::ESP] + 4 * i));
            std::printf("\n");
            // What the stack's first words point to, where they're pointers
            // (a call's arguments, at a call; after the return address, at
            // a function's start).
            for (uint32_t i = 0; i <= 3; ++i) {
                const uint32_t p = c.read32(c.reg[x86::Cpu::ESP] + 4 * i);
                if (p < 0x10000 || !c.valid(p, 64))
                    continue;
                std::printf("  [%x]:", p);
                for (uint32_t k = 0; k < 16; ++k)
                    std::printf(" %x", c.read32(p + 4 * k));
                std::printf("\n");
            }
        }
        if (!logging)
            return;
        const std::string *name = c.nativeName(c.eip);
        if (!name)
            return;
        std::printf("call %s (%x, %x, %x, %x, %x, %x) from %x", name->c_str(), c.arg(0), c.arg(1), c.arg(2), c.arg(3),
                    c.arg(4), c.arg(5), c.read32(c.reg[x86::Cpu::ESP]));
        if (name->find("RegOpenKeyEx") != std::string::npos || name->find("RegQueryValueEx") != std::string::npos ||
            name->find("RegCreateKeyEx") != std::string::npos || name->find("RegSetValueEx") != std::string::npos)
            if (c.valid(c.arg(1), 1))
                std::printf("  \"%s\"", c.string(c.arg(1), 200).c_str());
        if (name->find("::QueryInterface") != std::string::npos) {
            const uint32_t iid = c.read32(c.reg[x86::Cpu::ESP] + 8);
            std::printf("  {%08x-%04x-%04x-...}", c.read32(iid), c.read16(iid + 4), c.read16(iid + 6));
        }
        std::printf("\n");
    };
    const auto start = std::chrono::steady_clock::now();
    uint32_t exitCode = 0;
    const Error err = game.run(exitCode);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const uint64_t executed = cpu.executed();
    std::printf("%s\n", err ? ("stopped: " + err.message).c_str() : ("ended with " + std::to_string(exitCode)).c_str());
    std::printf("%llu exceptions raised\n", static_cast<unsigned long long>(raised));
    std::printf("%llu instructions in %.2f s (%.1f million a second); %llu frames shown\n",
                static_cast<unsigned long long>(executed), seconds,
                seconds > 0 ? double(executed) / seconds / 1e6 : 0.0, static_cast<unsigned long long>(frames));
    std::printf("last instructions:");
    for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
        std::printf(" %08x", recent[i % recent.size()]);
    std::printf("\n");
    if (!framePath.empty() && frames) {
        const auto &f = game.directX().frame();
        image::Image img;
        img.w = f.width;
        img.h = f.height;
        img.rgba.resize(size_t(f.width) * size_t(f.height) * 4);
        for (size_t i = 0; i < f.pixels.size(); ++i) {
            const uint32_t p = f.pixels[i];
            img.rgba[4 * i] = uint8_t(p >> 16);
            img.rgba[4 * i + 1] = uint8_t(p >> 8);
            img.rgba[4 * i + 2] = uint8_t(p);
            img.rgba[4 * i + 3] = 255;
        }
        writeFile(framePath, image::encodePng(img));
    }
    return err ? 1 : 0;
}

// StarFleet2.exe's code from one address to another, disassembled, for
// following what the host run reports.
int cmdDisasm(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool sfc disasm <game folder> <start> <end>\n");
        return 2;
    }
    const auto path = fs::findFile(argv[2], sfc::GameProcess::kProgram);
    std::vector<uint8_t> file;
    PeImage pe;
    if (!path || !readFile(*path, file) || parsePe(file, pe)) {
        std::fprintf(stderr, "can't read %s in %s\n", sfc::GameProcess::kProgram, argv[2]);
        return 1;
    }
    const auto start = uint32_t(std::strtoul(argv[3], nullptr, 0));
    const auto end = uint32_t(std::strtoul(argv[4], nullptr, 0));
    for (const PeImage::Section &sec : pe.sections) {
        const auto base = uint32_t(pe.imageBase) + sec.virtualAddress;
        if (start < base || start >= base + sec.rawSize)
            continue;
        for (uint32_t at = start; at < end && at < base + sec.rawSize;) {
            const size_t offset = sec.rawOffset + (at - base);
            const x86::Instruction ins =
                x86::disassemble(file.data() + offset, std::min<size_t>(16, file.size() - offset), at);
            std::printf("%08x  %-8s %s\n", at, ins.mnemonic.c_str(), ins.operands.c_str());
            at += uint32_t(std::max(ins.size, 1));
        }
        return 0;
    }
    std::fprintf(stderr, "0x%08x isn't in a section\n", start);
    return 1;
}

const Commands kCommands(
    "sfc",
    {
        {"host",
         "<game folder> [-limit n] [-trace n] [-frame out.png] [-quiet] [-save folder] [-calls address] [-watch address]...",
         "run StarFleet2.exe in the x86 emulator, headless, and say how far it gets", cmdHost},
        {"disasm", "<game folder> <start> <end>", "disassemble StarFleet2.exe from one address to another", cmdDisasm},
    });

} // namespace
