// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco command for running the game's own program and
// seeing how far it gets (docs/engines/legoloco/systems/running.md).

#include "devtools/tool.h"
#include "engines/legoloco/host/game_process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// The frame as a binary PPM; nothing if none was shown.
void saveShot(const x86::DirectX::Frame &f, const std::string &path) {
    if (f.pixels.empty()) {
        std::printf("no frame was shown\n");
        return;
    }
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << f.width << ' ' << f.height << "\n255\n";
    for (uint32_t v : f.pixels) {
        const char rgb[3] = {char(v >> 16), char(v >> 8), char(v)};
        out.write(rgb, 3);
    }
    std::printf("frame %llu saved to %s\n", (unsigned long long)f.number, path.c_str());
}

// What the tool does to the program, at a time on its clock: the
// player's input, or saving the screen.
struct Step {
    uint32_t at = 0; // milliseconds
    enum class Kind { kMove, kDown, kUp, kKey, kShot } kind = Kind::kMove;
    int x = 0; // kMove: where; kDown, kUp: the button (0 left, 1 right)
    int y = 0;
    uint32_t key = 0; // kKey: a virtual-key code
    std::string file; // kShot
};

Step step(uint32_t at, Step::Kind kind, int x = 0, int y = 0) {
    Step s;
    s.at = at, s.kind = kind, s.x = x, s.y = y;
    return s;
}

