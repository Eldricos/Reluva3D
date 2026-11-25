// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith command for running the retail JK.EXE in the x86
// emulator: how far it gets, and what it asks for that nothing stands in
// for (docs/engines/sith/games/jk/systems/hosting.md).

#include "common/events.h"
#include "audio/mixer.h"
#include "audio/wav.h"
#include "backends/platform/sdl3/sdl_system.h"
#include "backends/networking/sockets/socket_net.h"
#include "common/fs.h"
#include "common/stream.h"
#include "devtools/tool.h"
#include "common/pe.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/disasm.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"
#include "engines/sith/folders.h"
#include "engines/sith/host/game_process.h"
#include "engines/sith/render/scene_gpu.h"
#include "image/image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <functional>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::Error;
namespace x86 = reluva::x86;
namespace key = reluva::key;
namespace sith = reluva::sith;

// A key pressed, or the mouse clicked, at a moment of the run (-keys).
struct KeyPress {
    double at = 0; // seconds from the start
    uint32_t virtualKey = 0;
    uint32_t scanCode = 0;
    bool click = false; // the left button at (x, y) on the picture shown, not a key
    int x = 0;
    int y = 0;
    bool down = false;
    bool up = false;
};

// "<seconds>:<key>,..." as key presses; false if one can't be read.
bool parseKeys(const std::string &text, std::vector<KeyPress> &out) {
    static const std::map<std::string, uint32_t> kNames = {
        {"esc", key::kEscape}, {"enter", key::kReturn}, {"space", key::kSpace}, {"tab", key::kTab},
        {"up", key::kUp},      {"down", key::kDown},    {"left", key::kLeft},   {"right", key::kRight}};
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(',', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = text.substr(at, end - at);
        at = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos)
            return false;
        const std::string name = item.substr(colon + 1);
        uint32_t code = 0;
        if (auto it = kNames.find(name); it != kNames.end())
            code = it->second;
        else if (name.size() == 1 && std::isgraph(uint8_t(name[0])))
            code = uint32_t(std::tolower(uint8_t(name[0])));
        KeyPress k;
        k.at = std::atof(item.substr(0, colon).c_str());
        if (name.rfind("click", 0) == 0) { // click<x>x<y>
            k.click = true;
            if (std::sscanf(name.c_str() + 5, "%dx%d", &k.x, &k.y) != 2)
                return false;
        } else if (!code || !x86::windowsKey(code, k.virtualKey, k.scanCode)) {
            return false;
        }
        out.push_back(k);
    }
    return true;
}

