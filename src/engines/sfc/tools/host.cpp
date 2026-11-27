// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Starfleet Command command for running the game's own
// StarFleet2.exe in the x86 emulator, without a window: how far it gets.

#include "backends/networking/sockets/socket_net.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/pe.h"
#include "devtools/tool.h"
#include "emulation/x86/disasm.h"
#include "engines/sfc/host/game_process.h"
#include "engines/sfc/host/patcher.h"
#include "common/stream.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "image/image.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
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

// Reluva's key by a name given to -key: num0 to num9, numplus, numminus,
// nummult, numdiv, numdec, numenter, the arrows, f1 to f12, and enter,
// esc, space, tab, backspace, shift, ctrl, alt; 0 for none.
uint32_t keyByName(const std::string &name) {
    static const std::pair<const char *, uint32_t> kNames[] = {
        {"numplus", key::kKpPlus},  {"numminus", key::kKpMinus}, {"nummult", key::kKpMultiply},
        {"numdiv", key::kKpDivide}, {"numdec", key::kKpPeriod},  {"numenter", key::kKpEnter},
        {"left", key::kLeft},       {"right", key::kRight},      {"up", key::kUp},
        {"down", key::kDown},       {"enter", key::kReturn},     {"esc", key::kEscape},
        {"space", key::kSpace},     {"tab", key::kTab},          {"backspace", key::kBackspace},
        {"shift", key::kLShift},    {"ctrl", key::kLCtrl},       {"alt", key::kLAlt},
    };
    for (const auto &[n, k] : kNames)
        if (name == n)
            return k;
    if (name.size() == 4 && name.rfind("num", 0) == 0 && name[3] >= '0' && name[3] <= '9')
        return key::kKp0 + uint32_t(name[3] - '0');
    if (name.size() >= 2 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12)
            return key::kF1 + uint32_t(n - 1);
    }
    return 0;
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
        std::fprintf(
            stderr,
            "usage: reluva-tool sfc host <game folder> [-limit instructions] [-trace count] "
            "[-frame out.png] [-quiet] [-save folder] [-calls address] [-watch address]... "
            "[-type frame text] [-click frame x y] [-shot frame out.png]... [-sound folder] [-writes address]... "
            "[-newcode from to] [-profile from to] [-rate from to] [-fixed-clock] [-key frame name[+name]]... [-down frame name] [-up frame name] "
            "[-net]\n");
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
    std::vector<std::pair<uint32_t, uint32_t>> writes; // addresses whose changes are shown, and their last value
    // With -newcode, the program's code first run between two frames: a
    // bit an address of its image, set as it runs.
    bool fixedClock = false;
    bool network = false;
    uint64_t newFrom = 0;
    uint64_t newTo = 0;
    // With -profile, how many instructions ran in each 16 bytes of code
    // between two frames: where a program that's busy is busy.
    uint64_t profileFrom = 0;
    uint64_t profileTo = 0;
    // With -rate, how fast the program ran between two frames, untraced:
    // instructions and frames a second of the host's time, and the
    // program's own time (its clock, its sleeps) gone by.
    uint64_t rateFrom = 0;
    uint64_t rateTo = 0;
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
        else if (arg == "-writes" && a + 1 < argc)
            writes.emplace_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)), 0);
        else if (arg == "-fixed-clock")
            fixedClock = true;
        else if (arg == "-net")
            network = true;
        else if (arg == "-key" && a + 2 < argc) {
            // Pressed at the frame, released at the next; keys joined by
            // + (ctrl+3) are pressed a frame apart and released the other
            // way round, as a hand holds a modifier around a key.
            const uint64_t at = std::strtoull(argv[a + 1], nullptr, 0);
            std::vector<uint32_t> keys;
            const std::string names = argv[a + 2];
            for (size_t from = 0;;) {
                const size_t plus = names.find('+', from + 1);
                const std::string name = names.substr(from, plus == std::string::npos ? plus : plus - from);
                uint32_t k = keyByName(name);
                if (!k && name.size() == 1)
                    k = uint32_t(uint8_t(std::tolower(static_cast<unsigned char>(name[0]))));
                if (!k)
                    std::fprintf(stderr, "no key named %s\n", name.c_str());
                else
                    keys.push_back(k);
                if (plus == std::string::npos)
                    break;
                from = plus + 1;
            }
            uint64_t frame = at;
            for (size_t i = 0; i < 2 * keys.size(); ++i) {
                Step s;
                s.frame = frame++;
                s.down = i < keys.size();
                s.key = s.down ? keys[i] : keys[2 * keys.size() - 1 - i];
                steps.push_back(s);
            }
            a += 2;
        } else if ((arg == "-down" || arg == "-up") && a + 2 < argc) {
            // A key held from one frame to another: Shift around a click.
            Step s;
            s.frame = std::strtoull(argv[a + 1], nullptr, 0);
            s.key = keyByName(argv[a + 2]);
            s.down = arg == "-down";
            if (s.key)
                steps.push_back(s);
            else
                std::fprintf(stderr, "no key named %s\n", argv[a + 2]);
            a += 2;
        } else if (arg == "-rate" && a + 2 < argc) {
            rateFrom = std::strtoull(argv[a + 1], nullptr, 0);
            rateTo = std::strtoull(argv[a + 2], nullptr, 0);
            a += 2;
        } else if (arg == "-profile" && a + 2 < argc) {
            profileFrom = std::strtoull(argv[a + 1], nullptr, 0);
            profileTo = std::strtoull(argv[a + 2], nullptr, 0);
            a += 2;
        } else if (arg == "-newcode" && a + 2 < argc) {
            newFrom = std::strtoull(argv[a + 1], nullptr, 0);
            newTo = std::strtoull(argv[a + 2], nullptr, 0);
            a += 2;
        } else if (arg == "-sound" && a + 1 < argc)
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
    sfc::GameProcess game(argv[2], sfc::GameProcess::kMemoryMb);
    // Without -save, a folder of its own in the system's temporary folder.
    if (saveFolder.empty()) {
        std::error_code ec;
        saveFolder = std::filesystem::temp_directory_path(ec) / "reluva-sfc-host";
    }
    game.setSaveFolder(saveFolder);
    // -net: multiplayer over the machine's network, so two copies of the
    // tool (each with a save folder of its own) can play each other.
    if (network)
        game.directPlay().setNetwork(net::systemSockets(), net::systemBroadcastAddresses());
    game.winsock().setSockets(net::systemSockets());
    std::printf("writes go to %s\n", saveFolder.string().c_str());
    x86::Cpu &cpu = game.cpu();
    x86::Win32Process &process = game.process();
    // -fixed-clock: the clock follows the instructions run, 40,000 to a
    // millisecond, the program's sleeps and the time every thread waits,
    // not the time: a run with the
    // same input shows the same frames, so input given at frames lands
    // where it did, however fast the host is.
    // When every thread waits, the clock moves on a millisecond a time,
    // as nothing runs to move it.
    // It starts where the last run with the same save folder stopped
    // (kept in the folder's reluva-clock.txt), as a machine's clock goes on
    // from one run of a program to the next: the game keeps the count in
    // its campaign saves, and when a run starts from 0 below a save's, the
    // next mission accepted never starts
    // (docs/engines/sfc/games/sfc2/systems/hosting.md). A new folder starts
    // at 0, so a run with one shows the same frames each time.
    std::atomic<uint64_t> idleMs{0};
    const std::filesystem::path clockFile = saveFolder / "reluva-clock.txt";
    uint64_t clockStart = 0;
    if (fixedClock) {
        std::ifstream(clockFile) >> clockStart;
        process.milliseconds = [&cpu, &process, &idleMs, clockStart] {
            constexpr uint64_t kInstructionsPerMs = 40000;
            return uint32_t(clockStart + cpu.executed() / kInstructionsPerMs + process.slept() + idleMs);
        };
        process.idleTick = [&idleMs] { ++idleMs; };
        // And the date, from 1 January 2001, moving on with that clock.
        game.wallClockMicros = [&process] {
            constexpr uint64_t kStart = 978307200ull * 1000000; // 2001-01-01, in microseconds since 1970
            return kStart + uint64_t(process.milliseconds()) * 1000;
        };
    }
    process.output = [](const std::string &text) { std::printf("program: %s", text.c_str()); };
    game.fileOpened = [quiet](const std::string &path, bool found) {
        if (!quiet || !found)
            std::printf("open %s%s\n", path.c_str(), found ? "" : "  (not found)");
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
    struct RateMark {
        std::chrono::steady_clock::time_point at;
        uint64_t instructions = 0;
        uint64_t clockMs = 0;
        uint64_t sleptMs = 0;
    };
    RateMark rateStart;
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
        if (rateTo && (frames == rateFrom || frames == rateTo)) {
            const RateMark now{std::chrono::steady_clock::now(), cpu.executed(), process.milliseconds(),
                               process.slept()};
            if (frames == rateFrom) {
                rateStart = now;
            } else {
                const double seconds = std::chrono::duration<double>(now.at - rateStart.at).count();
                const double ran = double(now.instructions - rateStart.instructions);
                std::printf("between frames %llu and %llu: %.0f million instructions in %.2f s (%.1f million a "
                            "second, %.1f frames a second); the program's clock %llu ms on, %llu ms of it slept\n",
                            static_cast<unsigned long long>(rateFrom), static_cast<unsigned long long>(rateTo),
                            ran / 1e6, seconds, seconds > 0 ? ran / seconds / 1e6 : 0.0,
                            seconds > 0 ? double(rateTo - rateFrom) / seconds : 0.0,
                            static_cast<unsigned long long>(now.clockMs - rateStart.clockMs),
                            static_cast<unsigned long long>(now.sleptMs - rateStart.sleptMs));
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
    constexpr uint32_t kImageStart = 0x400000;
    constexpr uint32_t kImageEnd = 0x800000;
    std::vector<bool> ran(newTo ? kImageEnd - kImageStart : 0);
    std::vector<uint32_t> firstRun;                 // in order, between the frames
    std::unordered_map<uint32_t, uint64_t> profile; // by address / 16
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    // With -calls, each call into a stand-in from the moment the program
    // reaches an address, and the interface each QueryInterface asks for.
    bool logging = false;
    cpu.trace = [&](x86::Cpu &c) {
        // With -writes, each change to a watched word, after the
        // instruction before this one made it.
        for (auto &[at, last] : writes)
            if (c.valid(at, 4))
                if (const uint32_t now = c.read32(at); now != last) {
                    std::printf("write %08x: %x -> %x after %08x\n", at, last, now,
                                recent[(recentAt + recent.size() - 1) % recent.size()]);
                    last = now;
                }
        recent[recentAt++ % recent.size()] = c.eip;
        if (newTo && c.eip >= kImageStart && c.eip < kImageEnd && !ran[c.eip - kImageStart]) {
            ran[c.eip - kImageStart] = true;
            if (frames >= newFrom && frames < newTo)
                firstRun.push_back(c.eip);
        }
        if (profileTo && frames >= profileFrom && frames < profileTo)
            ++profile[c.eip >> 4];
        if (callsFrom && c.eip == callsFrom)
            logging = true;
        if (std::find(watch.begin(), watch.end(), c.eip) != watch.end()) {
            std::printf("at %08x: eax %x ebx %x ecx %x edx %x esi %x edi %x ebp %x esp %x; stack", c.eip,
                        c.reg[x86::Cpu::EAX], c.reg[x86::Cpu::EBX], c.reg[x86::Cpu::ECX], c.reg[x86::Cpu::EDX],
                        c.reg[x86::Cpu::ESI], c.reg[x86::Cpu::EDI], c.reg[x86::Cpu::EBP], c.reg[x86::Cpu::ESP]);
            for (uint32_t i = 0; i < 8; ++i)
                std::printf(" %x", c.read32(c.reg[x86::Cpu::ESP] + 4 * i));
            std::printf("\n");
            // The callers, by the frame pointers' chain: right where the
            // functions on it keep EBP as their frame, as the game's do.
            std::printf("  callers:");
            for (uint32_t frame = c.reg[x86::Cpu::EBP], depth = 0;
                 depth < 16 && frame >= c.reg[x86::Cpu::ESP] && c.valid(frame, 8); ++depth) {
                std::printf(" %x", c.read32(frame + 4));
                const uint32_t next = c.read32(frame);
                if (next <= frame)
                    break;
                frame = next;
            }
            std::printf("\n");
            // What EAX points to (an object a call returned, say).
            if (const uint32_t p = c.reg[x86::Cpu::EAX]; p >= 0x10000 && c.valid(p, 128)) {
                std::printf("  eax->");
                for (uint32_t k = 0; k < 32; ++k)
                    std::printf(" %x", c.read32(p + 4 * k));
                std::printf("\n");
            }
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
                // and as text, where it starts as some
                const std::string text = c.string(p, 64);
                if (text.size() >= 3 &&
                    std::all_of(text.begin(), text.end(), [](char ch) { return ch >= 0x20 && ch < 0x7f; }))
                    std::printf("  \"%s\"", text.c_str());
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
        // The names a module or function is looked for by.
        if (name->find("LoadLibrary") != std::string::npos || name->find("GetModuleHandle") != std::string::npos)
            if (c.valid(c.arg(0), 1))
                std::printf("  \"%s\"", c.string(c.arg(0), 260).c_str());
        // The path a file or folder function is given (UTF-16 for a W one).
        if ((name->find("File") != std::string::npos || name->find("Directory") != std::string::npos) &&
            name->find("!") != std::string::npos && c.arg(0) >= 0x10000 && c.valid(c.arg(0), 2)) {
            std::string path;
            if (name->back() == 'W')
                for (uint32_t at = c.arg(0); path.size() < 260 && c.valid(at, 2) && c.read16(at); at += 2)
                    path.push_back(char(c.read16(at) < 0x80 ? c.read16(at) : '?'));
            else
                path = c.string(c.arg(0), 260);
            std::printf("  \"%s\"", path.c_str());
        }
        if (name->find("GetProcAddress") != std::string::npos && c.arg(1) >= 0x10000 && c.valid(c.arg(1), 1))
            std::printf("  \"%s\"", c.string(c.arg(1), 260).c_str());
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
    // Which module the last instruction is in: a mission's DLL is loaded
    // wherever there's room, so its addresses say nothing alone.
    // The last instruction traced, not the processor's EIP, which a fault
    // leaves elsewhere.
    if (err && recentAt) {
        const uint32_t at = recent[(recentAt - 1) % recent.size()];
        bool found = false;
        for (const x86::Win32Process::Module *m : game.process().modules())
            if (at >= m->base && at - m->base < m->size) {
                std::printf("in %s, at +0x%x (loaded at 0x%x)\n", m->name.c_str(), at - m->base, m->base);
                found = true;
            }
        if (!found && !cpu.nativeName(at)) { // a stand-in names itself
            std::printf("in no module; those loaded:\n");
            for (const x86::Win32Process::Module *m : game.process().modules())
                std::printf("  %s at 0x%x, 0x%x bytes\n", m->name.c_str(), m->base, m->size);
        }
    }
    if (fixedClock)
        std::ofstream(clockFile) << process.milliseconds() << "\n";
    std::printf("%llu exceptions raised\n", static_cast<unsigned long long>(raised));
    std::printf("%llu instructions in %.2f s (%.1f million a second); %llu frames shown\n",
                static_cast<unsigned long long>(executed), seconds,
                seconds > 0 ? double(executed) / seconds / 1e6 : 0.0, static_cast<unsigned long long>(frames));
    std::printf("last instructions:");
    for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
        std::printf(" %08x", recent[i % recent.size()]);
    std::printf("\n");
    if (profileTo) {
        std::vector<std::pair<uint64_t, uint32_t>> hot;
        uint64_t all = 0;
        for (const auto &[block, count] : profile) {
            hot.emplace_back(count, block << 4);
            all += count;
        }
        std::sort(hot.rbegin(), hot.rend());
        std::printf("instructions run between frames %llu and %llu: %llu; the most run, by 16 bytes:\n",
                    static_cast<unsigned long long>(profileFrom), static_cast<unsigned long long>(profileTo),
                    static_cast<unsigned long long>(all));
        for (size_t i = 0; i < hot.size() && i < 40; ++i)
            std::printf("  %08x %llu\n", hot[i].second, static_cast<unsigned long long>(hot[i].first));
    }
    if (newTo) {
        // Runs of consecutive first-run instructions, as where each began.
        std::printf("code first run between frames %llu and %llu:", static_cast<unsigned long long>(newFrom),
                    static_cast<unsigned long long>(newTo));
        for (size_t i = 0; i < firstRun.size(); ++i)
            if (i == 0 || firstRun[i] < firstRun[i - 1] || firstRun[i] - firstRun[i - 1] > 16)
                std::printf(" %08x", firstRun[i]);
        std::printf("\n");
    }
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
// following what the host run reports; or, given a file rather than the
// game's folder, that program's (a mission's DLL), at its preferred base.
int cmdDisasm(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool sfc disasm <game folder or program> <start> <end>\n");
        return 2;
    }
    const std::filesystem::path given = argv[2];
    const auto path = std::filesystem::is_regular_file(given) ? std::optional(given)
                                                              : fs::findFile(given, sfc::GameProcess::kProgram);
    std::vector<uint8_t> file;
    PeImage pe;
    if (!path || !readFile(*path, file) || parsePe(file, pe)) {
        std::fprintf(stderr, "can't read a program at %s\n", argv[2]);
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

// sfc patch: copies an install and applies the 2.0.3.6 patch to the copy.
int cmdPatch(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(
            stderr,
            "usage: reluva-tool sfc patch <game folder> <sfc2patch20022036.exe or its files> <out folder> [-calls]\n");
        return 2;
    }
    const std::filesystem::path out = argv[4];
    std::error_code ec;
    if (std::filesystem::exists(out, ec) && !std::filesystem::is_empty(out, ec)) {
        std::fprintf(stderr, "%s isn't empty\n", out.string().c_str());
        return 1;
    }
    std::filesystem::create_directories(out / "SFC2", ec);
    std::filesystem::copy(argv[2], out / "SFC2", std::filesystem::copy_options::recursive, ec);
    if (ec) {
        std::fprintf(stderr, "can't copy %s: %s\n", argv[2], ec.message().c_str());
        return 1;
    }
    // The patch's download, unpacked first, or a folder of its files.
    std::filesystem::path patchFiles = argv[3];
    if (std::filesystem::is_regular_file(patchFiles, ec)) {
        patchFiles = out / "patch";
        if (Error e = sfc::Patcher::unpack(argv[3], patchFiles)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            return 1;
        }
    }
    sfc::Patcher patcher(out / "SFC2", patchFiles, out / "scratch");
    for (int a = 5; a < argc; ++a)
        if (std::string(argv[a]) == "-calls")
            patcher.logCalls = true;
    patcher.output = [](const std::string &text) { std::fputs(text.c_str(), stdout); };
    uint32_t exitCode = 0;
    const auto start = std::chrono::steady_clock::now();
    const Error err = patcher.run(exitCode);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (err) {
        std::printf("stopped: %s (%.1f s)\n", err.message.c_str(), seconds);
        return 1;
    }
    std::printf("Patch.exe ended with %u (%.1f s)\n", exitCode, seconds);
    return exitCode == 0 ? 0 : 1;
}

const Commands
    kCommands("sfc",
              {
                  {"host",
                   "<game folder> [-limit n] [-trace n] [-frame out.png] [-quiet] [-save folder] [-calls address] "
                   "[-watch address]... [-type frame text] [-click frame x y] [-shot frame out.png]... [-sound folder] "
                   "[-writes address]... [-newcode from to] [-profile from to] [-rate from to] [-fixed-clock] "
                   "[-key frame name]... "
                   "[-net]",
                   "run StarFleet2.exe in the x86 emulator, headless, and say how far it gets; keys and clicks "
                   "given at frames it shows",
                   cmdHost},
                  {"disasm", "<game folder or program> <start> <end>",
                   "disassemble StarFleet2.exe (or another program) from one address to another", cmdDisasm},
                  {"patch", "<game folder> <sfc2patch20022036.exe or its files> <out folder> [-calls]",
                   "copy an install and apply the 2.0.3.6 patch to the copy, running its Patch.exe", cmdPatch},
              });

} // namespace
