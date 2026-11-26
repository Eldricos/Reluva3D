// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which plays the retail
// JK.EXE in the x86 emulator (host/player.h).

#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/sith/folders.h"
#include "engines/sith/games.h"
#include "engines/sith/host/player.h"
#include "engines/sith/import.h"
#include "engines/sith/render/scene_gpu.h"
#include "graphics/gfx3d.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace reluva::sith {

namespace {

/**
 * The game running: JK.EXE on a thread of its own, its frames shown in the
 * window and the player's input passed on, until it ends or the player
 * leaves. What it writes goes to the target's save folder.
 */
class SithEngine : public Engine {
public:
    SithEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        // Without an audio device the game still has its sound card, unheard.
        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, audioOut};
        // `commandline` (reluva.ini): what follows JK.EXE, as a shortcut on
        // Windows would give it (-windowGUI, -devMode...).
        const auto commandLine = _target.settings.find("commandline");
        Player player(findFolders(_target.path, _target.settings), _target.savePath, audioOut ? &mixer : nullptr,
                      network(), commandLine == _target.settings.end() ? std::string() : commandLine->second);
        // The controllers, if they're offered: the game's two joysticks.
        const auto joystickSetting = _target.settings.find("joystick");
        const bool joystick = joystickSetting == _target.settings.end() || joystickSetting->second != "off";
        // A wheel's pedal axes where they aren't found ("accelerator,brake",
        // set in reluva.ini): wheelpedals the first's, wheel2pedals the second's.
        for (int i = 0; i < x86::Joysticks::kConnectable; ++i) {
            const auto pedals = _target.settings.find(i == 0 ? "wheelpedals" : "wheel2pedals");
            int accelerator = -1;
            int brake = -1;
            if (pedals != _target.settings.end() &&
                std::sscanf(pedals->second.c_str(), "%d,%d", &accelerator, &brake) == 2)
                _sys.setWheelPedals(i, accelerator, brake);
        }
        GamepadState pad;
        auto pollPad = [&] {
            for (int i = 0; i < x86::Joysticks::kConnectable; ++i)
                player.gamepad(i, joystick && _sys.readGamepad(i, pad) ? &pad : nullptr);
        };
        pollPad();
        // `drawing` gpu: the game's Direct3D scenes drawn on the GPU, its 2D
        // over them; in software, as the game's own, without a GPU.
        const auto drawing = _target.settings.find("drawing");
        const bool wantGpu = drawing != _target.settings.end() && drawing->second == "gpu";
        // `renderscale` (1 to 4): the GPU's picture this many times the
        // game's size.
        int scale = 2;
        if (const auto s = _target.settings.find("renderscale"); s != _target.settings.end())
            scale = std::clamp(std::atoi(s->second.c_str()), 1, 4);
        // `antialiasing` (off, 2 or 4): the GPU's samples a pixel, as many
        // of them as the device has.
        int samples = 4;
        if (const auto a = _target.settings.find("antialiasing"); a != _target.settings.end())
            samples = a->second == "off" ? 1 : std::clamp(std::atoi(a->second.c_str()), 1, 4);
        int width = 640;
        int height = 480;
        bool gpu = false;
        auto screen = [&] {
            _scenes.setRenderer(nullptr);
            gfx::Renderer3D *renderer = nullptr;
            if (wantGpu && !_sys.init3D(width * scale, height * scale, renderer) && renderer) {
                gpu = true;
                renderer->setMultisampling(samples);
                _scenes.setRenderer(renderer);
            } else {
                gpu = false;
                _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            }
            _screen.create(width, height, PixelFormat::kRGBA32);
        };
        screen();
        struct RendererGone {
            SceneGpu &scenes;
            ~RendererGone() { scenes.setRenderer(nullptr); }
        } rendererGone{_scenes};
        // Asked for once: the game can't change how it's drawn as it goes.
        player.keepScenes(gpu);
        // `framerate`: game, 60 frames a second, the world moved as the
        // game moves it (more than 20 ms apart, so about every other
        // frame); or 60, 75, 85 or 100, the world moved at each.
        int framesPerSecond = 60;
        bool everyFrame = false;
        if (const auto r = _target.settings.find("framerate"); r != _target.settings.end() && r->second != "game") {
            framesPerSecond = std::clamp(std::atoi(r->second.c_str()), 60, 100);
            everyFrame = true;
        }
        player.setPacing(framesPerSecond, everyFrame);
        player.start();
        // black while it starts
        if (gpu)
            _scenes.draw(nullptr, _screen);
        else
            _sys.updateScreen(_screen);
        _sys.setWindowTitle("Reluva - " + _target.description);
        // The menus show the system's pointer, which the game moves; in
        // play the mouse turns the view, so the window holds it.
        bool pointer = true;
        _sys.setMouseCursorVisible(true);
        std::vector<uint32_t> pixels;
        std::shared_ptr<const x86::DirectX::Scene> scene;
        while (true) {
            if (const bool shown = player.pointerShown(); shown != pointer) {
                pointer = shown;
                _sys.setRelativeMouse(!pointer);
                _sys.setMouseCursorVisible(pointer);
            }
            Event ev;
            while (_sys.pollEvent(ev)) {
                const bool leaving = ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher;
                if (leaving && confirmQuit(ev.type == EventType::kReturnToLauncher))
                    return Error::success();
                if (leaving)
                    continue;
                // The mouse in the GPU's frame, larger than the game's, put
                // back into the game's pixels; its movement too, which
                // comes scaled the same way, so the view turns as fast.
                if (gpu && scale > 1) {
                    ev.mouseX /= scale;
                    ev.mouseY /= scale;
                    ev.moveX /= float(scale);
                    ev.moveY /= float(scale);
                }
                player.input(ev);
            }
            pollPad();
            int w = 0;
            int h = 0;
            if (player.frame(pixels, w, h, scene)) {
                if (w != width || h != height) {
                    width = w;
                    height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory: the alpha 0 where the
                // scene shows through.
                for (int y = 0; y < height; ++y) {
                    uint8_t *row = _screen.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16);
                        row[1] = uint8_t(v >> 8);
                        row[2] = uint8_t(v);
                        row[3] = gpu && scene ? uint8_t(v >> 24) : 255;
                    }
                }
                if (gpu)
                    _scenes.draw(scene.get(), _screen);
                else
                    _sys.updateScreen(_screen);
            }
            if (const std::string out = player.takeOutput(); !out.empty())
                std::fputs(out.c_str(), stderr);
            bool ended = false;
            std::string error;
            if (player.finished(ended, error))
                return error.empty() ? Error::success() : Error::fail(_target.description + " stopped: " + error);
            // Until the game's next frame, shown as soon as it comes; the
            // window's events are looked at between, at most 5 ms apart.
            player.waitForFrame(std::chrono::milliseconds(5));
        }
    }

