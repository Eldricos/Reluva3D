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

#include <cstdio>
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
        Player player(findFolders(_target.path, _target.settings), _target.savePath, audioOut ? &mixer : nullptr);
        player.start();
        int width = 640;
        int height = 480;
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
        };
        screen();
        _sys.updateScreen(_screen); // black while it starts
        _sys.setWindowTitle("Reluva - " + _target.description);
        // The menus show the system's pointer, which the game moves; in
        // play the mouse turns the view, so the window holds it.
        bool pointer = true;
        _sys.setMouseCursorVisible(true);
        std::vector<uint32_t> pixels;
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
                if (!leaving)
                    player.input(ev);
            }
            int w = 0;
            int h = 0;
            if (player.frame(pixels, w, h)) {
                if (w != width || h != height) {
                    width = w;
                    height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory.
                for (int y = 0; y < height; ++y) {
                    uint8_t *row = _screen.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16);
                        row[1] = uint8_t(v >> 8);
                        row[2] = uint8_t(v);
                        row[3] = 255;
                    }
                }
                _sys.updateScreen(_screen);
            }
            if (const std::string out = player.takeOutput(); !out.empty())
                std::fputs(out.c_str(), stderr);
            bool ended = false;
            std::string error;
            if (player.finished(ended, error))
                return error.empty() ? Error::success() : Error::fail(_target.description + " stopped: " + error);
            _sys.delayMillis(5);
        }
    }

private:
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
