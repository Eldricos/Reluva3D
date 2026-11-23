// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Starfleet Command command for running the game's own
// StarFleet2.exe in the x86 emulator, without a window: how far it gets.

#include "common/events.h"
#include "common/fs.h"
#include "common/pe.h"
#include "devtools/tool.h"
#include "emulation/x86/disasm.h"
#include "engines/sfc/host/game_process.h"
#include "common/stream.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "image/image.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// Input given to the headless game, each at a frame it shows: one step a
// frame, so that a program polling the keys' state sees each press.
struct Step {
    uint64_t frame = 0;
    enum class Kind { kKey, kMove, kButton, kShot } kind = Kind::kKey;
    uint32_t key = 0; // kKey: Reluva's key code
    bool down = false;
    int x = 0; // kMove: where on the screen
    int y = 0;
    std::string path; // kShot: the PNG written
};

// Text typed from a frame on: each character's press then release, a
// frame apart, with Shift held around capitals.
void addTyping(std::vector<Step> &steps, uint64_t frame, const std::string &text) {
    auto press = [&](uint32_t k, bool down) {
        Step s;
        s.frame = frame++;
        s.key = k, s.down = down;
        steps.push_back(s);
    };
    for (const char ch : text) {
        if (ch == '\n') {
            press(key::kReturn, true), press(key::kReturn, false);
            continue;
        }
        const bool upper = ch >= 'A' && ch <= 'Z';
        const uint32_t k = upper ? uint32_t(ch - 'A' + 'a') : uint32_t(uint8_t(ch));
        if (upper)
            press(key::kLShift, true);
        press(k, true), press(k, false);
        if (upper)
            press(key::kLShift, false);
    }
}

void writeFrame(const x86::DirectX::Frame &f, const std::string &path) {
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
    writeFile(path, image::encodePng(img));
}

// The game run headless until it stops or the limit: what it opened, the
// frames it showed, the imports nothing stands in for, and where it ended.
int cmdHost(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc host <game folder> [-limit instructions] [-trace count] "
                             "[-frame out.png] [-quiet] [-save folder] [-calls address] [-watch address]... "
                             "[-type frame text] [-click frame x y] [-shot frame out.png]... [-sound folder]\n");
        return 2;
    }
    uint64_t limit = 2000000000ull;
    int traceCount = 24;
    std::string framePath;
    bool quiet = false;
    std::filesystem::path saveFolder; // where what the program writes goes
    uint32_t callsFrom = 0;
    std::vector<uint32_t> watch; // addresses at which to show the registers and the stack
    std::vector<Step> steps;
    std::filesystem::path soundFolder; // each sample's sound, as WAVs
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
        else if (arg == "-sound" && a + 1 < argc)
            soundFolder = argv[++a];
        else if (arg == "-type" && a + 2 < argc) {
            const uint64_t at = std::strtoull(argv[a + 1], nullptr, 0);
            addTyping(steps, at, argv[a + 2]);
            a += 2;
        } else if (arg == "-click" && a + 3 < argc) {
            Step s;
            s.frame = std::strtoull(argv[a + 1], nullptr, 0);
            s.kind = Step::Kind::kMove;
            s.x = std::atoi(argv[a + 2]), s.y = std::atoi(argv[a + 3]);
            steps.push_back(s);
            s.kind = Step::Kind::kButton;
            s.down = true;
            ++s.frame;
            steps.push_back(s);
            s.down = false;
            ++s.frame;
            steps.push_back(s);
            a += 3;
        } else if (arg == "-shot" && a + 2 < argc) {
            Step s;
            s.frame = std::strtoull(argv[a + 1], nullptr, 0);
            s.kind = Step::Kind::kShot;
            s.path = argv[a + 2];
            steps.push_back(s);
            a += 2;
        }
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
    // Where, not what: a value written may be the player's CD key.
    game.registryWritten = [quiet](const std::string &key, const std::string &name) {
        if (!quiet)
            std::printf("registry %s\\%s written\n", key.c_str(), name.c_str());
    };
    // With -sound, what each Miles sample plays, in order, one WAV a
    // sample handle.
    std::map<uint32_t, audio::Sound> sounds;
    if (!soundFolder.empty())
        game.miles().bufferLoaded = [&sounds](uint32_t sample, const std::vector<int16_t> &pcm, int rate,
                                              int channels) {
            audio::Sound &out = sounds[sample];
            if (out.rate != rate || out.channels != channels) {
                out = audio::Sound{};
                out.rate = rate, out.channels = channels;
            }
            out.samples.insert(out.samples.end(), pcm.begin(), pcm.end());
        };
    uint64_t raised = 0;
    process.exceptionRaised = [&](x86::Cpu &c, uint32_t record) {
        ++raised;
        if (!quiet)
            std::printf("raised %s\n", sfc::GameProcess::describeException(c, record).c_str());
    };
    uint64_t frames = 0;
    std::stable_sort(steps.begin(), steps.end(), [](const Step &a, const Step &b) { return a.frame < b.frame; });
    size_t nextStep = 0;
    game.directX().presented = [&](const x86::DirectX::Frame &f) {
        if (frames == 0)
            std::printf("first frame: %dx%d after %llu instructions\n", f.width, f.height,
                        static_cast<unsigned long long>(cpu.executed()));
        for (; nextStep < steps.size() && steps[nextStep].frame <= frames; ++nextStep) {
            const Step &s = steps[nextStep];
            switch (s.kind) {
            case Step::Kind::kKey: {
                uint32_t vk = 0;
                uint32_t scan = 0;
                if (x86::windowsKey(s.key, vk, scan))
                    process.key(vk, scan, s.down);
                break;
            }
            case Step::Kind::kMove:
                process.mouseMoveTo(s.x, s.y);
                break;
            case Step::Kind::kButton:
                process.mouseButton(0, s.down);
                break;
            case Step::Kind::kShot:
                writeFrame(f, s.path);
                std::printf("frame %llu written to %s\n", static_cast<unsigned long long>(frames), s.path.c_str());
                break;
            }
        }
        ++frames;
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
    if (!soundFolder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(soundFolder, ec);
        for (const auto &[sample, sound] : sounds) {
            char name[32];
            std::snprintf(name, sizeof name, "sample-%08x.wav", sample);
            writeFile(soundFolder / name, audio::encodePcmWav(sound));
            std::printf("%s: %d Hz, %d channels, %.2f s\n", name, sound.rate, sound.channels,
                        double(sound.frames()) / double(sound.rate));
        }
    }
    if (!framePath.empty() && frames)
        writeFrame(game.directX().frame(), framePath);
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
         "<game folder> [-limit n] [-trace n] [-frame out.png] [-quiet] [-save folder] [-calls address] "
         "[-watch address]... [-type frame text] [-click frame x y] [-shot frame out.png]... [-sound folder]",
         "run StarFleet2.exe in the x86 emulator, headless, and say how far it gets; keys and clicks "
         "given at frames it shows",
         cmdHost},
        {"disasm", "<game folder> <start> <end>", "disassemble StarFleet2.exe from one address to another", cmdDisasm},
    });

} // namespace