// JK.EXE under the emulator for up to `seconds`, from `install` with CD 1
// in drive D, saying what it opens, writes and shows.
int cmdHost(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: reluva-tool sith host <install> [-seconds N] [-args \"...\"] [-png file] "
                     "[-files] [-calls] [-d3d] [-net] [-join address] [-keys \"s:key,...\"] [-skim] [-cheat word] "
                     "[-pad \"s:control=value,...\"] [-close seconds] [-frames N folder] [-peek address] [-wav file] "
                     "[-write folder] [-gpu folder] [-gpuplay folder] [-gpuscale N] [-gpuaa N]\n");
        return 2;
    }
    const std::filesystem::path install = argv[2];
    double seconds = 20;
    std::string args;
    std::string png;
    std::string wav;
    bool listFiles = false;
    bool listCalls = false;
    bool direct3d = false;
    bool network = false;
    std::string join;
    std::vector<uint32_t> peeks;
    std::string keysArg;
    bool skim = false;
    double closeAt = -1;
    std::string padArg;
    std::vector<std::string> cheats; // typed once in each level, with -skim
    double framesEvery = 0;
    std::filesystem::path framesFolder;
    std::filesystem::path writable = std::filesystem::temp_directory_path() / "reluva-sith-host";
    std::filesystem::path gpuFolder;
    bool gpuPlay = false;
    int gpuScale = 1;   // the GPU's picture this many times the game's
    int gpuSamples = 1; // multisampling, as the device has it
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-seconds" && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (a == "-args" && i + 1 < argc)
            args = argv[++i];
        else if (a == "-wav" && i + 1 < argc)
            wav = argv[++i];
        else if (a == "-png" && i + 1 < argc)
            png = argv[++i];
        else if (a == "-files")
            listFiles = true;
        else if (a == "-calls")
            listCalls = true;
        else if (a == "-d3d")
            direct3d = true;
        else if (a == "-net")
            network = true;
        else if (a == "-join" && i + 1 < argc)
            join = argv[++i], network = true;
        else if (a == "-skim")
            skim = true;
        else if (a == "-close" && i + 1 < argc)
            closeAt = std::atof(argv[++i]);
        else if (a == "-pad" && i + 1 < argc)
            padArg = argv[++i];
        else if (a == "-cheat" && i + 1 < argc)
            cheats.push_back(argv[++i]);
        else if (a == "-frames" && i + 2 < argc)
            framesEvery = std::atof(argv[++i]), framesFolder = argv[++i];
        else if (a == "-keys" && i + 1 < argc)
            keysArg = argv[++i];
        else if (a == "-peek" && i + 1 < argc)
            peeks.push_back(uint32_t(std::strtoul(argv[++i], nullptr, 16)));
        else if (a == "-write" && i + 1 < argc)
            writable = argv[++i];
        else if (a == "-gpu" && i + 1 < argc)
            gpuFolder = argv[++i];
        else if (a == "-gpuplay" && i + 1 < argc)
            gpuFolder = argv[++i], gpuPlay = true;
        else if (a == "-gpuscale" && i + 1 < argc)
            gpuScale = std::clamp(std::atoi(argv[++i]), 1, 4);
        else if (a == "-gpuaa" && i + 1 < argc)
            gpuSamples = std::clamp(std::atoi(argv[++i]), 1, 4);
    }
    if (!reluva::fs::findFile(install, "JK.EXE")) {
        std::fprintf(stderr, "no JK.EXE in %s\n", install.string().c_str());
        return 1;
    }
    sith::GameProcess game(sith::findFolders(install, {}), writable, args);
    x86::Cpu &cpu = game.cpu();
    x86::Win32Process &process = game.process();
    x86::DirectX &directX = game.directX();
    std::vector<std::string> opened;
    process.output = [](const std::string &text) { std::printf("debug output: %s", text.c_str()); };
    process.messageBox = [](const std::string &caption, const std::string &text) {
        std::printf("message box: %s: %s\n", caption.c_str(), text.c_str());
    };
    process.registryWritten = [kept = process.registryWritten](const std::string &key, const std::string &name) {
        std::printf("registry written: %s\\%s\n", key.c_str(), name.c_str());
        if (kept)
            kept(key, name);
    };
    if (direct3d) {
        // Setup's 3D acceleration on, on the primary display with Direct3D's
        // HAL device, as the game keeps them.
        const std::string key = "HKEY_LOCAL_MACHINE\\Software\\LucasArts Entertainment Company\\JediKnight\\v1.0";
        process.setRegistryValue(key, "b3DAccel", 3, {1, 0, 0, 0});
        process.setRegistryValue(key, "displayDeviceGUID", 3, std::vector<uint8_t>(16, 0)); // the primary display
        process.setRegistryValue(key, "displayMode", 3, {3, 0, 0, 0}); // 640 by 480 in 16-bit colour
        process.setRegistryValue(
            key, "3DDeviceGUID", 3,
            {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11, 0x81, 0x6f, 0, 0, 0xc0, 0x20, 0x15, 0x6e});
    }
    if (network) {
        // Multiplayer over this machine's network: sessions looked for on
        // the local networks, on this machine and at -join's address.
        x86::DirectPlay::Network &n = game.directPlay().network;
        game.directPlay().log = [](const std::string &line) { std::printf("network: %s\n", line.c_str()); };
        n.open = [](uint16_t port) {
            std::string error;
            auto socket = reluva::net::openSystemUdp(port, error);
            if (!socket)
                std::printf("network: %s\n", error.c_str());
            return socket;
        };
        n.search.push_back(reluva::net::Address::broadcast(x86::kDirectPlayPort));
        for (uint32_t ip : reluva::net::systemBroadcastAddresses())
            n.search.push_back({ip, x86::kDirectPlayPort});
        n.search.push_back({0x7f000001, x86::kDirectPlayPort});
        if (!join.empty()) {
            const auto [host, port] = reluva::net::splitHostPort(join, x86::kDirectPlayPort);
            uint32_t ip = 0;
            if (reluva::net::resolveSystemHost(host, ip))
                n.search.push_back({ip, port});
            else
                std::printf("network: can't find %s\n", host.c_str());
        }
    }
    if (Error err = game.load()) {
        std::printf("%s\n", err.message.c_str());
        return 1;
    }
    for (const std::string &m : process.missingImports())
        std::printf("not stood in for: %s\n", m.c_str());

    std::vector<KeyPress> presses;
    if (!parseKeys(keysArg, presses)) {
        std::fprintf(stderr, "-keys: \"<seconds>:<key>,...\", a key being a letter, a digit, esc, enter, space, "
                             "tab, up, down, left, right, or click<x>x<y> for the mouse's left button\n");
        return 2;
    }
    // With -pad, a game controller connected from the start, moved at the
    // times given: lx ly rx ry (-1 to 1), lt rt (0 to 1), and the buttons a
    // b x y lb rb back start ls rs up down left right (1 held, 0 not); "2."
    // before a control is a second controller's.
    struct PadChange {
        double at;
        std::string control;
        float value;
        bool done = false;
        int pad = 0;
    };
    std::vector<PadChange> padChanges;
    for (size_t at = 0; at < padArg.size();) {
        size_t end = padArg.find(',', at);
        if (end == std::string::npos)
            end = padArg.size();
        const std::string item = padArg.substr(at, end - at);
        at = end + 1;
        const size_t colon = item.find(':');
        const size_t equals = item.find('=');
        if (colon == std::string::npos || equals == std::string::npos || equals < colon) {
            std::fprintf(stderr, "-pad: \"<seconds>:<control>=<value>,...\"\n");
            return 2;
        }
        PadChange change{std::atof(item.substr(0, colon).c_str()), item.substr(colon + 1, equals - colon - 1),
                         float(std::atof(item.substr(equals + 1).c_str()))};
        if (change.control.rfind("2.", 0) == 0) {
            change.control = change.control.substr(2);
            change.pad = 1;
        }
        padChanges.push_back(change);
    }
    std::array<reluva::GamepadState, 2> pads;
    for (size_t i = 0; i < pads.size(); ++i) {
        pads[i].name = "Test pad " + std::to_string(i + 1);
        const bool used =
            std::any_of(padChanges.begin(), padChanges.end(), [&](const PadChange &c) { return c.pad == int(i); });
        const x86::Joysticks::Device device = x86::Joysticks::fromGamepad(pads[i]);
        if (used)
            game.joysticks().connect(int(i), &device);
    }
    auto movePad = [&](double now) {
        static const std::map<std::string, int> kAxes = {{"lx", 0}, {"ly", 1}, {"rx", 2},
                                                         {"ry", 3}, {"lt", 4}, {"rt", 5}};
        static const std::map<std::string, reluva::GamepadButton> kButtons = {
            {"a", reluva::kPadSouth},       {"b", reluva::kPadEast},          {"x", reluva::kPadWest},
            {"y", reluva::kPadNorth},       {"lb", reluva::kPadLeftShoulder}, {"rb", reluva::kPadRightShoulder},
            {"back", reluva::kPadBack},     {"start", reluva::kPadStart},     {"ls", reluva::kPadLeftStick},
            {"rs", reluva::kPadRightStick}, {"up", reluva::kPadUp},           {"down", reluva::kPadDown},
            {"left", reluva::kPadLeft},     {"right", reluva::kPadRight}};
        std::array<bool, 2> moved{};
        for (PadChange &p : padChanges) {
            if (p.done || now < p.at)
                continue;
            p.done = moved[size_t(p.pad)] = true;
            reluva::GamepadState &pad = pads[size_t(p.pad)];
            if (auto a = kAxes.find(p.control); a != kAxes.end())
                pad.axes[a->second] = p.value;
            else if (auto b = kButtons.find(p.control); b != kButtons.end())
                pad.buttons = p.value != 0 ? pad.buttons | 1u << b->second : pad.buttons & ~(1u << b->second);
        }
        for (size_t i = 0; i < pads.size(); ++i)
            if (moved[i])
                game.joysticks().set(int(i), x86::Joysticks::stateOf(pads[i]));
    };
    // Stopped after `seconds` of this machine's time; the keys pressed at
    // their times, each held a tenth of a second.
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    std::mutex tickMutex;
    // Each level and cut scene said as the game first looks for it (a loose
    // file before the archives'); with -skim, a cut scene skipped after a
    // few seconds, and a level after it has loaded and played a little, by
    // the cheat that ends it (thereisnotry, in the program XOR 0xc5 at
    // 0x53c760).
    std::string lastNamed;
    // In a level: invulnerable once it has loaded (jediwannabe), then the
    // cheat that ends it tried every half minute until another level or cut
    // scene comes, in case the level had the controls when it was typed.
    // After a level, the screens' Ok clicked every few seconds (the tally,
    // the Force powers).
    bool inLevel = false;
    bool invulnerable = false;
    double nextCheat = 0;
    bool betweenLevels = false;
    double nextOk = 0;
    auto typed = [](double at, const std::string &word) {
        std::string keys = std::to_string(at) + ":t";
        for (size_t i = 0; i < word.size(); ++i)
            keys += "," + std::to_string(at + 0.4 + 0.25 * double(i)) + ":" + word[i];
        return keys + "," + std::to_string(at + 0.6 + 0.25 * double(word.size())) + ":enter";
    };
    process.fileOpened = [&](const std::string &path, bool found) {
        std::lock_guard<std::mutex> lock(tickMutex);
        opened.push_back(path + (found ? "" : "  (not found)"));
        const std::string lower = reluva::fs::toLower(path);
        const bool level = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".jkl") == 0;
        const bool movie = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".smk") == 0;
        const std::string name = lower.substr(lower.find_last_of("\\/") + 1);
        if ((!level && !movie) || name == lastNamed || name == "static.jkl")
            return;
        lastNamed = name;
        const double now = elapsed();
        std::printf("%.1f s: %s %s\n", now, level ? "level" : "cut scene", name.c_str());
        std::fflush(stdout);
        if (!skim)
            return;
        inLevel = level;
        if (movie) {
            parseKeys(std::to_string(now + 4) + ":esc", presses);
            return;
        }
        invulnerable = false;
        nextCheat = now + 12;
        betweenLevels = true;
        nextOk = now + 25;
    };
    // With -wav, what's heard, taken as the clock moves.
    constexpr int kWavRate = 22050;
    // The sound card's output and the CD's music, mixed as the engine mixes
    // them.
    reluva::audio::Mixer mixer(kWavRate);
    if (!wav.empty()) {
        mixer.playOutput(game.directSound().output(), reluva::audio::SoundType::kEffects);
        game.cdAudio().mixer = &mixer;
    }
    reluva::audio::Sound recorded;
    recorded.rate = kWavRate;
    recorded.channels = 2;
    // Run on every thread's slices, so taken in turn.
    auto tick = [&](x86::Cpu &c) {
        std::lock_guard<std::mutex> lock(tickMutex);
        const double now = elapsed();
        movePad(now);
        // With -close, the game's window closed as the player's closing
        // Reluva's does (sith::Player): the game ends as on Windows.
        if (closeAt >= 0 && now >= closeAt) {
            process.postMessage(process.mainWindow(), 0x10, 0, 0); // WM_CLOSE
            closeAt = -1;
        }
        if (skim && inLevel && now >= nextCheat) {
            double at = now;
            if (!invulnerable) {
                parseKeys(typed(at, "jediwannabe"), presses);
                for (const std::string &cheat : cheats)
                    parseKeys(typed(at += 4.5, cheat), presses);
            }
            parseKeys(typed(at + 4.5, "thereisnotry"), presses);
            invulnerable = true;
            nextCheat = now + 30;
        }
        if (skim && betweenLevels && now >= nextOk) {
            parseKeys(std::to_string(now) + ":click563x438", presses);
            nextOk = now + 6;
        }
        if (!wav.empty()) {
            const size_t due = size_t(now * kWavRate) * 2;
            if (due > recorded.samples.size()) {
                const size_t at = recorded.samples.size();
                recorded.samples.resize(due);
                mixer.mix(recorded.samples.data() + at, int((due - at) / 2));
            }
        }
        for (KeyPress &k : presses) {
            if (!k.down && now >= k.at) {
                if (k.click) { // on the picture shown, wherever that is on the screen
                    process.mouseMoveTo(directX.frame().x + k.x, directX.frame().y + k.y);
                    process.mouseButton(0, true);
                } else {
                    process.key(k.virtualKey, k.scanCode, true);
                }
                k.down = true;
            } else if (k.down && !k.up && now >= k.at + 0.1) {
                if (k.click)
                    process.mouseButton(0, false);
                else
                    process.key(k.virtualKey, k.scanCode, false);
                k.up = true;
            }
        }
        if (now > seconds) {
            c.requestStop();
            process.requestExit(); // for a thread waiting on a message
        }
    };
    process.onSlice = tick;
    // When every thread waits (for a message, say), the clock and the keys
    // still move.
    process.idleTick = [&] {
        tick(cpu);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    // Every stand-in called, and the last few in order.
    std::map<std::string, uint64_t> called;
    std::deque<std::string> lastCalled;
    if (listCalls)
        cpu.nativeTrace = [&](x86::Cpu &, const std::string &name) {
            ++called[name];
            lastCalled.push_back(name);
            if (lastCalled.size() > 40)
                lastCalled.pop_front();
        };
    auto savePng = [](const std::filesystem::path &path, const x86::DirectX::Frame &f) {
        reluva::image::Image img;
        img.w = f.width;
        img.h = f.height;
        img.rgba.resize(size_t(img.w) * img.h * 4);
        for (size_t i = 0; i < f.pixels.size() && i * 4 < img.rgba.size(); ++i) {
            const uint32_t p = f.pixels[i];
            img.rgba[i * 4] = uint8_t(p >> 16);
            img.rgba[i * 4 + 1] = uint8_t(p >> 8);
            img.rgba[i * 4 + 2] = uint8_t(p);
            img.rgba[i * 4 + 3] = 255;
        }
        reluva::writeFile(path, reluva::image::encodePng(img));
    };
    // With -gpu, the scenes drawn in software and on the GPU, compared.
    std::unique_ptr<reluva::SdlSystem> window;
    sith::SceneGpu sceneGpu;
    reluva::Surface clear;
    struct Compared {
        uint64_t frames = 0;
        uint64_t pixels = 0;
        uint64_t differing = 0; // a channel more than a step of the game's 16 bits apart
        double worst = -1;      // the share of a frame's pixels differing
        uint64_t worstFrame = 0;
    } compared;
    if (!gpuFolder.empty()) {
        window = std::make_unique<reluva::SdlSystem>();
        reluva::gfx::Renderer3D *renderer = nullptr;
        if (Error e = window->init(); !e)
            e = window->init3D(640 * gpuScale, 480 * gpuScale, renderer);
        if (!renderer) {
            std::fprintf(stderr, "-gpu: no GPU renderer\n");
            return 1;
        }
        std::printf("GPU: %s, %d samples a pixel\n", renderer->deviceName().c_str(),
                    renderer->setMultisampling(gpuSamples));
        sceneGpu.setRenderer(renderer);
        clear.create(640, 480, reluva::PixelFormat::kRGBA32);
        std::filesystem::create_directories(gpuFolder);
        directX.sceneCapture = gpuPlay ? x86::DirectX::SceneCapture::kInstead : x86::DirectX::SceneCapture::kAlso;
    }
    auto writeRgba = [](const std::filesystem::path &path, int w, int h, std::vector<uint8_t> rgba) {
        reluva::image::Image img;
        img.w = w;
        img.h = h;
        img.rgba = std::move(rgba);
        reluva::writeFile(path, reluva::image::encodePng(img));
    };
    auto compare = [&](const x86::DirectX::Scene &scene) {
        if (scene.width != 640 || scene.height != 480 || scene.software.size() != size_t(640 * 480))
            return;
        sceneGpu.captureNext();
        sceneGpu.draw(&scene, clear);
        std::vector<uint8_t> gpu;
        if (!sceneGpu.capture(gpu) || gpu.size() != scene.software.size() * 4 * size_t(gpuScale * gpuScale))
            return;
        // Drawn larger: each of the game's pixels the average of its own.
        if (gpuScale > 1) {
            std::vector<uint8_t> small(scene.software.size() * 4);
            const int n = gpuScale * gpuScale;
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x)
                    for (int k = 0; k < 4; ++k) {
                        int sum = 0;
                        for (int dy = 0; dy < gpuScale; ++dy)
                            for (int dx = 0; dx < gpuScale; ++dx)
                                sum += gpu[(size_t(y * gpuScale + dy) * size_t(640 * gpuScale) +
                                            size_t(x * gpuScale + dx)) *
                                               4 +
                                           size_t(k)];
                        small[(size_t(y) * 640 + size_t(x)) * 4 + size_t(k)] = uint8_t((sum + n / 2) / n);
                    }
            gpu = std::move(small);
        }
        // Only where the scene draws, by its triangles' bounds (as
        // SceneCapture::kInstead covers): around them the buffer has what
        // the game left there.
        std::vector<uint8_t> inside(scene.software.size(), 0);
        auto mark = [&](int x0, int y0, int x1, int y1, const int *vp) {
            for (int y = std::max({0, y0, vp[1]}); y <= std::min({479, y1, vp[1] + vp[3] - 1}); ++y)
                for (int x = std::max({0, x0, vp[0]}); x <= std::min({639, x1, vp[0] + vp[2] - 1}); ++x)
                    inside[size_t(y) * 640 + size_t(x)] = 1;
        };
        for (const x86::DirectX::Scene::Step &st : scene.steps) {
            if (st.clearColor)
                mark(0, 0, 639, 479, st.viewport);
            for (uint32_t t = st.first; t + 2 < st.first + st.count && t + 2 < scene.vertices.size(); t += 3) {
                const x86::DirectX::Scene::Vertex *v = &scene.vertices[t];
                const auto [x0, x1] = std::minmax({v[0].x, v[1].x, v[2].x});
                const auto [y0, y1] = std::minmax({v[0].y, v[1].y, v[2].y});
                mark(int(std::ceil(x0)), int(std::ceil(y0)), int(std::floor(x1)), int(std::floor(y1)), st.viewport);
            }
        }
        std::vector<uint8_t> cpu(gpu.size());
        std::vector<uint8_t> diff(gpu.size());
        uint64_t differing = 0;
        uint64_t pixels = 0;
        for (size_t i = 0; i < scene.software.size(); ++i) {
            const uint32_t p = scene.software[i];
            const uint8_t c[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
            // In the steps of JK.EXE's 16-bit buffer (5, 6 and 5 bits):
            // more than one apart is a difference, not rounding.
            constexpr int kDrop[3] = {3, 2, 3};
            int most = 0;
            for (int k = 0; k < 3; ++k) {
                cpu[4 * i + size_t(k)] = c[k];
                most = std::max(most, std::abs((int(c[k]) >> kDrop[k]) - (int(gpu[4 * i + size_t(k)]) >> kDrop[k])));
            }
            cpu[4 * i + 3] = gpu[4 * i + 3] = diff[4 * i + 3] = 255;
            if (!inside[i])
                continue;
            ++pixels;
            diff[4 * i] = diff[4 * i + 1] = diff[4 * i + 2] = uint8_t(std::min(255, most * 64));
            differing += most > 1;
        }
        if (!pixels)
            return;
        ++compared.frames;
        compared.pixels += pixels;
        compared.differing += differing;
        const double share = double(differing) / double(pixels);
        const bool worst = share > compared.worst;
        if (worst)
            compared.worst = share, compared.worstFrame = compared.frames;
        // The first of every 20, and the worst so far.
        if (compared.frames % 20 == 1 || worst) {
            char name[32];
            std::snprintf(name, sizeof name, "%s%05llu", worst ? "worst-" : "", (unsigned long long)compared.frames);
            writeRgba(gpuFolder / (std::string(name) + "-cpu.png"), 640, 480, cpu);
            writeRgba(gpuFolder / (std::string(name) + "-gpu.png"), 640, 480, gpu);
            writeRgba(gpuFolder / (std::string(name) + "-diff.png"), 640, 480, diff);
        }
    };
    // -gpuplay: each frame drawn as the engine draws it with `drawing` gpu,
    // the scene on the GPU under the 2D; one in 60 written.
    reluva::Surface overlay;
    uint64_t played = 0;
    uint64_t playedScenes = 0;
    auto play = [&](const x86::DirectX::Frame &f) {
        if (f.width != 640 || f.height != 480)
            return;
        overlay.create(640, 480, reluva::PixelFormat::kRGBA32);
        for (int y = 0; y < 480; ++y) {
            uint8_t *row = overlay.row(y);
            for (int x = 0; x < 640; ++x, row += 4) {
                const uint32_t v = f.pixels[size_t(y) * 640 + size_t(x)];
                row[0] = uint8_t(v >> 16), row[1] = uint8_t(v >> 8), row[2] = uint8_t(v);
                row[3] = f.scene ? uint8_t(v >> 24) : 255;
            }
        }
        playedScenes += f.scene != nullptr;
        const bool keep = ++played % 60 == 0;
        if (keep)
            sceneGpu.captureNext();
        sceneGpu.draw(f.scene.get(), overlay);
        std::vector<uint8_t> rgba;
        const int w = 640 * gpuScale;
        const int h = 480 * gpuScale;
        if (!keep || !sceneGpu.capture(rgba) || rgba.size() != size_t(w) * size_t(h) * 4)
            return;
        // The capture is without the overlay: put over it as the renderer
        // does, enlarged as SceneGpu enlarges it.
        for (int y = 0; y < h; ++y) {
            const uint8_t *row = overlay.row(y / gpuScale);
            for (int x = 0; x < w; ++x) {
                const uint8_t *o = row + size_t(x / gpuScale) * 4;
                uint8_t *p = &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
                for (int k = 0; k < 3; ++k)
                    p[k] = uint8_t((o[k] * o[3] + p[k] * (255 - o[3]) + 127) / 255);
                p[3] = 255;
            }
        }
        char name[32];
        std::snprintf(name, sizeof name, "play-%05llu.png", (unsigned long long)played);
        writeRgba(gpuFolder / name, w, h, std::move(rgba));
    };
    uint64_t frames = 0;
    double nextSaved = 0;
    if (framesEvery > 0)
        std::filesystem::create_directories(framesFolder);
    // With -frames, one frame each so many seconds, named by the second,
    // or by the millisecond (00012-250.png) when they're closer together.
    directX.presented = [&](const x86::DirectX::Frame &f) {
        ++frames;
        if (window && gpuPlay)
            play(f);
        else if (f.scene && window)
            compare(*f.scene);
        const double now = elapsed();
        if (framesEvery > 0 && now >= nextSaved && f.width > 0) {
            char name[32];
            if (framesEvery >= 1)
                std::snprintf(name, sizeof name, "%05d.png", int(now));
            else
                std::snprintf(name, sizeof name, "%05d-%03d.png", int(now), int(now * 1000) % 1000);
            savePng(framesFolder / name, f);
            nextSaved = now + framesEvery;
        }
    };
    uint32_t exitCode = 0;
    const Error err = game.run(exitCode);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Stopped at the time given, not ended by itself: its Setup settings
    // saved for it, as Reluva's player does.
    if (!err && took >= seconds && game.saveSettings())
        std::printf("settings saved for the game\n");

    if (listFiles)
        for (const std::string &f : opened)
            std::printf("opened: %s\n", f.c_str());
    else
        std::printf("%zu files opened (-files lists them)\n", opened.size());
    for (const auto &[name, n] : called)
        std::printf("called: %s x%llu\n", name.c_str(), (unsigned long long)n);
    for (const std::string &name : lastCalled)
        std::printf("called last: %s\n", name.c_str());
    for (uint32_t a : peeks)
        std::printf("at %08x: %08x\n", a, cpu.valid(a, 4) ? cpu.read32(a) : 0u);
    if (!wav.empty()) {
        int peak = 0;
        for (int16_t v : recorded.samples)
            peak = std::max(peak, std::abs(int(v)));
        std::printf("sound: %.1f s recorded, peak %d\n", double(recorded.samples.size()) / 2 / kWavRate, peak);
        if (!reluva::writeFile(wav, reluva::audio::encodePcmWav(recorded)))
            std::fprintf(stderr, "can't write %s\n", wav.c_str());
    }
    const x86::DirectX::Frame &frame = directX.frame();
    std::printf("frames shown: %llu, the last %dx%d\n", (unsigned long long)frames, frame.width, frame.height);
    if (window && gpuPlay)
        std::printf("frames drawn on the GPU: %llu, %llu with a scene\n", (unsigned long long)played,
                    (unsigned long long)playedScenes);
    else if (window) {
        std::printf("scenes compared: %llu; pixels differing: %.4f%%; the worst, scene %llu: %.4f%%\n",
                    (unsigned long long)compared.frames,
                    compared.pixels ? 100.0 * double(compared.differing) / double(compared.pixels) : 0.0,
                    (unsigned long long)compared.worstFrame, 100.0 * std::max(0.0, compared.worst));
    }
    sceneGpu.setRenderer(nullptr);
    if (!png.empty() && frame.width > 0)
        savePng(png, frame);
    if (err) {
        std::printf("stopped: %s\n", err.message.c_str());
        const auto &r = cpu.faultReg;
        std::printf("registers: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n", r[0], r[1],
                    r[2], r[3], r[4], r[5], r[6], r[7]);
        // Return addresses on the stack: the words that point into the program's code.
        std::printf("on the stack, into JK.EXE's code:");
        for (uint32_t i = 0, shown = 0; i < 256 && shown < 12; ++i) {
            const uint32_t at = r[4] + i * 4;
            if (at + 4 > cpu.memory().size())
                break;
            const uint32_t v = cpu.read32(at);
            if (v >= 0x401000 && v < 0x522000) {
                std::printf(" %08x", v);
                ++shown;
            }
        }
        std::printf("\n");
    } else {
        std::printf("the program ended, exit code %u\n", exitCode);
    }
    if (network) {
        const x86::DirectPlay::Traffic t = game.directPlay().traffic();
        std::printf("network: %llu datagrams sent, %llu received\n", (unsigned long long)t.sent,
                    (unsigned long long)t.received);
    }
    std::printf("%llu instructions in %.1f s (%.1f million a second)\n", (unsigned long long)cpu.executed(), took,
                took > 0 ? double(cpu.executed()) / took / 1e6 : 0.0);
    return err ? 1 : 0;
}

