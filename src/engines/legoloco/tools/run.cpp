// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco command for running the game's own program and
// seeing how far it gets (docs/engines/legoloco/systems/running.md).

#include "backends/networking/sockets/socket_net.h"
#include "devtools/tool.h"
#include "engines/legoloco/host/game_process.h"

#include <algorithm>
#include <cstdarg>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// The frame as a binary PPM; nothing if none was shown.
// The screen as shown, with Windows' pointer over it when it's shown.
void saveShot(const x86::DirectX::Frame &f, x86::Win32Process &process, const std::string &path) {
    if (f.pixels.empty()) {
        std::printf("no frame was shown\n");
        return;
    }
    std::vector<uint32_t> pixels = f.pixels;
    if (const Cursor *c = process.pointer()) {
        const int left = process.cursorX - f.x - c->hotX;
        const int top = process.cursorY - f.y - c->hotY;
        for (int y = 0; y < c->h; ++y)
            for (int x = 0; x < c->w; ++x) {
                const int sx = left + x;
                const int sy = top + y;
                const uint8_t *p = c->pixel(x, y);
                if (sx < 0 || sy < 0 || sx >= f.width || sy >= f.height || !p[3])
                    continue;
                uint32_t &v = pixels[size_t(sy) * size_t(f.width) + size_t(sx)];
                const auto mix = [&](int shift, uint8_t to) {
                    const uint32_t from = (v >> shift) & 0xff;
                    return ((from * (255u - p[3]) + uint32_t(to) * p[3]) / 255u) << shift;
                };
                v = 0xff000000u | mix(16, p[0]) | mix(8, p[1]) | mix(0, p[2]);
            }
    }
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << f.width << ' ' << f.height << "\n255\n";
    for (uint32_t v : pixels) {
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

// One copy of the program as the tool runs it: its save folder, what's
// done to it, and its report. Its lines are led by `name` when there are
// two.
struct Copy {
    std::string name;
    std::string save;
    std::vector<Step> steps;
    std::string shot;
    bool log = false;
    int desktopWidth = 0; // 0: the process's own
    int desktopHeight = 0;

    // Reads one option at argv[i] that's a copy's own, moving i past its
    // arguments; false if it isn't one.
    bool option(int argc, char **argv, int &i) {
        auto ms = [](const char *s) { return uint32_t(std::strtod(s, nullptr) * 1000); };
        auto isNumber = [](const char *s) {
            char *end = nullptr;
            std::strtod(s, &end);
            return end != s && *end == 0;
        };
        const char *a = argv[i];
        if (!std::strcmp(a, "-log")) {
            log = true;
        } else if (!std::strcmp(a, "-desktop") && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &desktopWidth, &desktopHeight);
        } else if ((!std::strcmp(a, "-click") || !std::strcmp(a, "-rclick")) && i + 3 < argc) {
            const uint32_t at = ms(argv[i + 1]);
            const int button = a[1] == 'r' ? 1 : 0;
            steps.push_back(step(at, Step::Kind::kMove, std::atoi(argv[i + 2]), std::atoi(argv[i + 3])));
            steps.push_back(step(at, Step::Kind::kDown, button));
            steps.push_back(step(at + 100, Step::Kind::kUp, button)); // held a tenth of a second
            i += 3;
        } else if (!std::strcmp(a, "-move") && i + 3 < argc) {
            steps.push_back(step(ms(argv[i + 1]), Step::Kind::kMove, std::atoi(argv[i + 2]), std::atoi(argv[i + 3])));
            i += 3;
        } else if (!std::strcmp(a, "-drag") && i + 5 < argc) {
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
        } else if (!std::strcmp(a, "-key") && i + 2 < argc) {
            Step key = step(ms(argv[i + 1]), Step::Kind::kKey);
            key.key = uint32_t(std::strtoul(argv[i + 2], nullptr, 16));
            steps.push_back(key);
            i += 2;
        } else if (!std::strcmp(a, "-shot") && i + 2 < argc && isNumber(argv[i + 1])) {
            Step s = step(ms(argv[i + 1]), Step::Kind::kShot);
            s.file = argv[i + 2];
            steps.push_back(s);
            i += 2;
        } else if (!std::strcmp(a, "-shot") && i + 1 < argc) {
            shot = argv[++i];
        } else if (!std::strcmp(a, "-save") && i + 1 < argc) {
            save = argv[++i];
        } else {
            return false;
        }
        return true;
    }

    // Prints a line of its report, led by its name when it has one.
    void say(const char *format, ...) const {
        std::lock_guard<std::mutex> lock(outputMutex());
        if (!name.empty())
            std::printf("[%s] ", name.c_str());
        va_list args;
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
    }
    static std::mutex &outputMutex() {
        static std::mutex m;
        return m;
    }

    // Runs the program until it ends, fails, passes `limit` million
    // instructions or `seconds` of its clock; the report printed. 0 if it
    // ran (or ended) as asked.
    int run(const std::string &gameDir, uint64_t limit, uint32_t seconds, bool noMovies,
            const std::shared_ptr<x86::DirectPlay::Network> &network) {
        std::stable_sort(steps.begin(), steps.end(), [](const Step &a, const Step &b) { return a.at < b.at; });
        legoloco::GameProcess game(gameDir, save);
        game.video().endAtOnce = noMovies;
        game.directPlay().network = network;
        if (desktopWidth > 0 && desktopHeight > 0)
            game.process().screenWidth = desktopWidth, game.process().screenHeight = desktopHeight;
        if (Error err = game.load()) {
            say("%s\n", err.message.c_str());
            return 1;
        }
        std::string missing;
        for (const std::string &m : game.process().missingImports())
            missing += " " + m;
        say("%zu imports stood in for by nothing:%s\n", game.process().missingImports().size(), missing.c_str());

        x86::Cpu &cpu = game.cpu();
        cpu.setInstructionLimit(limit * 1000000);
        std::mutex mutex;
        std::vector<std::string> calls(48);
        size_t callsAt = 0;
        std::string last;
        int repeats = 0;
        // The stand-ins' reports end their own lines, or not.
        game.process().output = [this](const std::string &s) {
            say("[program] %s%s", s.c_str(), !s.empty() && s.back() == '\n' ? "" : "\n");
        };
        cpu.nativeTrace = [&](x86::Cpu &c, const std::string &fn) {
            std::lock_guard<std::mutex> lock(mutex);
            // Each argument as a number, and as text where it points at some.
            std::string line = fn + "(";
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
            if (fn == last) {
                ++repeats;
                return;
            }
            if (repeats > 1)
                say("  (%d times)\n", repeats);
            say("%s\n", line.c_str());
            last = fn;
            repeats = 1;
        };
        // The clock is watched from a thread of the tool's own: the
        // program's may all be waiting.
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
        // The steps are taken as the player's input would be: when the
        // program looks for a message and finds none.
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
                    saveShot(game.directX().frame(), game.process(), st.file);
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
            saveShot(game.directX().frame(), game.process(), shot);
        std::string lastCalls;
        for (size_t i = callsAt > calls.size() ? callsAt - calls.size() : 0; i < callsAt; ++i)
            lastCalls += "  " + calls[i % calls.size()] + "\n";
        if (timedOut) {
            say("%llu instructions; stopped after %u seconds\nlast stand-ins called:\n%s",
                (unsigned long long)cpu.executed(), seconds, lastCalls.c_str());
            return 0;
        }
        if (!err) {
            say("%llu instructions; the program ended, code %u\n", (unsigned long long)cpu.executed(), exitCode);
            return 0;
        }
        std::string stack;
        const uint32_t esp = cpu.reg[x86::Cpu::ESP];
        for (uint32_t at = esp, found = 0; at < esp + 4096 && found < 16; at += 4) {
            const uint32_t a = cpu.read32(at);
            if (a >= 0x401000 && a < 0x477000 && cpu.read8(a - 5) == 0xe8) {
                char n[16];
                std::snprintf(n, sizeof n, " %x", a);
                stack += n;
                ++found;
            }
        }
        say("%llu instructions; stopped: %s\nlast stand-ins called:\n%seip %08x esp %08x; return addresses on the "
            "stack:%s\n",
            (unsigned long long)cpu.executed(), err.message.c_str(), lastCalls.c_str(), cpu.faultEip, esp,
            stack.c_str());
        return 1;
    }
};

// Options both commands take: -limit n stops after n million
// instructions; -seconds n stops once the program's clock has passed n
// seconds (a program waiting for messages runs no instructions);
// -nomovies ends each movie as it starts.
struct Common {
    uint64_t limit = 2000;
    uint32_t seconds = 0;
    bool noMovies = false;
    bool udp = false;
    std::string meeting;
    bool option(int argc, char **argv, int &i) {
        if (!std::strcmp(argv[i], "-udp"))
            udp = true;
        else if (!std::strcmp(argv[i], "-meeting") && i + 1 < argc)
            meeting = argv[++i], udp = true;
        else if (!std::strcmp(argv[i], "-limit") && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "-seconds") && i + 1 < argc)
            seconds = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "-nomovies"))
            noMovies = true;
        else
            return false;
        return true;
    }
};

