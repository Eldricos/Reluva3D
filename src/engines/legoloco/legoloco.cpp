// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which runs the game's own
// program (docs/engines/legoloco/systems/running.md).

#include "engines/engine.h"
#include "engines/legoloco/host/player.h"

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace reluva::legoloco {

namespace {

// The releases it recognises, as `reluva --hash <files...>` prints them.
// The CD and an install have the same layout, and the installer copies the
// same files whichever of the CD's nine languages is chosen (it writes the
// choice into Exe/LEGO.INI), so the one entry stands for all of them.
const std::vector<GameVariant> kVariants = {
    {"legoloco",
     "LEGO Loco (1.0.18.2)",
     "en",
     "windows",
     {{"Exe/Loco.exe", "695dd274531337b1b600fd55f8f51482", 1117696},
      {"art-res/resource.RFH", "e7f73852368fbd30ca42b6d527b25242", 85647}}},
};

/**
 * The game running: its own program, loco.exe (Player), its frames shown
 * and its sound played, the player's mouse and keys passed on.
 */
class LegolocoEngine : public Engine {
public:
    LegolocoEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (!fs::findFile(_target.path, "Exe/Loco.exe"))
            return Error::fail("LEGO Loco's program, Exe/Loco.exe, isn't in " + _target.path.string());
        // Without an audio device nothing plays.
        _mixer = std::make_unique<audio::Mixer>(_sys.audioRate());
        _mixer->setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        _audioOut = !audio::startMixerOutput(_sys, *_mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, _audioOut};
        return play();
    }

private:
    // The game, until it ends or the player leaves. It draws its own
    // pointer (a hand) where the mouse is, so the system's is hidden.
    Error play() {
        Player player(_target.path, _target.savePath, _audioOut ? _mixer.get() : nullptr);
        player.start();
        int width = kWidth;
        int height = kHeight;
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
            _sys.setMouseCursorVisible(false);
        };
        screen();
        _sys.setWindowTitle("Reluva - " + _target.description);
        _sys.updateScreen(_screen); // black while it starts
        std::vector<uint32_t> pixels;
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if ((ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) &&
                    !confirmQuit(ev.type == EventType::kReturnToLauncher))
                    continue;
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                player.input(ev);
            }
            int w = 0;
            int h = 0;
            if (player.frame(pixels, w, h)) {
                if (w != width || h != height) {
                    width = w, height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory.
                for (int y = 0; y < height; ++y) {
                    uint8_t *row = _screen.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16), row[1] = uint8_t(v >> 8), row[2] = uint8_t(v), row[3] = 255;
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

    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed): the player is asked, as a layout not saved
    // would be lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description + " is still running. A layout not saved will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    // The desktop the game runs on (Win32Process's), its window filling it.
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 600;
    System &_sys;
    GameTarget _target;
    Surface _screen;
    std::unique_ptr<audio::Mixer> _mixer;
    bool _audioOut = false;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a LegolocoEngine to run one.
 */
class LegolocoMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "legoloco"; }
    const char *name() const override { return "LEGO Loco"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegolocoEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::legoloco

namespace reluva {

const MetaEngine &legolocoMetaEngine() {
    static const legoloco::LegolocoMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
