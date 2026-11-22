// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which runs the game's own
// program.

#include "common/fs.h"
#include "engines/engine.h"
#include "engines/sfc/host/game_process.h"
#include "engines/sfc/host/player.h"

#include <cstdio>
#include <string>
#include <vector>

namespace reluva::sfc {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
// The 2.0.3.6 patch rewrites both files, so a patched install is an unknown
// release until its own are hashed.
const std::vector<GameVariant> kVariants = {
    {"sfc2",
     "Star Trek: Starfleet Command II (1.0)",
     "en",
     "windows",
     {{"StarFleet2.exe", "e78f2d2c9c9c51f2077bb5c10cbaf617", 4149248},
      {"Assets/Sprites/sprites.q3", "7eaeae9272f5d82ac6523a3d6191a45a", 78253026}}},
};

// The same game by its files' names alone, for releases not in kVariants:
// used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"sfc2",
     "Star Trek: Starfleet Command II (unknown release)",
     "",
     "windows",
     {{"StarFleet2.exe", "", -1}, {"Assets/Sprites/sprites.q3", "", -1}}},
};

/**
 * The game running: its own StarFleet2.exe in the x86 emulator (Player),
 * its frames in the window, the mouse and keys passed to it.
 */
class SfcEngine : public Engine {
public:
    SfcEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (!fs::findFile(_target.path, GameProcess::kProgram))
            return Error::fail(std::string("Starfleet Command II's program, ") + GameProcess::kProgram + ", isn't in " +
                               _target.path.string());
        Player player(_target.path, _target.savePath);
        player.start();
        int width = 800; // the game's own resolution, until its first frame says
        int height = 600;
        // The game draws its own pointer, where the mouse is on its screen.
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
            _sys.setMouseCursorVisible(false);
        };
        screen();
        _sys.setWindowTitle("Reluva - " + _target.description);
        _sys.updateScreen(_screen); // black while it loads
        std::vector<uint32_t> pixels;
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                    if (confirmQuit(ev.type == EventType::kReturnToLauncher))
                        return Error::success();
                    continue;
                }
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

private:
    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed), as what the game hasn't saved is lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description + " is still running. Anything it hasn't saved will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a SfcEngine to run one.
 */
class SfcMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "sfc"; }
    const char *name() const override { return "Starfleet Command"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SfcEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::sfc

namespace reluva {

const MetaEngine &sfcMetaEngine() {
    static const sfc::SfcMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