// Options: -log prints each stand-in called; -limit n stops after n
// million instructions; -seconds n stops once the program's clock has
// passed n seconds (a program waiting for messages runs no instructions);
// -save folder is where it may write; -nomovies ends each movie as it
// starts. The program's clock (s, in seconds) times what's done to it:
// -click s x y and -rclick s x y click a button at (x, y), -move s x y
// moves the pointer, -drag s x1 y1 x2 y2 drags with the left button over
// half a second, -key s vk presses and releases a key (a virtual-key code,
// in hex), and -shot s file.ppm saves the screen (-shot file.ppm, at the
// end).
int cmdRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco run <game folder> -save <folder> [-log] [-limit millions] "
                             "[-seconds n] [-nomovies] [-click|-rclick|-move s x y] [-drag s x1 y1 x2 y2] "
                             "[-key s vk] [-shot [s] file.ppm]...\n");
        return 2;
    }
    bool log = false;
    bool noMovies = false;
    uint64_t limit = 2000;
    uint32_t seconds = 0;
    std::vector<Step> steps;
    std::string shot;
    std::string save;
    auto ms = [](const char *s) { return uint32_t(std::strtod(s, nullptr) * 1000); };
    auto isNumber = [](const char *s) {
        char *end = nullptr;
        std::strtod(s, &end);
        return end != s && *end == 0;
    };
    auto click = [&](uint32_t at, int button, int x, int y) {
        steps.push_back(step(at, Step::Kind::kMove, x, y));
        steps.push_back(step(at, Step::Kind::kDown, button));
        steps.push_back(step(at + 100, Step::Kind::kUp, button)); // held a tenth of a second
    };
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-log"))
            log = true;
        else if (!std::strcmp(argv[i], "-limit") && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "-seconds") && i + 1 < argc)
            seconds = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "-nomovies"))
            noMovies = true;
        else if ((!std::strcmp(argv[i], "-click") || !std::strcmp(argv[i], "-rclick")) && i + 3 < argc) {
            click(ms(argv[i + 1]), argv[i][1] == 'r' ? 1 : 0, std::atoi(argv[i + 2]), std::atoi(argv[i + 3]));
            i += 3;
        } else if (!std::strcmp(argv[i], "-move") && i + 3 < argc) {
            steps.push_back(step(ms(argv[i + 1]), Step::Kind::kMove, std::atoi(argv[i + 2]), std::atoi(argv[i + 3])));
            i += 3;
        } else if (!std::strcmp(argv[i], "-drag") && i + 5 < argc) {
            const uint32_t at = ms(argv[i + 1]);
            const int x1 = std::atoi(argv[i + 2]);
            const int y1 = std::atoi(argv[i + 3]);
            const int x2 = std::atoi(argv[i + 4]);
            const int y2 = std::atoi(argv[i + 5]);
            steps.push_back(step(at, Step::Kind::kMove, x1, y1));
            steps.push_back(step(at, Step::Kind::kDown, 0));
            for (int k = 1; k <= 10; ++k)
                steps.push_back(
                    step(at + uint32_t(k) * 50, Step::Kind::kMove, x1 + (x2 - x1) * k / 10, y1 + (y2 - y1) * k / 10));
            steps.push_back(step(at + 600, Step::Kind::kUp, 0));
            i += 5;
        } else if (!std::strcmp(argv[i], "-key") && i + 2 < argc) {
            Step key = step(ms(argv[i + 1]), Step::Kind::kKey);
            key.key = uint32_t(std::strtoul(argv[i + 2], nullptr, 16));
            steps.push_back(key);
            i += 2;
        } else if (!std::strcmp(argv[i], "-shot") && i + 2 < argc && isNumber(argv[i + 1])) {
            Step s = step(ms(argv[i + 1]), Step::Kind::kShot);
            s.file = argv[i + 2];
            steps.push_back(s);
            i += 2;
        } else if (!std::strcmp(argv[i], "-shot") && i + 1 < argc)
            shot = argv[++i];
        else if (!std::strcmp(argv[i], "-save") && i + 1 < argc)
            save = argv[++i];
    }
    if (save.empty()) {
        std::fprintf(stderr, "a save folder is needed: -save <folder>\n");
        return 2;
    }
    std::stable_sort(steps.begin(), steps.end(), [](const Step &a, const Step &b) { return a.at < b.at; });
    legoloco::GameProcess game(argv[2], save);
    game.video().endAtOnce = noMovies;
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
        // Each argument as a number, and as text where it points at some.
        std::string line = name + "(";
        for (int i = 0; i < 6; ++i) {
            char n[16];
            std::snprintf(n, sizeof n, "%s%x", i ? ", " : "", c.arg(i));
            line += n;
            const uint32_t v = c.arg(i);
            if (v >= 0x10000 && c.valid(v, 2)) {
                const std::string text = c.string(v, 60);
                if (text.size() >= 2 && std::all_of(text.begin(), text.end(),
                                                    [](char ch) { return uint8_t(ch) >= 32 && uint8_t(ch) < 127; }))
                    line += " \"" + text + "\"";
            }
        }
        char from[32];
        std::snprintf(from, sizeof from, ") from %x", c.read32(c.reg[x86::Cpu::ESP]));
        line += from;
        calls[callsAt++ % calls.size()] = line;
        if (!log)
            return;
        if (name == last) {
            ++repeats;
            return;
        }
        if (repeats > 1)
            std::printf("  (%d times)\n", repeats);
        std::printf("%s\n", line.c_str());
        last = name;
        repeats = 1;
    };
    // The clock is watched from a thread of the tool's own: the program's
    // may all be waiting.
    std::atomic<bool> done{false};
    std::atomic<bool> timedOut{false};
    std::thread watch;
    if (seconds)
        watch = std::thread([&] {
            while (!done) {
                if (game.process().milliseconds() >= seconds * 1000) {
                    timedOut = true;
                    game.process().requestExit();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    // The steps are taken as the player's input would be: when the program
    // looks for a message and finds none.
    size_t next = 0;
    game.process().idle = [&] {
        x86::Win32Process &p = game.process();
        for (const uint32_t now = p.milliseconds(); next < steps.size() && steps[next].at <= now; ++next) {
            const Step &st = steps[next];
            switch (st.kind) {
            case Step::Kind::kMove:
                p.mouseMoveTo(st.x, st.y);
                break;
            case Step::Kind::kDown:
            case Step::Kind::kUp:
                p.mouseButton(st.x, st.kind == Step::Kind::kDown);
                break;
            case Step::Kind::kKey:
                p.key(st.key, 0, true);
                p.key(st.key, 0, false);
                break;
            case Step::Kind::kShot:
                saveShot(game.directX().frame(), st.file);
                break;
            }
        }
    };
    uint32_t exitCode = 0;
    const Error err = game.run(exitCode);
    done = true;
    if (watch.joinable())
        watch.join();
    if (!shot.empty())
        saveShot(game.directX().frame(), shot);
    if (timedOut) {
        std::printf("%llu instructions; stopped after %u seconds\nlast stand-ins called:\n",
                    (unsigned long long)cpu.executed(), seconds);
        for (size_t i = callsAt > calls.size() ? callsAt - calls.size() : 0; i < callsAt; ++i)
            std::printf("  %s\n", calls[i % calls.size()].c_str());
        return 0;
    }
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
                                         {"run",
                                          "<game folder> -save <folder> [-log] [-limit millions] [-seconds n] "
                                          "[-nomovies] [-click|-rclick|-move s x y] [-drag s x1 y1 x2 y2] [-key s vk] "
                                          "[-shot [s] file.ppm]...",
                                          "run the game's own loco.exe and report how far it gets", cmdRun},
                                     });

} // namespace