// DirectPlay over the system's UDP sockets: a search with no address
// goes to each network's broadcast address, and the machine's own; and to
// the meeting server at `meeting` ("host[:port]"), if it's given.
std::shared_ptr<x86::DirectPlay::Network> systemNetwork(const std::string &meeting) {
    auto network = std::make_shared<x86::DirectPlay::Network>();
    network->open = [](uint16_t port) {
        std::string error;
        auto socket = net::openSystemUdp(port, error);
        if (!socket)
            std::printf("no UDP socket on port %u: %s\n", unsigned(port), error.c_str());
        return socket;
    };
    network->broadcast = net::systemBroadcastAddresses();
    network->broadcast.push_back(0xffffffffu);
    network->broadcast.push_back(0x7f000001u);
    if (!meeting.empty()) {
        const auto [host, port] = net::splitHostPort(meeting, net::kMeetingPort);
        network->meeting.port = port;
        if (!net::resolveSystemHost(host, network->meeting.ip))
            std::printf("can't find the meeting server %s\n", meeting.c_str());
    }
    return network;
}

// A copy's options: -log prints each stand-in called; -save folder is
// where it may write. The program's clock (s, in seconds) times what's
// done to it: -click s x y and -rclick s x y click a button at (x, y),
// -move s x y moves the pointer, -drag s x1 y1 x2 y2 drags with the left
// button over half a second, -key s vk presses and releases a key (a
// virtual-key code, in hex), and -shot s file.ppm saves the screen
// (-shot file.ppm, at the end).
int cmdRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(
            stderr,
            "usage: reluva-tool legoloco run <game folder> -save <folder> [-log] [-limit millions] "
            "[-seconds n] [-nomovies] [-udp] [-meeting host] [-click|-rclick|-move s x y] [-drag s x1 y1 x2 y2] "
            "[-key s vk] [-shot [s] file.ppm]...\n");
        return 2;
    }
    Common common;
    Copy copy;
    for (int i = 3; i < argc; ++i)
        if (!common.option(argc, argv, i))
            copy.option(argc, argv, i);
    if (copy.save.empty()) {
        std::fprintf(stderr, "a save folder is needed: -save <folder>\n");
        return 2;
    }
    return copy.run(argv[2], common.limit, common.seconds, common.noMovies,
                    common.udp ? systemNetwork(common.meeting) : nullptr);
}

