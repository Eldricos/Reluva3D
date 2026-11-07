// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for running LEGO Alpha Team's own code headless
// (docs/engines/netimmerse/README.md, TESTING.md and
// REVERSE_ENGINEERING.md):
//
//   reluva-tool netimmerse at-run <game folder> [options]
//       runs the game in the x86 emulator with scripted input, tracing and
//       frames; -fixed-clock makes a run repeat exactly
//   reluva-tool netimmerse at-sweep <game folder> <out folder> [options] [mission...]
//       runs many missions at once (an at-run each), solved or not

#include "common/fs.h"
#include "engines/netimmerse/alphateam/game_process.h"
#include "engines/netimmerse/pac_archive.h"
#include "tools/tool.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {



// LoadComp's interface buttons (DDUIManager::Buttons, an NiTArray of
// DDUIButton pointers: its items at +4, its count at +0xc). A button's name
// is at +8; its rectangle at +0x40 (x0, y0, x1, y1, 0..1 from the bottom
// left); whether it's enabled and clickable at +0x50 and +0x51.
void printButtons(reluva::x86::Win32Process& p, int width, int height) {
    reluva::x86::Cpu& cpu = p.cpu();
    const auto* load = p.module("LoadComp.dll");
    const uint32_t array =
        load ? load->exportAddress("?Buttons@DDUIManager@@1V?$NiTArray@V?$NiPointer@VDDUIButton@@@@@@A") : 0;
    if (!array || !cpu.valid(array, 16)) {
        std::printf("-buttons: no DDUIManager::Buttons\n");
        return;
    }
    const uint32_t items = cpu.read32(array + 4), count = cpu.read32(array + 0xc);
    auto f32 = [&](uint32_t a) {
        float v;
        const uint32_t w = cpu.read32(a);
        std::memcpy(&v, &w, 4);
        return v;
    };
    for (uint32_t i = 0; i < count && i < 4096 && cpu.valid(items + 4 * i, 4); ++i) {
        const uint32_t b = cpu.read32(items + 4 * i);
        if (!b || !cpu.valid(b, 0x54))
            continue;
        const uint32_t name = cpu.read32(b + 8);
        const float x0 = f32(b + 0x40), y0 = f32(b + 0x44), x1 = f32(b + 0x48), y1 = f32(b + 0x4c);
        std::printf("button %08x %-28s %s%s at %d,%d (%d..%d x %d..%d)\n", b,
                    name && cpu.valid(name, 1) ? cpu.string(name).c_str() : "?", cpu.read8(b + 0x50) ? "enabled" : "disabled",
                    cpu.read8(b + 0x51) ? " clickable" : "", int((x0 + x1) / 2 * float(width)),
                    int((1 - (y0 + y1) / 2) * float(height)), int(x0 * float(width)), int(x1 * float(width)),
                    int((1 - y1) * float(height)), int((1 - y0) * float(height)));
    }
}

// A behaviour state machine (BehaviorComp's DDParent family: states,
// sub-machines, choice points, ports and transitions), walked from some of
// its nodes by calling their virtual functions as the program does. The
// slots are BehaviorComp's base vtables' (DDParent: 0 getNumTrans,
// 1 getTrans, 3 isPort, 4 isState, 5 isHFSM, 6 getParent; DDPort: 8 getTrans;
// DDTransition: 8 getToObj, 9 getNumTriggers, 11 getTrigger, 13 getType).
// Each node and transition is printed with its vtable, which names its
// generated class in the behaviour DLL.
void printGraph(reluva::x86::Cpu& cpu, const std::vector<uint32_t>& roots,
                       const std::vector<std::pair<uint32_t, uint32_t>>& scans) {
    using reluva::x86::Cpu;
    auto vcall = [&](uint32_t object, int slot, std::vector<uint32_t> args = {}) -> uint32_t {
        if (!object || !cpu.valid(object, 4) || !cpu.valid(cpu.read32(object) + 4 * uint32_t(slot), 4))
            return 0;
        const auto saved = cpu.reg;
        cpu.reg[Cpu::ECX] = object;
        uint32_t result = 0;
        const bool failed = bool(cpu.call(cpu.read32(cpu.read32(object) + 4 * uint32_t(slot)), args, result));
        cpu.reg = saved;
        return failed ? 0 : result;
    };
    std::vector<uint32_t> todo(roots.begin(), roots.end());
    // A node of the family: its vtable's isPort and isHFSM (slots 3 and 5)
    // are the same functions as the first root's (BehaviorComp's, which
    // only ports and machines override; those are reached through
    // transitions and parents instead).
    if (!roots.empty() && cpu.valid(roots[0], 4)) {
        const uint32_t known = cpu.read32(roots[0]);
        for (const auto& [lo, hi] : scans)
            for (uint32_t a = lo & ~3u; a + 4 <= hi; a += 4) {
                const uint32_t vt = cpu.valid(a, 4) ? cpu.read32(a) : 0;
                if (!vt || !cpu.valid(vt, 28) || vt == known)
                    continue;
                bool same = true;
                for (int slot : {3, 5})
                    same = same && cpu.read32(vt + 4 * uint32_t(slot)) == cpu.read32(known + 4 * uint32_t(slot));
                if (same)
                    todo.push_back(a);
            }
    }
    std::set<uint32_t> seen;
    while (!todo.empty()) {
        const uint32_t node = todo.back();
        todo.pop_back();
        if (!node || !seen.insert(node).second || !cpu.valid(node, 4))
            continue;
        const bool port = vcall(node, 3) & 0xff, state = vcall(node, 4) & 0xff, machine = vcall(node, 5) & 0xff;
        const uint32_t parent = vcall(node, 6);
        std::printf("node %08x vt %08x %s parent %08x\n", node, cpu.read32(node),
                    machine ? "machine" : state ? "state" : port ? "port" : "other", parent);
        todo.push_back(parent);
        std::vector<uint32_t> transitions;
        if (port) {
            transitions.push_back(vcall(node, 8));
        } else {
            const uint32_t n = vcall(node, 0);
            for (uint32_t i = 0; i < n && i < 64; ++i)
                transitions.push_back(vcall(node, 1, {i}));
        }
        for (uint32_t t : transitions) {
            if (!t)
                continue;
            // The target (getToObj) is a node itself, in its parent's machine.
            const uint32_t to = vcall(t, 8), triggers = vcall(t, 9);
            std::printf("  trans %08x vt %08x type %u -> %08x, on", t, cpu.read32(t), vcall(t, 13), to);
            for (uint32_t i = 0; i < triggers && i < 32; ++i)
                std::printf(" %x", vcall(t, 11, {i}));
            std::printf("\n");
            todo.push_back(to);
        }
    }
}