// JK.EXE's code from an address on, an instruction a line.
int cmdDisasm(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool sith disasm <JK.EXE> <address> [count]\n");
        return 2;
    }
    std::vector<uint8_t> file;
    reluva::PeImage pe;
    if (!reluva::readFile(argv[2], file) || reluva::parsePe(file, pe)) {
        std::fprintf(stderr, "can't read %s\n", argv[2]);
        return 1;
    }
    uint32_t address = uint32_t(std::strtoul(argv[3], nullptr, 16));
    const int count = argc > 4 ? std::atoi(argv[4]) : 30;
    for (int i = 0; i < count; ++i) {
        const uint32_t rva = address - uint32_t(pe.imageBase);
        const reluva::PeImage::Section *in = nullptr;
        for (const auto &sec : pe.sections)
            if (rva >= sec.virtualAddress && rva < sec.virtualAddress + std::max(sec.virtualSize, sec.rawSize))
                in = &sec;
        if (!in || rva - in->virtualAddress >= in->rawSize)
            break;
        const size_t at = in->rawOffset + (rva - in->virtualAddress);
        const x86::Instruction ins = x86::disassemble(&file[at], file.size() - at, address);
        std::printf("%08x  %-8s %s\n", address, ins.mnemonic.c_str(), ins.operands.c_str());
        address += uint32_t(ins.size);
    }
    return 0;
}

const Commands kCommands(
    "sith",
    {
        {"host",
         "<install> [-seconds N] [-args \"...\"] [-png file] [-files] [-calls] [-d3d] [-net] [-join address] [-keys "
         "\"s:key,...\"] [-skim] [-cheat word] [-pad \"s:control=value,...\"] [-close seconds] [-frames N folder] "
         "[-peek address] [-wav file] [-write folder] [-gpu folder] [-gpuplay folder] [-gpuscale N] [-gpuaa N]",
         "run the retail JK.EXE in the x86 emulator: how far it gets", cmdHost},
        {"disasm", "<JK.EXE> <address> [count]", "the program's instructions from an address (hexadecimal) on",
         cmdDisasm},
    });

} // namespace