// Two copies at once, each with a save folder of its own and its own
// steps (those after -- are the second's), on one DirectPlay network: in
// memory, for network play without a network, or with -udp the system's.
int cmdPair(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(
            stderr,
            "usage: reluva-tool legoloco pair <game folder> [-limit millions] [-seconds n] "
            "[-nomovies] [-udp] [-meeting host] -save <folder> [first's steps]... -- -save <folder> [second's steps]...\n");
        return 2;
    }
    Common common;
    Copy copies[2];
    copies[0].name = "A";
    copies[1].name = "B";
    int which = 0;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--"))
            which = 1;
        else if (!common.option(argc, argv, i))
            copies[which].option(argc, argv, i);
    }
    if (copies[0].save.empty() || copies[1].save.empty() || copies[0].save == copies[1].save) {
        std::fprintf(stderr, "each copy needs a save folder of its own: -save <folder> ... -- -save <folder>\n");
        return 2;
    }
    auto network = common.udp ? systemNetwork(common.meeting) : x86::DirectPlay::Network::inMemory();
    int results[2] = {0, 0};
    std::thread second(
        [&] { results[1] = copies[1].run(argv[2], common.limit, common.seconds, common.noMovies, network); });
    results[0] = copies[0].run(argv[2], common.limit, common.seconds, common.noMovies, network);
    second.join();
    return results[0] || results[1] ? 1 : 0;
}

const Commands kCommands(
    "legoloco",
    {
        {"run",
         "<game folder> -save <folder> [-log] [-limit millions] [-seconds n] [-nomovies] [-udp] [-meeting host] "
         "[-click|-rclick|-move s x y] [-drag s x1 y1 x2 y2] [-key s vk] [-shot [s] file.ppm]...",
         "run the game's own loco.exe and report how far it gets", cmdRun},
        {"pair",
         "<game folder> [-limit millions] [-seconds n] [-nomovies] [-udp] [-meeting host] -save <folder> "
         "[steps]... -- -save <folder> [steps]...",
         "run two copies at once on one DirectPlay network (in memory, or UDP), each with its "
         "own steps",
         cmdPair},
    });

} // namespace