// The mission the first Adventure is on in a saved Game.txt ("P0Space"),
// or "" if there's none.
std::string savedSpace(const std::filesystem::path& gameTxt) {
    std::ifstream in(gameTxt);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key, value;
        if (words >> key >> value && key == "P0Space")
            return value;
    }
    return "";
}

// Writes a copy of the install's Etc.pac in which <space>.PUZ's entry
// points at <space>_SOLVED.PUZ's data (the directory's offset and size;
// the data itself is shared), for `at-run -solved`.
reluva::Error writeSolvedEtcPac(const std::filesystem::path& game, const std::string& space,
                                       const std::filesystem::path& out) {
    const auto path = reluva::fs::findFile(game, "Etc.pac");
    if (!path)
        return reluva::Error::fail("no Etc.pac in " + game.string());
    std::ifstream in(*path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 20 || std::memcmp(d.data(), "PACK", 4) != 0)
        return reluva::Error::fail("Etc.pac isn't a .pac archive");
    auto le32 = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24; };
    const uint32_t count = le32(16), dataStart = le32(12);
    std::string upper = space;
    for (char& c : upper)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string puzzle = upper + ".PUZ", solved = upper + "_SOLVED.PUZ";
    size_t puzzleAt = 0, at = 20;
    uint32_t solvedOffset = 0, solvedSize = 0;
    for (uint32_t i = 0; i < count && at < dataStart; ++i) {
        const size_t end = std::find(d.begin() + std::ptrdiff_t(at), d.begin() + dataStart, 0) - d.begin();
        const std::string name(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(end));
        const size_t fields = end + 1; // flags, offset, size, date
        if (name == puzzle)
            puzzleAt = fields;
        if (name == solved) {
            solvedOffset = le32(fields + 4);
            solvedSize = le32(fields + 8);
        }
        at = fields + 20;
    }
    if (!puzzleAt || !solvedSize)
        return reluva::Error::fail("Etc.pac has no " + puzzle + " and " + solved);
    for (int k = 0; k < 4; ++k) {
        d[puzzleAt + 4 + size_t(k)] = uint8_t(solvedOffset >> (8 * k));
        d[puzzleAt + 8 + size_t(k)] = uint8_t(solvedSize >> (8 * k));
    }
    std::ofstream o(out, std::ios::binary);
    o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    return o ? reluva::Error::success() : reluva::Error::fail("can't write " + out.string());
}