private:
    // Multiplayer over the machine's network: sessions looked for on the
    // local networks and on this machine, and at the host the joinaddress
    // setting names.
    x86::DirectPlay::Network network() {
        x86::DirectPlay::Network n;
        n.open = [this](uint16_t port) {
            std::unique_ptr<net::UdpSocket> socket;
            return _sys.openUdp(port, socket) ? nullptr : std::move(socket);
        };
        n.search.push_back(net::Address::broadcast(x86::kDirectPlayPort));
        for (uint32_t ip : _sys.broadcastAddresses())
            n.search.push_back({ip, x86::kDirectPlayPort});
        n.search.push_back({0x7f000001, x86::kDirectPlayPort});
        if (const auto join = _target.settings.find("joinaddress"); join != _target.settings.end()) {
            const auto [host, port] = net::splitHostPort(join->second, x86::kDirectPlayPort);
            uint32_t ip = 0;
            if (!host.empty() && _sys.resolveHost(host, ip))
                n.search.push_back({ip, port});
        }
        return n;
    }

    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed): what the game hasn't saved would be lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description +
                                    " is still running. Anything since the game was last saved will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    SceneGpu _scenes;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, importing it from its CDs, and making a SithEngine to
 * run one.
 */
class SithMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "sith"; }
    const char *name() const override { return "Sith"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override { return detectGames(dir); }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        return sith::planImport(source);
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message) const override {
        return sith::addToImport(plan, folder, message);
    }

    // The settings the launcher shows for the game; the first choice is
    // the default.
    std::vector<GameOption> engineOptions() const override {
        GameOption joystick;
        joystick.key = "joystick";
        joystick.label = "Game controller";
        joystick.tooltip = "Connected controllers offered to the game as its joysticks, two at most.";
        joystick.choices = {{"on", "Offered to the game"}, {"off", "Left out"}};
        GameOption drawing;
        drawing.key = "drawing";
        drawing.label = "3D drawing";
        drawing.tooltip = "The game's Direct3D drawn in software, as the game draws it itself; or on the "
                          "graphics card, its menus and gauges over it.";
        drawing.choices = {{"software", "In software"}, {"gpu", "On the graphics card"}};
        GameOption size;
        size.key = "renderscale";
        size.label = "Picture size";
        size.tooltip = "With the 3D on the graphics card, the game's 640x480 drawn at this many times its size: "
                       "the same picture with finer 3D. The menus and gauges are enlarged as they are.";
        size.choices = {
            {"2", "2x (1280x960)"}, {"1", "1x (640x480)"}, {"3", "3x (1920x1440)"}, {"4", "4x (2560x1920)"}};
        GameOption smoothing;
        smoothing.key = "antialiasing";
        smoothing.label = "Anti-aliasing";
        smoothing.tooltip = "With the 3D on the graphics card, the edges of its polygons smoothed by "
                            "multisampling, as far as the card can.";
        smoothing.choices = {{"4", "4x"}, {"2", "2x"}, {"off", "Off"}};
        GameOption rate;
        rate.key = "framerate";
        rate.label = "Frame rate";
        rate.tooltip = "As the game: 60 frames a second, its world moved only every other one. Or that many "
                       "frames a second, its world moved at each (100 at most, the game's limit). A display "
                       "slower than the rate shows no more than its own.";
        rate.choices = {{"game", "As the game"}, {"60", "60"}, {"75", "75"}, {"85", "85"}, {"100", "100"}};
        return {joystick, drawing, size, smoothing, rate};
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SithEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::sith

namespace reluva {

const MetaEngine &sithMetaEngine() {
    static const sith::SithMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