// at-sweep <game folder> <out folder> [-jobs n] [-quit frame] [-shots] [-solved] [mission...]:
// each mission (all 54 by default) from an Adventure saved at it, through
// the menus, its fly-over and Go with nothing placed, to a frame (3700):
// one `at-run` each, several at once, then which stopped where they should
// and which failed. Each mission's log and save go in <out>/<mission>.
// With -solved, each mission's puzzle is its _SOLVED.PUZ (the parts where
// they solve it), and a mission passes when the Adventure moves on from it
// (the game saves the next mission) by the frame (12000).
int cmdAtSweep(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool netimmerse at-sweep <game folder> <out folder> [-jobs n] [-quit frame] [-shots] "
                             "[-solved] [mission...]\n");
        return 2;
    }
    const std::filesystem::path game = argv[2], out = argv[3];
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    int quit = 0;
    bool shots = false, solved = false;
    std::vector<std::string> missions;
    for (int a = 4; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-jobs" && a + 1 < argc)
            jobs = unsigned(std::max(1, std::atoi(argv[++a])));
        else if (arg == "-quit" && a + 1 < argc)
            quit = std::atoi(argv[++a]);
        else if (arg == "-shots")
            shots = true;
        else if (arg == "-solved")
            solved = true;
        else
            missions.push_back(arg);
    }
    if (!quit)
        quit = solved ? 12000 : 3700;
    if (missions.empty()) { // the three later training missions (which have no _SOLVED.PUZ), then the Adventure's 50
        if (!solved)
            missions = {"M0020", "M0030", "M0040"};
        for (const auto& [first, last] : std::vector<std::pair<int, int>>{{1010, 1140}, {2010, 2160}, {3010, 3100}, {4010, 4070}})
            for (int m = first; m <= last; m += 10)
                missions.push_back("M" + std::to_string(m));
        for (const char* odd : {"M1065", "M1085", "M1095", "M1135"})
            missions.push_back(odd);
        std::sort(missions.begin(), missions.end());
    }
    // The route: past the demo, Adventure, the first slot, past the
    // mission's fly-over and goal, then Go. Frames shown late, as the
    // player shows them.
    const std::string route = "-limit 0 -fixed-clock -present-late -click 25 -point 1510@320,128 -click 1512 "
                              "-point 1600@180,124 -click 1602 -click 2400 -click 2500 -click 3000 -point 3196@572,434 "
                              "-click 3200";
    auto quoted = [](const std::filesystem::path& p) { return "\"" + p.string() + "\""; };
    std::vector<std::string> results(missions.size());
    std::vector<double> seconds(missions.size());
    std::atomic<size_t> next{0};
    const auto start = std::chrono::steady_clock::now();
    auto work = [&] {
        for (size_t i; (i = next++) < missions.size();) {
            const std::string& m = missions[i];
            const std::filesystem::path dir = out / m;
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir / "save", ec);
            {
                std::ofstream g(dir / "save" / "Game.txt", std::ios::binary);
                g << "##  Game.txt\r\n\r\nP0Space  " << m << "\r\nP0Time  1793119257\r\n\r\n##  End\r\n";
            }
            std::string cmd = quoted(reluva::tool::programPath()) + " netimmerse at-run " + quoted(game) + " " + route + " -quit " + std::to_string(quit) +
                              " -save " + quoted(dir / "save");
            if (solved)
                cmd += " -solved " + m + " -until-advance";
            if (shots) {
                std::filesystem::create_directories(dir / "shots", ec);
                cmd += " -shots " + quoted(dir / "shots") + " -every 100";
            }
            cmd += " > " + quoted(dir / "log.txt") + " 2>&1";
            const auto t0 = std::chrono::steady_clock::now();
            if (std::system(cmd.c_str()) == -1)
                std::fprintf(stderr, "%s: couldn't start at-run\n", m.c_str());
            seconds[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::ifstream log(dir / "log.txt");
            std::string line, last;
            bool advanced = false; // with -solved, that line is the result whatever follows
            while (std::getline(log, line))
                if (solved && line.rfind("advanced from", 0) == 0) {
                    last = line;
                    advanced = true;
                } else if (!advanced && (line.rfind("stopped at frame", 0) == 0 || line.rfind("failed", 0) == 0 ||
                                         line.rfind("-solved:", 0) == 0)) {
                    last = line;
                }
            results[i] = last.empty() ? "no result" : last;
            std::printf("%s %s (%.0f s)\n", m.c_str(), results[i].c_str(), seconds[i]);
            std::fflush(stdout);
        }
    };
    std::vector<std::thread> pool;
    for (unsigned j = 0; j < std::min<size_t>(jobs, missions.size()); ++j)
        pool.emplace_back(work);
    for (auto& t : pool)
        t.join();
    const std::string expected = solved ? "advanced from" : "stopped at frame " + std::to_string(quit) + ",";
    size_t passed = 0;
    for (size_t i = 0; i < missions.size(); ++i) {
        if (results[i].rfind(expected, 0) == 0)
            ++passed;
        else
            std::printf("not as expected: %s: %s\n", missions[i].c_str(), results[i].c_str());
    }
    if (solved)
        std::printf("%zu of %zu missions solved by their _SOLVED.PUZ (the Adventure moved on) by frame %d, %u at "
                    "once, in %.0f s\n",
                    passed, missions.size(), quit, jobs,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    else
        std::printf("%zu of %zu missions ran to frame %d, %u at once, in %.0f s\n", passed, missions.size(), quit, jobs,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return passed == missions.size() ? 0 : 1;
}

int cmdAtRun(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool netimmerse at-run <game folder> [-v] [-limit instructions] [-trace count] "
                             "[-memory MB] [-log] [-files] [-save folder] [-from frame] [-profile from-to] [-profile-addresses] [-present-late] [-shots folder] [-every frames] [-click frame[@x,y]]... [-move frame@dx,dy]... "
                             "[-key frame@vk]... [-quit frame] [-fixed-clock] [-clock-rate frame@n] [-watch dll!export]... [-point frame@x,y]... "
                             "[-calls frame@dll!export] [-dump frame@address,bytes]... [-solved space] [-until-advance]\n");
        return 2;
    }
    bool verbose = false;
    int traceCount = 0; // instructions kept for a failure (0: none, which keeps the instruction cache in use)
    uint64_t quitAt = 0;
    bool fixedClock = false;
    // -clock-rate frame@n: from that frame on, the fixed clock counts n
    // instructions a millisecond instead of 40,000 (the time it has
    // reached carries on).
    uint64_t clockFrom = 0, clockRate = 40000;
    bool files = false; // -files: each path the program asks for, and where it was found
    std::string saveDir; // -save: where what the program writes goes (else it may write nothing)
    // -solved <space>: the game reads <space>_SOLVED.PUZ (the puzzle with
    // its parts where they solve it) in place of <space>.PUZ.
    std::string solvedSpace;
    // -until-advance: stop once the save (in -save's folder) shows the
    // first Adventure on a mission other than the one it started at.
    bool untilAdvance = false;
    uint64_t advancedAt = 0;
    // -from: -watch, -calls, -log and -step look from this frame on (the
    // instructions are traced only from then, which is much slower).
    uint64_t traceFrom = 0;
    // -watch: calls to a function of the program's (an export), with what
    // they return, summed up per frame.
    std::vector<std::string> watches;
    // -calls: from a frame on, the next call of a function of the
    // program's, as a tree of the calls it makes.
    uint64_t callsFrame = 0;
    std::string callsOf;
    // -dump: memory as words (and as floats) when a frame is shown.
    struct Dump {
        uint64_t frame;
        uint32_t address, bytes;
    };
    std::vector<Dump> dumps;
    // -graph: the behaviour state machines reachable from some of their
    // nodes (BehaviorComp's DDParent objects), walked through their
    // virtual functions when a frame is shown.
    uint64_t graphFrame = 0;
    std::vector<uint32_t> graphRoots;
    std::vector<std::pair<uint32_t, uint32_t>> graphScan;
    // -buttons: the interface's buttons (LoadComp's DDUIManager::Buttons)
    // when a frame is shown, with where they are on the screen.
    std::vector<uint64_t> buttonFrames;
    uint64_t limit = 4000000000ull;
    uint32_t memoryMb = 1024;
    bool log = false;
    std::string shots;
    // -profile from-to: where the program is, sampled every millisecond
    // between those frames.
    uint64_t profileFrom = 0, profileTo = 0;
    bool presentLate = false; // -present-late: DirectX::presentLate, as the player has it
    std::atomic<bool> profiling{false}, profileDone{false};
    std::vector<uint32_t> profileSamples;
    bool profileAddresses = false; // -profile-addresses: also by 64-byte stretch of code
    std::thread profiler;
    uint64_t every = 30;
    // -click: a left click when a frame is shown, at a place on the screen
    // (where the pointer is if none is given).
    struct Click {
        uint64_t frame;
        int x = -1, y = -1;
    };
    std::vector<Click> clicks;
    // -move: the pointer moved by (dx, dy) when a frame is shown, as a mouse
    // moves it.
    std::vector<Click> moves;
    // -key: a key pressed (a Windows virtual-key code) when a frame is shown,
    // let go at the next.
    std::vector<Click> keys;
    // -point: the game's own pointer put at a place on the screen when a
    // frame is shown (DDMouse's position, 0..1 from the bottom left).
    std::vector<Click> points;
    // -step: input given when a behaviour's state starts waiting for an
    // event (BehaviorComp's DDBehavior::registerEvent): `event[+delay]@
    // click:x,y[*n] | point:x,y | key:vk | drag:x,y,x2,y2 | buttons` (the
    // last lists them; *n clicks again, up to n times in all, until a state
    // starts waiting for something new). Each time the event is waited for,
    // the next action given for it is taken, `delay` frames on (3 if none);
    // a listing of the buttons is given each time.
    struct StepAction {
        uint32_t event;
        int delay = 3;
        char kind; // 'c'lick, 'p'oint, 'k'ey, 'd'rag, 'b'uttons (list them)
        int x = -1, y = -1, x2 = -1, y2 = -1;
        int repeat = 1; // click:x,y*n
    };
    // A click repeated (every 40 frames, up to `left` more times) until a
    // state starts waiting for a new training step's event (0xea60..0xea7d,
    // LEGO Alpha Team's).
    struct Retry {
        uint64_t next, since;
        int left, x, y;
    };
    std::vector<Retry> retries;
    uint64_t lastRegistered = 0; // the frame a training step's event was last waited for
    struct Hold {
        uint64_t frame;
        uint64_t frames;
    };
    std::vector<Hold> holds;
    std::vector<StepAction> stepActions;
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-v")
            verbose = true;
        else if (arg == "-log")
            log = true;
        else if (arg == "-files")
            files = true;
        else if (arg == "-save" && a + 1 < argc)
            saveDir = argv[++a];
        else if (arg == "-from" && a + 1 < argc)
            traceFrom = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-shots" && a + 1 < argc)
            shots = argv[++a];
        else if (arg == "-click" && a + 1 < argc) {
            Click k{std::strtoull(argv[++a], nullptr, 0)};
            if (const char* at = std::strchr(argv[a], '@'))
                std::sscanf(at + 1, "%d,%d", &k.x, &k.y);
            clicks.push_back(k);
        } else if (arg == "-move" && a + 1 < argc) {
            Click k{std::strtoull(argv[++a], nullptr, 0), 0, 0};
            if (const char* at = std::strchr(argv[a], '@'))
                std::sscanf(at + 1, "%d,%d", &k.x, &k.y);
            moves.push_back(k);
        } else if (arg == "-key" && a + 1 < argc) {
            Click k{std::strtoull(argv[++a], nullptr, 0), 0, 0};
            if (const char* at = std::strchr(argv[a], '@'))
                k.x = int(std::strtol(at + 1, nullptr, 0));
            keys.push_back(k);
        } else if (arg == "-point" && a + 1 < argc) {
            Click k{std::strtoull(argv[++a], nullptr, 0), 0, 0};
            if (const char* at = std::strchr(argv[a], '@'))
                std::sscanf(at + 1, "%d,%d", &k.x, &k.y);
            points.push_back(k);
        } else if (arg == "-quit" && a + 1 < argc)
            quitAt = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-present-late")
            presentLate = true;
        else if (arg == "-solved" && a + 1 < argc)
            solvedSpace = argv[++a];
        else if (arg == "-until-advance")
            untilAdvance = true;
        else if (arg == "-fixed-clock")
            fixedClock = true;
        else if (arg == "-profile-addresses")
            profileAddresses = true;
        else if (arg == "-profile" && a + 1 < argc) {
            const std::string v = argv[++a];
            profileFrom = std::strtoull(v.c_str(), nullptr, 0);
            profileTo = v.find('-') == std::string::npos ? UINT64_MAX : std::strtoull(v.c_str() + v.find('-') + 1, nullptr, 0);
        }
        else if (arg == "-clock-rate" && a + 1 < argc) {
            const std::string v = argv[++a];
            clockFrom = std::strtoull(v.c_str(), nullptr, 0);
            clockRate = std::max<uint64_t>(1, std::strtoull(v.c_str() + v.find('@') + 1, nullptr, 0));
        }
        else if (arg == "-watch" && a + 1 < argc)
            watches.push_back(argv[++a]);
        else if (arg == "-dump" && a + 1 < argc) {
            Dump d{std::strtoull(argv[++a], nullptr, 0), 0, 64};
            if (const char* at = std::strchr(argv[a], '@'))
                std::sscanf(at + 1, "%x,%x", &d.address, &d.bytes);
            dumps.push_back(d);
        } else if (arg == "-step" && a + 1 < argc) {
            const std::string v = argv[++a];
            StepAction st{};
            st.event = uint32_t(std::strtoul(v.c_str(), nullptr, 16));
            st.delay = 3;
            if (const size_t plus = v.find('+'); plus != std::string::npos && plus < v.find('@'))
                st.delay = std::atoi(v.c_str() + plus + 1);
            const size_t at = v.find('@');
            const std::string action = at == std::string::npos ? "" : v.substr(at + 1);
            st.kind = action.empty() ? 0 : action[0];
            const size_t colon = action.find(':');
            if (const size_t star = action.find('*'); star != std::string::npos)
                st.repeat = std::max(1, std::atoi(action.c_str() + star + 1));
            if (colon != std::string::npos && st.kind == 'd')
                std::sscanf(action.c_str() + colon + 1, "%d,%d,%d,%d", &st.x, &st.y, &st.x2, &st.y2);
            else if (colon != std::string::npos && st.kind == 'k')
                st.x = int(std::strtol(action.c_str() + colon + 1, nullptr, 0));
            else if (colon != std::string::npos)
                std::sscanf(action.c_str() + colon + 1, "%d,%d", &st.x, &st.y);
            if (st.kind != 'c' && st.kind != 'p' && st.kind != 'k' && st.kind != 'd' && st.kind != 'b') {
                std::fprintf(stderr, "-step: event[+delay]@click:x,y|point:x,y|key:vk|drag:x,y,x2,y2|buttons, not %s\n",
                             v.c_str());
                return 2;
            }
            stepActions.push_back(st);
        } else if (arg == "-buttons" && a + 1 < argc) {
            buttonFrames.push_back(std::strtoull(argv[++a], nullptr, 0));
        } else if (arg == "-graph" && a + 1 < argc) {
            const std::string v = argv[++a];
            graphFrame = std::strtoull(v.c_str(), nullptr, 0);
            // Roots, or lo-hi: every object there whose vtable's first
            // seven entries are the vtable at the first root's.
            for (size_t at = v.find('@'); at != std::string::npos; at = v.find(',', at + 1)) {
                char* end = nullptr;
                const uint32_t lo = uint32_t(std::strtoul(v.c_str() + at + 1, &end, 16));
                if (*end == '-') {
                    graphScan.push_back({lo, uint32_t(std::strtoul(end + 1, nullptr, 16))});
                } else {
                    graphRoots.push_back(lo);
                }
            }
        } else if (arg == "-calls" && a + 1 < argc) {
            const std::string v = argv[++a];
            callsFrame = std::strtoull(v.c_str(), nullptr, 0);
            callsOf = v.substr(v.find('@') + 1);
        }
        else if (arg == "-every" && a + 1 < argc)
            every = std::max<uint64_t>(1, std::strtoull(argv[++a], nullptr, 0));
        else if (arg == "-limit" && a + 1 < argc)
            limit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = std::atoi(argv[++a]);
        else if (arg == "-memory" && a + 1 < argc)
            memoryMb = uint32_t(std::atoi(argv[++a]));
    }
    using reluva::netimmerse::alphateam::GameProcess;
    using reluva::x86::Cpu;
    GameProcess game(argv[2], std::min<uint32_t>(memoryMb, 3072));
    Cpu& cpu = game.cpu();
    if (!saveDir.empty())
        game.setSaveFolder(saveDir);
    cpu.setInstructionLimit(limit);
    game.process().output = [](const std::string& text) { std::printf("[program] %s\n", text.c_str()); };
    const std::string startSpace = saveDir.empty() ? "" : savedSpace(std::filesystem::path(saveDir) / "Game.txt");
    if (!solvedSpace.empty()) {
        // A copy of Etc.pac whose <space>.PUZ entry points at the solved
        // puzzle's data, put where the program reads Etc.pac from.
        const std::filesystem::path etc = saveDir.empty() ? std::filesystem::temp_directory_path() / "reluva_etc_solved.pac"
                                                         : std::filesystem::path(saveDir) / "Etc_solved.pac";
        if (reluva::Error err = writeSolvedEtcPac(argv[2], solvedSpace, etc)) {
            std::fprintf(stderr, "-solved: %s\n", err.message.c_str());
            return 1;
        }
        game.process().mapPath = [map = game.process().mapPath, etc](const std::string& path, bool writing)
            -> std::optional<std::filesystem::path> {
            const auto host = map(path, writing);
            if (host && !writing && reluva::fs::toLower(host->filename().string()) == "etc.pac")
                return etc;
            return host;
        };
    }
    if (files)
        game.process().mapPath = [map = game.process().mapPath](const std::string& path, bool writing) {
            const auto host = map(path, writing);
            std::printf("file %s %s -> %s\n", writing ? "write" : "read", path.c_str(),
                        host ? host->string().c_str() : "(none)");
            return host;
        };
    if (files)
        game.process().registryWritten = [&game](const std::string& key, const std::string& name) {
            uint32_t type = 0;
            const auto value = game.process().registryValue(key, name, &type);
            std::string shown;
            for (size_t i = 0; value && i < value->size() && i < 64; ++i) {
                char hex[4];
                std::snprintf(hex, sizeof hex, "%02x", (*value)[i]);
                shown += hex;
            }
            std::printf("registry write %s\\%s (type %u, %zu bytes) %s\n", key.c_str(), name.c_str(), type,
                        value ? value->size() : 0, shown.c_str());
        };
    // -fixed-clock: the clock follows the instructions run (40,000 to a
    // millisecond, or -clock-rate's) and the main thread's sleeps, not the
    // time, so a run with the same input repeats exactly.
    uint64_t clockBaseInstructions = 0, clockBaseMs = 0, clockPerMs = 40000;
    if (fixedClock)
        game.process().milliseconds = [&cpu, &game, &clockBaseInstructions, &clockBaseMs, &clockPerMs] {
            return uint32_t(clockBaseMs + (cpu.executed() - clockBaseInstructions) / clockPerMs +
                            game.process().slept());
        };
    // -shots: the first frame shown and every `every`th, as TGA files.
    uint64_t framesShown = 0;
    auto saveFrame = [&shots](const reluva::x86::DirectX::Frame& f) {
        char name[64];
        std::snprintf(name, sizeof name, "/frame%05llu.tga", (unsigned long long)f.number);
        std::FILE* out = std::fopen((shots + name).c_str(), "wb");
        if (!out)
            return;
        uint8_t header[18] = {};
        header[2] = 2; // uncompressed true colour
        header[12] = uint8_t(f.width), header[13] = uint8_t(f.width >> 8);
        header[14] = uint8_t(f.height), header[15] = uint8_t(f.height >> 8);
        header[16] = 32;
        header[17] = 0x28; // top-down, 8 alpha bits
        std::fwrite(header, 1, sizeof header, out);
        std::fwrite(f.pixels.data(), 4, f.pixels.size(), out); // 0xAARRGGBB is BGRA in memory
        std::fclose(out);
    };
    struct Watch {
        std::string name;
        uint32_t address = 0;
        std::map<uint32_t, unsigned> results; // this frame's
        std::vector<std::pair<uint32_t, uint32_t>> pending; // return address, esp
        std::vector<std::array<uint32_t, 6>> args;           // this frame's first calls': ecx, the stack's, the caller
        std::vector<double> st0;                              // this frame's first returns' x87 top of stack
    };
    // An argument, as text if it points to some.
    auto argText = [&cpu](uint32_t v) {
        std::string t;
        for (uint32_t a = v; cpu.valid(a, 1) && t.size() < 48; ++a) {
            const uint8_t ch = cpu.read8(a);
            if (!ch)
                break;
            if (ch < 32 || ch > 126) {
                t.clear();
                break;
            }
            t += char(ch);
        }
        char b[16];
        std::snprintf(b, sizeof b, "%x", v);
        return t.size() >= 3 ? std::string(b) + "\"" + t + "\"" : std::string(b);
    };
    std::vector<Watch> watched;
    std::function<std::string(uint32_t)> symbolOf; // `symbol` below
    std::function<void(Cpu&)> traceFn;             // installed from frame `traceFrom`
    game.directX().presentLate = presentLate;
    game.directX().presented = [&](const reluva::x86::DirectX::Frame& f) {
        ++framesShown;
        if (profileFrom && framesShown == profileFrom && !profiler.joinable()) {
            profiling = true;
            profiler = std::thread([&] {
                while (!profileDone) {
                    if (profiling)
                        profileSamples.push_back(uint32_t(*static_cast<volatile uint32_t*>(&cpu.eip))); // racy, as a sample may be
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        }
        if (profiling && framesShown >= profileTo)
            profiling = false;
        if (traceFrom && framesShown == traceFrom && traceFn)
            cpu.trace = traceFn;
        if (fixedClock && clockFrom && framesShown == clockFrom) {
            clockBaseMs += (cpu.executed() - clockBaseInstructions) / clockPerMs;
            clockBaseInstructions = cpu.executed();
            clockPerMs = clockRate;
        }
        for (Watch& w : watched) {
            if (w.results.empty())
                continue;
            std::printf("frame %llu: %s returned", (unsigned long long)f.number, w.name.c_str());
            for (const auto& [value, count] : w.results)
                std::printf(" 0x%x (x%u)", value, count);
            std::printf(", given");
            for (const auto& a : w.args)
                std::printf(" (this %x: %s %s %s %s; from %s)", a[0], argText(a[1]).c_str(), argText(a[2]).c_str(),
                            argText(a[3]).c_str(), argText(a[4]).c_str(), symbolOf(a[5]).c_str());
            std::printf(", st(0)");
            for (double v : w.st0)
                std::printf(" %g", v);
            w.st0.clear();
            std::printf("\n");
            w.results.clear();
            w.args.clear();
        }
        reluva::x86::Win32Process& p = game.process();
        for (const Click& k : moves)
            if (f.number == k.frame)
                p.mouseMoveTo(p.cursorX + k.x, p.cursorY + k.y);
        for (Retry& r : retries) { // stopped by any state since
            if (r.left <= 0 || f.number != r.next)
                continue;
            if (lastRegistered > r.since) {
                r.left = 0;
                continue;
            }
            std::printf("step: clicking %d,%d again on frame %llu\n", r.x, r.y, (unsigned long long)f.number);
            points.push_back({f.number, r.x, r.y});
            clicks.push_back({f.number + 2, -1, -1});
            --r.left;
            r.next = f.number + 40;
        }
        for (const Hold& h : holds) { // a drag: the button held from frame to frame + frames
            if (f.number == h.frame)
                p.mouseButton(0, true);
            else if (f.number == h.frame + h.frames)
                p.mouseButton(0, false);
        }
        for (const Click& k : clicks) {
            if (f.number == k.frame) {
                if (k.x >= 0 && k.y >= 0)
                    p.mouseMoveTo(k.x, k.y);
                p.mouseButton(0, true);
                std::printf("clicked at %d,%d on frame %llu\n", p.cursorX, p.cursorY, (unsigned long long)f.number);
            } else if (f.number == k.frame + 1) {
                p.mouseButton(0, false);
            }
        }
        for (const Click& k : points) {
            if (f.number != k.frame)
                continue;
            const auto* load = p.module("LoadComp.dll");
            const uint32_t setPosition = load ? load->exportAddress("?SetPosition@DDMouse@@SAXABVNiPoint3@@@Z") : 0;
            const uint32_t point = p.memory().alloc(12);
            const float xyz[3] = {float(k.x) / float(f.width), 1 - float(k.y) / float(f.height), 0};
            std::memcpy(cpu.memory().data() + point, xyz, 12);
            uint32_t ignored = 0;
            if (!setPosition || cpu.call(setPosition, {point}, ignored))
                std::printf("-point: couldn't set the pointer on frame %llu\n", (unsigned long long)f.number);
        }
        if (std::find(buttonFrames.begin(), buttonFrames.end(), f.number) != buttonFrames.end())
            printButtons(game.process(), f.width, f.height);
        if (graphFrame && f.number == graphFrame)
            printGraph(cpu, graphRoots, graphScan);
        for (const Dump& d : dumps) {
            if (f.number != d.frame)
                continue;
            for (uint32_t at = d.address; at < d.address + d.bytes; at += 16) {
                std::printf("%08x:", at);
                for (uint32_t i = 0; i < 16; i += 4)
                    std::printf(" %08x", cpu.valid(at + i, 4) ? cpu.read32(at + i) : 0);
                std::printf("  ");
                for (uint32_t i = 0; i < 16; i += 4) {
                    const uint32_t v = cpu.valid(at + i, 4) ? cpu.read32(at + i) : 0;
                    float fl;
                    std::memcpy(&fl, &v, 4);
                    std::printf(" %g", double(fl));
                }
                std::printf("\n");
            }
        }
        for (const Click& k : keys)
            if (f.number == k.frame || f.number == k.frame + 1)
                p.key(uint32_t(k.x), 0, f.number == k.frame);
        if (!shots.empty() && (f.number == 1 || f.number % every == 0))
            saveFrame(f);
        if (quitAt && f.number >= quitAt)
            cpu.requestStop();
        if (untilAdvance && !saveDir.empty() && f.number % 25 == 0 && !advancedAt) {
            const std::string now = savedSpace(std::filesystem::path(saveDir) / "Game.txt");
            if (!startSpace.empty() && !now.empty() && now != startSpace) {
                advancedAt = f.number;
                std::printf("advanced from %s to %s by frame %llu\n", startSpace.c_str(), now.c_str(),
                            (unsigned long long)f.number);
                cpu.requestStop();
            }
        }
    };

    if (reluva::Error e = game.load()) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("loaded:");
    for (const std::string& m : game.loadOrder())
        std::printf(" %s@%08x", m.c_str(), game.process().module(m)->base);
    std::printf("\n");
    // What nothing stands in for yet, by DLL.
    std::map<std::string, std::vector<std::string>> missing;
    for (const std::string& m : game.process().missingImports())
        missing[m.substr(0, m.find('!'))].push_back(m.substr(m.find('!') + 1));
    for (const auto& [dll, names] : missing) {
        std::printf("not stood in for: %s, %zu imports\n", dll.c_str(), names.size());
        if (verbose)
            for (const std::string& n : names)
                std::printf("    %s\n", n.c_str());
    }

    // Where a failure happened: the last instructions (as module+offset)
    // and the last calls to stood-in functions.
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    std::vector<std::string> calls(16);
    size_t recentAt = 0, callsAt = 0;
    // -log: every stand-in called, in order, repeats counted.
    std::string lastLogged;
    unsigned repeats = 0;
    auto flushLog = [&] {
        if (repeats > 1)
            std::printf("  (x%u)\n", repeats);
        repeats = 0;
    };
    // An address as module!export+offset (the nearest export at or below it).
    std::map<std::string, std::vector<std::pair<uint32_t, std::string>>> exportsOf;
    auto symbol = [&](uint32_t a) {
        if (const std::string* n = cpu.nativeName(a))
            return "[" + *n + "]";
        for (const auto* m : game.process().modules()) {
            if (a < m->base || a >= m->base + m->size)
                continue;
            auto& list = exportsOf[m->name];
            if (list.empty()) {
                for (const auto& e : m->image.exports)
                    if (!e.name.empty() && e.forwarder.empty())
                        list.push_back({m->base + e.rva, e.name});
                std::sort(list.begin(), list.end());
                list.push_back({0xffffffffu, ""}); // never empty again
            }
            auto it = std::upper_bound(list.begin(), list.end(), std::make_pair(a, std::string("\xff")));
            char b[64];
            if (it != list.begin() && std::prev(it)->first <= a && !std::prev(it)->second.empty()) {
                --it;
                std::snprintf(b, sizeof b, "+%x", a - it->first);
                return m->name + "!" + it->second + (a == it->first ? "" : b);
            }
            std::snprintf(b, sizeof b, "+%x", a - m->base);
            return m->name + b;
        }
        // A DLL the program mapped itself (DDPackedDirFileLib's loader does,
        // for behaviours): its image is in memory, headers and all.
        for (uint32_t base = a & ~0xfffu; base + 0x400000 > a && base >= 0x1000; base -= 0x1000) {
            if (!cpu.valid(base, 0x400) || cpu.read16(base) != 0x5a4d)
                continue;
            const uint32_t pe = base + cpu.read32(base + 0x3c);
            if (!cpu.valid(pe, 0x100) || cpu.read32(pe) != 0x4550)
                continue;
            const uint32_t size = cpu.read32(pe + 0x50), exportRva = cpu.read32(pe + 0x78);
            if (a >= base + size)
                break;
            std::string name = "image@" + std::to_string(base);
            auto& list = exportsOf[name];
            if (list.empty() && exportRva && cpu.valid(base + exportRva, 40)) {
                const uint32_t dir = base + exportRva;
                char n[64] = {};
                for (int i = 0; i < 63 && cpu.valid(base + cpu.read32(dir + 12) + uint32_t(i), 1); ++i)
                    if (!(n[i] = char(cpu.read8(base + cpu.read32(dir + 12) + uint32_t(i)))))
                        break;
                list.push_back({0, n}); // the DLL's own name first
                const uint32_t count = cpu.read32(dir + 24), functions = base + cpu.read32(dir + 28);
                const uint32_t names = base + cpu.read32(dir + 32), ordinals = base + cpu.read32(dir + 36);
                for (uint32_t i = 0; i < count && i < 4096; ++i) {
                    std::string e;
                    for (uint32_t at = base + cpu.read32(names + 4 * i); cpu.valid(at, 1) && cpu.read8(at); ++at)
                        e += char(cpu.read8(at));
                    list.push_back({base + cpu.read32(functions + 4 * cpu.read16(ordinals + 2 * i)), e});
                }
                std::sort(list.begin() + 1, list.end());
            }
            const std::string dll = list.empty() ? name : list[0].second;
            char b[64];
            const std::pair<uint32_t, std::string>* best = nullptr;
            for (size_t i = 1; i < list.size(); ++i)
                if (list[i].first <= a)
                    best = &list[i];
            if (best) {
                std::snprintf(b, sizeof b, "+%x", a - best->first);
                return dll + "!" + best->second + (a == best->first ? "" : b);
            }
            std::snprintf(b, sizeof b, "+%x", a - base);
            return dll + b;
        }
        char b[16];
        std::snprintf(b, sizeof b, "%08x", a);
        return std::string(b);
    };
    symbolOf = symbol;
    uint32_t callsAddress = 0;
    if (!callsOf.empty()) {
        const size_t bang = callsOf.find('!');
        const auto* m = bang == std::string::npos ? nullptr : game.process().module(callsOf.substr(0, bang));
        callsAddress = m ? m->exportAddress(callsOf.substr(bang + 1)) : 0;
        if (!callsAddress)
            std::printf("-calls: no %s\n", callsOf.c_str());
    }
    // The tree being recorded: the calls open, each as the stack pointer
    // holding its return address (it's over when the stack is above that).
    std::vector<uint32_t> openCalls;
    bool treeDone = false;
    uint32_t lastEip = 0, lastEsp = 0;
    std::string lastLine;
    unsigned lineRepeats = 0, linesPrinted = 0;
    auto treeLine = [&](const std::string& line) {
        if (line == lastLine) {
            ++lineRepeats;
            return;
        }
        if (lineRepeats)
            std::printf("%*s(x%u)\n", int(2 * openCalls.size()), "", lineRepeats + 1);
        lineRepeats = 0;
        lastLine = line;
        if (++linesPrinted <= 20000)
            std::printf("%s\n", line.c_str());
    };
    for (const std::string& name : watches) {
        const size_t bang = name.find('!');
        const auto* m = bang == std::string::npos ? nullptr : game.process().module(name.substr(0, bang));
        // dll!export, or an address (0x...) for code the program loaded itself.
        const uint32_t address = name.rfind("0x", 0) == 0 ? uint32_t(std::strtoul(name.c_str(), nullptr, 16))
                                 : m                       ? m->exportAddress(name.substr(bang + 1))
                                                           : 0;
        if (!address)
            std::printf("-watch: no %s\n", name.c_str());
        else
            watched.push_back({name, address, {}, {}, {}, {}});
    }
    const auto* behaviorComp = game.process().module("BehaviorComp.dll");
    const uint32_t registerEvent =
        stepActions.empty() || !behaviorComp
            ? 0
            : behaviorComp->exportAddress("?registerEvent@DDBehavior@@QAEXPAVDDHFSM@@HPAVDDTransition@@@Z");
    if (!stepActions.empty() && !registerEvent)
        std::printf("-step: no DDBehavior::registerEvent\n");
    if (traceCount > 0 || log || !watched.empty() || callsAddress || registerEvent)
        traceFn = [&](Cpu& c) {
            // -step: registerEvent(machine, event, transition), thiscall.
            if (registerEvent && c.eip == registerEvent) {
                const uint32_t event = c.read32(c.reg[Cpu::ESP] + 8);
                if (event >= 0xea60 && event <= 0xea7d) // LEGO Alpha Team's training steps
                    lastRegistered = framesShown;
                for (auto it = stepActions.begin(); it != stepActions.end(); ++it) {
                    if (it->event != event)
                        continue;
                    if (it->kind == 'b') { // every time, alongside the event's next action
                        buttonFrames.push_back(framesShown + uint64_t(it->delay));
                        continue;
                    }
                    const uint64_t at = framesShown + uint64_t(it->delay);
                    std::printf("step %x waited for on frame %llu: %c %d,%d on frame %llu\n", event,
                                (unsigned long long)framesShown, it->kind, it->x, it->y, (unsigned long long)at);
                    if (it->kind == 'd') { // press, move there over 20 frames, release
                        constexpr int kSteps = 20;
                        points.push_back({at, it->x, it->y});
                        // Each step both moves the mouse (so the program sees it
                        // moving with the button down) and puts the pointer
                        // where it should be by then.
                        for (int i = 1; i <= kSteps; ++i) {
                            const int x = it->x + (it->x2 - it->x) * i / kSteps,
                                      y = it->y + (it->y2 - it->y) * i / kSteps;
                            const int px = it->x + (it->x2 - it->x) * (i - 1) / kSteps,
                                      py = it->y + (it->y2 - it->y) * (i - 1) / kSteps;
                            moves.push_back({at + 2 + uint64_t(i), x - px, y - py});
                            points.push_back({at + 2 + uint64_t(i), x, y});
                        }
                        holds.push_back({at + 2, kSteps + 4});
                    } else if (it->kind == 'k') {
                        keys.push_back({at, it->x, 0});
                    } else {
                        points.push_back({at, it->x, it->y});
                        if (it->kind == 'c')
                            clicks.push_back({at + 2, -1, -1});
                        if (it->kind == 'c' && it->repeat > 1)
                            retries.push_back({at + 40, framesShown, it->repeat - 1, it->x, it->y});
                    }
                    stepActions.erase(it);
                    break;
                }
            }
            // -calls: a call is esp down by 4 with the address after the
            // last instruction on top and a jump; a return is reaching an
            // open call's return address.
            if (callsAddress && !treeDone) {
                const uint32_t sp = c.reg[Cpu::ESP];
                if (openCalls.empty()) {
                    if (c.eip == callsAddress && framesShown + 1 >= callsFrame) {
                        std::printf("calls of %s on frame %llu:\n", callsOf.c_str(),
                                    (unsigned long long)(framesShown + 1));
                        openCalls.push_back(sp);
                    }
                } else {
                    while (!openCalls.empty() && sp > openCalls.back())
                        openCalls.pop_back();
                    if (openCalls.empty()) {
                        treeLine("");
                        std::printf("returned 0x%x\n", c.reg[Cpu::EAX]);
                        treeDone = true;
                    } else if (sp == lastEsp - 4 && c.read32(sp) > lastEip && c.read32(sp) <= lastEip + 7 &&
                               c.eip != c.read32(sp)) {
                        treeLine(std::string(2 * openCalls.size(), ' ') + symbol(c.eip));
                        openCalls.push_back(sp);
                    }
                }
                lastEip = c.eip;
                lastEsp = sp;
            }
            for (Watch& w : watched) { // a call, or a return from one
                if (c.eip == w.address) {
                    const uint32_t sp = c.reg[Cpu::ESP];
                    w.pending.push_back({c.read32(sp), sp + 4});
                    if (w.args.size() < 256)
                        w.args.push_back({c.reg[Cpu::ECX], c.read32(sp + 4), c.read32(sp + 8), c.read32(sp + 12),
                                          c.read32(sp + 16), c.read32(sp)});
                }
                else if (!w.pending.empty() && c.eip == w.pending.back().first &&
                         c.reg[Cpu::ESP] >= w.pending.back().second) {
                    ++w.results[c.reg[Cpu::EAX]];
                    if (w.st0.size() < 8)
                        w.st0.push_back(c.fpu.regs[c.fpu.top & 7]);
                    w.pending.pop_back();
                }
            }
            recent[recentAt++ % recent.size()] = c.eip;
            if (const std::string* n = c.nativeName(c.eip)) {
                calls[callsAt++ % calls.size()] = *n;
                if (log) {
                    if (*n == lastLogged) {
                        ++repeats;
                    } else {
                        flushLog();
                        std::printf("call %s\n", n->c_str());
                        lastLogged = *n;
                        repeats = 1;
                    }
                }
            }
        };
    if (traceFn && !traceFrom)
        cpu.trace = traceFn;
    auto where = [&](uint32_t a) {
        for (const std::string& name : game.loadOrder()) {
            const auto* m = game.process().module(name);
            if (a >= m->base && a < m->base + m->size) {
                char b[96];
                std::snprintf(b, sizeof b, "%s+%x", name.c_str(), a - m->base);
                return std::string(b);
            }
        }
        char b[16];
        std::snprintf(b, sizeof b, "%08x", a);
        return std::string(b);
    };
    auto failed = [&](const reluva::Error& e) {
        std::printf("failed after %llu instructions: %s, at %s\nlast instructions:",
                    (unsigned long long)cpu.executed(), e.message.c_str(), where(cpu.faultEip).c_str());
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
            std::printf(" %s", where(recent[i % recent.size()]).c_str());
        std::printf("\nlast calls to stand-ins:");
        for (size_t i = callsAt > calls.size() ? callsAt - calls.size() : 0; i < callsAt; ++i)
            std::printf(" %s", calls[i % calls.size()].c_str());
        // The callers: words on the stack that are just after a call in
        // one of the modules (a guess, as frames aren't recorded).
        const uint32_t esp = cpu.reg[Cpu::ESP];
        std::printf("\nesp %08x ebp %08x ecx %08x, stack:", esp, cpu.reg[Cpu::EBP], cpu.reg[Cpu::ECX]);
        for (uint32_t i = 0; i < 16; ++i)
            std::printf(" %08x", cpu.read32(esp + 4 * i));
        std::printf("\nreturn addresses on the stack:");
        for (uint32_t at = esp, found = 0; at < esp + 4096 && found < 24; at += 4) {
            const uint32_t a = cpu.read32(at);
            if (a < 6 || where(a).find('+') == std::string::npos)
                continue;
            const bool afterCall = cpu.read8(a - 5) == 0xe8 || (cpu.read8(a - 6) == 0xff && cpu.read8(a - 5) == 0x15) ||
                                   (cpu.read8(a - 2) == 0xff && (cpu.read8(a - 1) & 0x38) == 0x10) ||
                                   (cpu.read8(a - 3) == 0xff && (cpu.read8(a - 2) & 0x38) == 0x10);
            if (!afterCall)
                continue;
            std::printf(" %s", symbol(a).c_str());
            ++found;
        }
        std::printf("\n");
        return 1;
    };

    if (reluva::Error e = game.attach())
        return failed(e);
    std::printf("every DLL's start-up done: %llu instructions\n", (unsigned long long)cpu.executed());
    uint32_t exitCode = 0;
    const uint64_t before = cpu.executed();
    reluva::Error ran = game.run(exitCode);
    if (profiler.joinable()) {
        profileDone = true;
        profiler.join();
        std::map<std::string, size_t> byFunction;
        for (uint32_t a : profileSamples) {
            std::string name = symbol(a);
            const size_t bang = name.find('!');
            if (const size_t plus = name.find('+', bang == std::string::npos ? 0 : bang); plus != std::string::npos && bang != std::string::npos)
                name.resize(plus);
            ++byFunction[name];
        }
        std::vector<std::pair<size_t, std::string>> top;
        for (const auto& [name, n] : byFunction)
            top.push_back({n, name});
        std::sort(top.rbegin(), top.rend());
        std::printf("profile: %zu samples\n", profileSamples.size());
        for (size_t i = 0; i < top.size() && i < 60; ++i)
            std::printf("  %5.1f%%  %s\n", 100.0 * double(top[i].first) / double(profileSamples.size()), top[i].second.c_str());
        if (profileAddresses) {
            // The busiest 64-byte stretches of code, for functions no export names.
            std::map<uint32_t, size_t> byBlock;
            for (uint32_t a : profileSamples)
                ++byBlock[a & ~63u];
            std::vector<std::pair<size_t, uint32_t>> blocks;
            for (const auto& [a, n] : byBlock)
                blocks.push_back({n, a});
            std::sort(blocks.rbegin(), blocks.rend());
            std::printf("profile by address:\n");
            for (size_t i = 0; i < blocks.size() && i < 60; ++i)
                std::printf("  %5.1f%%  %08x  %s\n", 100.0 * double(blocks[i].first) / double(profileSamples.size()),
                            blocks[i].second, symbol(blocks[i].second).c_str());
        }
    }
    if (framesShown) {
        std::printf("frames shown: %llu\n", (unsigned long long)framesShown);
        if (!shots.empty())
            saveFrame(game.directX().frame());
    }
    // The program's window, if it made one.
    if (const auto* w = game.process().window(game.process().mainWindow()))
        std::printf("window: \"%s\" (class %s), %dx%d at %d,%d, %s\n", w->title.c_str(), w->className.c_str(),
                    w->width, w->height, w->x, w->y, w->visible ? "shown" : "hidden");
    if (ran && !(quitAt && framesShown >= quitAt) && !advancedAt)
        return failed(ran);
    if (ran) {
        std::printf("stopped at frame %llu, after %llu instructions\n", (unsigned long long)framesShown,
                    (unsigned long long)cpu.executed());
        std::printf("the main thread asked to sleep %llu ms in all\n", (unsigned long long)game.process().slept());
        return 0;
    }
    std::printf("the program returned %u after %llu instructions\n", exitCode,
                (unsigned long long)(cpu.executed() - before));
    return 0;
}

const reluva::tool::Commands commands("netimmerse", {
    {"at-run", "<game folder> [options]",
     "run LEGO Alpha Team's own code in the x86 emulator, headless: with -fixed-clock and scripted input "
     "(-click, -point, -key) a run repeats exactly; -shots saves frames, -watch, -calls, -trace, -log, -files "
     "and -dump show what the game does. Run it without arguments for every option.",
     cmdAtRun},
    {"at-sweep", "<game folder> <out folder> [-jobs n] [-quit frame] [-shots] [-solved] [mission...]",
     "run every mission (or those named) past Go, several at once (an at-run each), and report which ran to "
     "the end; with -solved, each with its solved layout until the Adventure moves on",
     cmdAtSweep},
});

} // namespace
